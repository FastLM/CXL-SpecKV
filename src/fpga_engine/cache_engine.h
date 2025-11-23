#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <atomic>
#include <unordered_map>

namespace cxlspeckv {

// Compression ratio per layer
struct CompressionStats {
    double ratio;
    size_t original_size;
    size_t compressed_size;
};

// FPGA Cache Engine - implements compression/decompression pipeline
class FPGACacheEngine {
public:
    FPGACacheEngine(
        size_t num_engines = 1,
        double clock_frequency_mhz = 800.0,
        size_t data_width = 512,  // bits
        size_t hbm_channels = 16
    );
    
    ~FPGACacheEngine();

    // Compression pipeline
    // Input: KV page X ∈ R^(N×d) (FP16)
    // Output: Compressed data D_comp = ⟨s, D_RLE⟩
    struct CompressedData {
        float scale_factor;
        std::vector<int8_t> rle_data;
        size_t original_size;
        size_t compressed_size;
    };
    
    CompressedData compress(
        const std::vector<float>& kv_data,
        size_t num_tokens,
        size_t hidden_dim,
        uint32_t layer_id
    );

    // Decompression pipeline
    // Input: Compressed data D_comp
    // Output: Decompressed KV page X ∈ R^(N×d)
    std::vector<float> decompress(
        const CompressedData& compressed,
        size_t num_tokens,
        size_t hidden_dim
    );

    // Address Translation Unit (ATU)
    uint64_t translate_address(uint64_t virtual_addr);
    
    // Get compression ratio for a layer
    double get_compression_ratio(uint32_t layer_id) const;
    
    // Statistics
    struct EngineStatistics {
        size_t total_compressions;
        size_t total_decompressions;
        double avg_compression_ratio;
        double avg_compression_latency_ns;
        double avg_decompression_latency_ns;
        double throughput_gbps;
    };
    
    EngineStatistics get_statistics() const;
    void reset_statistics();

    // Multi-engine scaling
    void set_num_engines(size_t num_engines);
    size_t get_num_engines() const;

private:
    size_t num_engines_;
    double clock_frequency_mhz_;
    size_t data_width_bits_;
    size_t hbm_channels_;
    
    // Compression pipeline stages
    // Stage 1-4: Input buffering
    // Stage 5-8: Scaling and quantization (FP16 -> INT8)
    // Stage 9-14: Delta encoding
    // Stage 15-18: Run-length encoding (RLE)
    // Stage 19-20: Output formatting
    
    // Compression algorithm components
    float compute_scale_factor(const std::vector<float>& data);
    std::vector<int8_t> quantize_to_int8(const std::vector<float>& data, float scale);
    std::vector<int8_t> delta_encode(const std::vector<int8_t>& data);
    std::vector<uint8_t> run_length_encode(const std::vector<int8_t>& data);
    
    // Decompression pipeline (inverse operations)
    std::vector<int8_t> run_length_decode(const std::vector<uint8_t>& rle_data);
    std::vector<int8_t> delta_decode(const std::vector<int8_t>& delta_data);
    std::vector<float> dequantize_from_int8(const std::vector<int8_t>& data, float scale);
    
    // TLB for address translation
    struct TLBEntry {
        uint64_t virtual_addr;
        uint64_t physical_addr;
        bool valid;
    };
    std::vector<TLBEntry> tlb_;
    size_t tlb_size_;
    mutable std::mutex tlb_mutex_;
    
    // Layer-specific compression ratios
    std::vector<double> layer_compression_ratios_;
    mutable std::mutex stats_mutex_;
    
    // Statistics
    mutable EngineStatistics stats_;
    
    // Helper functions
    size_t compute_pipeline_latency_cycles() const;
    double compute_throughput_gbps() const;
};

} // namespace cxlspeckv

