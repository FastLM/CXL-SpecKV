#include "cache_engine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>

namespace cxlspeckv {

FPGACacheEngine::FPGACacheEngine(
    size_t num_engines,
    double clock_frequency_mhz,
    size_t data_width,
    size_t hbm_channels
) : num_engines_(num_engines),
    clock_frequency_mhz_(clock_frequency_mhz),
    data_width_bits_(data_width),
    hbm_channels_(hbm_channels),
    tlb_size_(1024)
{
    tlb_.resize(tlb_size_);
    for (auto& entry : tlb_) {
        entry.valid = false;
    }
    
    // Initialize layer compression ratios (from paper: early layers 3-4×, late layers 2.5-3×)
    layer_compression_ratios_.resize(80, 3.2);  // Default 3.2× for 80-layer model
    for (size_t i = 0; i < 80; ++i) {
        if (i < 80 / 3) {
            layer_compression_ratios_[i] = 3.5;  // Early layers
        } else if (i > 2 * 80 / 3) {
            layer_compression_ratios_[i] = 2.75;  // Late layers
        }
    }
    
    reset_statistics();
}

FPGACacheEngine::~FPGACacheEngine() = default;

FPGACacheEngine::CompressedData FPGACacheEngine::compress(
    const std::vector<float>& kv_data,
    size_t num_tokens,
    size_t hidden_dim,
    uint32_t layer_id
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    CompressedData result;
    result.original_size = kv_data.size() * sizeof(float);
    
    // Stage 5-8: Scaling and quantization (FP16 -> INT8)
    float scale = compute_scale_factor(kv_data);
    result.scale_factor = scale;
    std::vector<int8_t> quantized = quantize_to_int8(kv_data, scale);
    
    // Stage 9-14: Delta encoding
    std::vector<int8_t> delta_encoded = delta_encode(quantized);
    
    // Stage 15-18: Run-length encoding
    std::vector<uint8_t> rle_encoded = run_length_encode(delta_encoded);
    
    result.rle_data = std::vector<int8_t>(rle_encoded.begin(), rle_encoded.end());
    result.compressed_size = result.rle_data.size();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_compressions++;
        double ratio = static_cast<double>(result.original_size) / result.compressed_size;
        stats_.avg_compression_ratio = 
            (stats_.avg_compression_ratio * (stats_.total_compressions - 1) + ratio) /
            stats_.total_compressions;
        stats_.avg_compression_latency_ns = 
            (stats_.avg_compression_latency_ns * (stats_.total_compressions - 1) + latency_ns) /
            stats_.total_compressions;
    }
    
    return result;
}

std::vector<float> FPGACacheEngine::decompress(
    const CompressedData& compressed,
    size_t num_tokens,
    size_t hidden_dim
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Inverse pipeline: RLE -> Delta -> Dequantize
    
    // Stage 15-18 (inverse): Run-length decode
    std::vector<uint8_t> rle_data(compressed.rle_data.begin(), compressed.rle_data.end());
    std::vector<int8_t> delta_decoded = run_length_decode(rle_data);
    
    // Stage 9-14 (inverse): Delta decode
    std::vector<int8_t> quantized = delta_decode(delta_decoded);
    
    // Stage 5-8 (inverse): Dequantize (INT8 -> FP16)
    std::vector<float> decompressed = dequantize_from_int8(quantized, compressed.scale_factor);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_decompressions++;
        stats_.avg_decompression_latency_ns = 
            (stats_.avg_decompression_latency_ns * (stats_.total_decompressions - 1) + latency_ns) /
            stats_.total_decompressions;
    }
    
    return decompressed;
}

uint64_t FPGACacheEngine::translate_address(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> tlb_lock(tlb_mutex_);
    
    // TLB lookup
    size_t tlb_index = (virtual_addr >> 12) % tlb_size_;  // 4KB page alignment
    TLBEntry& entry = tlb_[tlb_index];
    
    if (entry.valid && entry.virtual_addr == (virtual_addr & ~0xFFFULL)) {
        // TLB hit
        return entry.physical_addr + (virtual_addr & 0xFFF);
    }
    
    // TLB miss - would perform page walk in real implementation
    // For now, simplified translation
    uint64_t physical_addr = 0x4000000000ULL + (virtual_addr & 0xFFFFFFFFFFFFULL);
    
    // Update TLB
    entry.virtual_addr = virtual_addr & ~0xFFFULL;
    entry.physical_addr = physical_addr & ~0xFFFULL;
    entry.valid = true;
    
    return physical_addr;
}

