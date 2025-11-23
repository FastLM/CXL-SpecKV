#include "speculative_prefetcher.h"
#include "lstm_predictor.h"
#include "../cxl_memory/cxl_memory_manager.h"
#include <algorithm>
#include <chrono>

namespace cxlspeckv {

SpeculativePrefetcher::SpeculativePrefetcher(
    CXLMemoryManager* memory_manager,
    size_t prefetch_depth,
    size_t history_length
) : memory_manager_(memory_manager),
    predictor_(std::make_unique<LSTMPredictor>(32000, 64, 128, 2, history_length)),
    prefetch_depth_(prefetch_depth),
    history_length_(history_length),
    adaptive_depth_(prefetch_depth),
    accuracy_window_size_(100)
{
    reset_statistics();
}

SpeculativePrefetcher::~SpeculativePrefetcher() = default;

std::vector<PrefetchRequest> SpeculativePrefetcher::prefetch(
    const std::vector<uint32_t>& token_history,
    uint32_t layer_id,
    size_t depth
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    size_t actual_depth = (depth > 0) ? depth : adaptive_depth_.load();
    
    // Step 1: Predict tokens using LSTM
    auto predictions = predictor_->predict_top_k(token_history, actual_depth);
    
    std::vector<PrefetchRequest> prefetch_requests;
    prefetch_requests.reserve(actual_depth);
    
    // Step 2: For each predicted token, compute KV-cache address and issue prefetch
    for (size_t i = 0; i < predictions.size(); ++i) {
        uint32_t predicted_token = predictions[i].first;
        float confidence = predictions[i].second;
        
        // Compute virtual address for KV-cache entry
        // Simplified: assume we can compute from (request_id, layer_id, position)
        // In real implementation, this would use proper address translation
        uint64_t virtual_addr = compute_kv_address(0, layer_id, i + 1);  // position = i+1 for next tokens
        
        // Check if already in L1 or L2
        if (memory_manager_->is_in_cache(virtual_addr, MemoryTier::L1_GPU_LOCAL) ||
            memory_manager_->is_in_cache(virtual_addr, MemoryTier::L2_PREFETCH)) {
            continue;  // Already cached, skip
        }
        
        PrefetchRequest req;
        req.virtual_addr = virtual_addr;
        req.layer_id = layer_id;
        req.predicted_token_id = predicted_token;
        req.confidence = confidence;
        req.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        
        prefetch_requests.push_back(req);
        
        // Issue non-blocking DMA prefetch
        issue_dma_prefetch(req);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_prefetches += prefetch_requests.size();
        stats_.avg_prediction_latency_us = 
            (stats_.avg_prediction_latency_us * (stats_.total_prefetches - prefetch_requests.size()) + latency_us) /
            stats_.total_prefetches;
    }
    
    return prefetch_requests;
}

void SpeculativePrefetcher::handle_misprediction(
    uint32_t actual_token,
    const std::vector<uint32_t>& predicted_tokens
) {
    bool was_correct = std::find(predicted_tokens.begin(), predicted_tokens.end(), actual_token) != predicted_tokens.end();
    
    if (!was_correct) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.mispredictions++;
        
        // Lazy invalidation: invalid prefetch buffer entries are not immediately evicted
        // They will be overwritten by new prefetches or naturally evicted
    }
}

void SpeculativePrefetcher::update_prediction_accuracy(uint32_t request_id, bool was_correct) {
    accuracy_history_.push_back(was_correct ? 1.0 : 0.0);
    if (accuracy_history_.size() > accuracy_window_size_) {
        accuracy_history_.erase(accuracy_history_.begin());
    }
    
    // Compute recent accuracy
    if (accuracy_history_.size() >= 10) {
        double recent_accuracy = 0.0;
        for (size_t i = accuracy_history_.size() - 10; i < accuracy_history_.size(); ++i) {
            recent_accuracy += accuracy_history_[i];
        }
        recent_accuracy /= 10.0;
        
        // Adaptively adjust depth
        if (recent_accuracy > 0.95 && adaptive_depth_.load() < 8) {
            adaptive_depth_++;
        } else if (recent_accuracy < 0.85 && adaptive_depth_.load() > 2) {
            adaptive_depth_--;
        }
    }
}

size_t SpeculativePrefetcher::get_adaptive_depth() const {
    return adaptive_depth_.load();
}

SpeculativePrefetcher::PrefetchStatistics SpeculativePrefetcher::get_statistics() const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    PrefetchStatistics stats = stats_;
    if (stats.total_prefetches > 0) {
        stats.hit_rate = static_cast<double>(stats.successful_prefetches) / stats.total_prefetches;
        stats.precision = static_cast<double>(stats.successful_prefetches) / 
                         (stats.successful_prefetches + stats.mispredictions + 1);
    }
    
    return stats;
}

void SpeculativePrefetcher::reset_statistics() {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_ = PrefetchStatistics{};
}

void SpeculativePrefetcher::set_prefetch_depth(size_t depth) {
    prefetch_depth_ = depth;
    adaptive_depth_ = depth;
}

size_t SpeculativePrefetcher::get_prefetch_depth() const {
    return prefetch_depth_;
}

uint64_t SpeculativePrefetcher::compute_kv_address(uint32_t req_id, uint32_t layer_id, uint32_t position) {
    // Simplified address computation
    // In real implementation, this would use proper address translation unit (ATU)
    // Address format: [req_id:32][layer_id:16][position:16]
    return (static_cast<uint64_t>(req_id) << 32) | 
           (static_cast<uint64_t>(layer_id) << 16) | 
           static_cast<uint64_t>(position);
}

void SpeculativePrefetcher::issue_dma_prefetch(const PrefetchRequest& req) {
    // In real implementation, this would issue actual DMA transfer
    // For now, we just track the request
    std::lock_guard<std::mutex> queue_lock(prefetch_queue_mutex_);
    outstanding_prefetches_.push(req);
    
    // Limit queue size
    if (outstanding_prefetches_.size() > 16) {
        outstanding_prefetches_.pop();
    }
}

bool SpeculativePrefetcher::is_already_prefetched(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> queue_lock(prefetch_queue_mutex_);
    
    std::queue<PrefetchRequest> temp_queue = outstanding_prefetches_;
    while (!temp_queue.empty()) {
        if (temp_queue.front().virtual_addr == virtual_addr) {
            return true;
        }
        temp_queue.pop();
    }
    return false;
}

} // namespace cxlspeckv

