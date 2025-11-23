#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <atomic>

namespace cxlspeckv {

// Forward declaration
class LSTMPredictor;
class CXLMemoryManager;

// Token prediction result
struct TokenPrediction {
    uint32_t token_id;
    float confidence;
};

// Prefetch request
struct PrefetchRequest {
    uint64_t virtual_addr;
    uint32_t layer_id;
    uint32_t predicted_token_id;
    float confidence;
    uint64_t timestamp;
};

// Speculative Prefetcher
class SpeculativePrefetcher {
public:
    SpeculativePrefetcher(
        CXLMemoryManager* memory_manager,
        size_t prefetch_depth = 4,
        size_t history_length = 16
    );
    
    ~SpeculativePrefetcher();

    // Main prefetch interface
    // Input: token history H_t = {t-15, ..., t0}
    // Output: Prefetched KV entries in L2 buffer
    std::vector<PrefetchRequest> prefetch(
        const std::vector<uint32_t>& token_history,
        uint32_t layer_id,
        size_t depth = 0  // 0 means use default
    );

    // Handle misprediction
    void handle_misprediction(uint32_t actual_token, const std::vector<uint32_t>& predicted_tokens);
    
    // Adaptive prediction depth adjustment
    void update_prediction_accuracy(uint32_t request_id, bool was_correct);
    size_t get_adaptive_depth() const;
    
    // Statistics
    struct PrefetchStatistics {
        size_t total_prefetches;
        size_t successful_prefetches;
        size_t mispredictions;
        double hit_rate;
        double precision;
        double avg_prediction_latency_us;
    };
    
    PrefetchStatistics get_statistics() const;
    void reset_statistics();

    // Configuration
    void set_prefetch_depth(size_t depth);
    size_t get_prefetch_depth() const;

private:
    CXLMemoryManager* memory_manager_;
    std::unique_ptr<LSTMPredictor> predictor_;
    
    size_t prefetch_depth_;
    size_t history_length_;
    
    // Adaptive depth management
    std::atomic<size_t> adaptive_depth_;
    mutable std::mutex depth_mutex_;
    std::vector<double> accuracy_history_;
    size_t accuracy_window_size_;
    
    // Outstanding prefetch requests
    std::queue<PrefetchRequest> outstanding_prefetches_;
    std::mutex prefetch_queue_mutex_;
    
    // Statistics
    mutable PrefetchStatistics stats_;
    mutable std::mutex stats_mutex_;
    
    // Helper functions
    uint64_t compute_kv_address(uint32_t req_id, uint32_t layer_id, uint32_t position);
    void issue_dma_prefetch(const PrefetchRequest& req);
    bool is_already_prefetched(uint64_t virtual_addr);
};

} // namespace cxlspeckv