double FPGACacheEngine::get_compression_ratio(uint32_t layer_id) const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    if (layer_id < layer_compression_ratios_.size()) {
        return layer_compression_ratios_[layer_id];
    }
    return 3.2;  // Default
}

FPGACacheEngine::EngineStatistics FPGACacheEngine::get_statistics() const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    EngineStatistics stats = stats_;
    stats.throughput_gbps = compute_throughput_gbps();
    
    return stats;
}

void FPGACacheEngine::reset_statistics() {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_ = EngineStatistics{};
}

void FPGACacheEngine::set_num_engines(size_t num_engines) {
    num_engines_ = num_engines;
}

size_t FPGACacheEngine::get_num_engines() const {
    return num_engines_;
}

float FPGACacheEngine::compute_scale_factor(const std::vector<float>& data) {
    // Find max absolute value
    float max_val = 0.0f;
    for (float val : data) {
        float abs_val = std::abs(val);
        if (abs_val > max_val) {
            max_val = abs_val;
        }
    }
    
    // Scale factor: s = max(|X|) / 127
    return (max_val > 0.0f) ? (max_val / 127.0f) : 1.0f;
}

std::vector<int8_t> FPGACacheEngine::quantize_to_int8(const std::vector<float>& data, float scale) {
    std::vector<int8_t> quantized(data.size());
    
    for (size_t i = 0; i < data.size(); ++i) {
        float scaled = data[i] / scale;
        int8_t quantized_val = static_cast<int8_t>(std::round(scaled * 127.0f));
        quantized[i] = std::max(-128, std::min(127, static_cast<int>(quantized_val)));
    }
    
    return quantized;
}

std::vector<int8_t> FPGACacheEngine::delta_encode(const std::vector<int8_t>& data) {
    if (data.empty()) {
        return data;
    }
    
    std::vector<int8_t> delta(data.size());
    delta[0] = data[0];  // First element unchanged
    
    for (size_t i = 1; i < data.size(); ++i) {
        delta[i] = data[i] - data[i - 1];
    }
    
    return delta;
}

std::vector<uint8_t> FPGACacheEngine::run_length_encode(const std::vector<int8_t>& data) {
    if (data.empty()) {
        return {};
    }
    
    std::vector<uint8_t> rle;
    int8_t current_val = data[0];
    size_t count = 1;
    
    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i] == current_val && count < 255) {
            count++;
        } else {
            // Encode: [value][count]
            rle.push_back(static_cast<uint8_t>(current_val));
            rle.push_back(static_cast<uint8_t>(count));
            current_val = data[i];
            count = 1;
        }
    }
    
    // Encode last run
    rle.push_back(static_cast<uint8_t>(current_val));
    rle.push_back(static_cast<uint8_t>(count));
    
    return rle;
}

std::vector<int8_t> FPGACacheEngine::run_length_decode(const std::vector<uint8_t>& rle_data) {
    std::vector<int8_t> decoded;
    
    for (size_t i = 0; i < rle_data.size(); i += 2) {
        if (i + 1 >= rle_data.size()) {
            break;
        }
        
        int8_t value = static_cast<int8_t>(rle_data[i]);
        uint8_t count = rle_data[i + 1];
        
        for (size_t j = 0; j < count; ++j) {
            decoded.push_back(value);
        }
    }
    
    return decoded;
}

std::vector<int8_t> FPGACacheEngine::delta_decode(const std::vector<int8_t>& delta_data) {
    if (delta_data.empty()) {
        return delta_data;
    }
    
    std::vector<int8_t> decoded(delta_data.size());
    decoded[0] = delta_data[0];  // First element unchanged
    
    for (size_t i = 1; i < delta_data.size(); ++i) {
        decoded[i] = decoded[i - 1] + delta_data[i];
    }
    
    return decoded;
}

std::vector<float> FPGACacheEngine::dequantize_from_int8(const std::vector<int8_t>& data, float scale) {
    std::vector<float> dequantized(data.size());
    
    for (size_t i = 0; i < data.size(); ++i) {
        float scaled = static_cast<float>(data[i]) / 127.0f;
        dequantized[i] = scaled * scale;
    }
    
    return dequantized;
}

size_t FPGACacheEngine::compute_pipeline_latency_cycles() const {
    // Pipeline has 20 stages, critical path is 25 cycles (from paper)
    return 25;
}

double FPGACacheEngine::compute_throughput_gbps() const {
    // Throughput = data_width * clock_frequency / 8
    // From paper: 51.2 GB/s per engine at 800MHz with 512-bit width
    double throughput_per_engine = (data_width_bits_ / 8.0) * (clock_frequency_mhz_ / 1000.0);
    return throughput_per_engine * num_engines_;
}

} // namespace cxlspeckv

