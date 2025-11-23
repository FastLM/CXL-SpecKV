#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>

namespace cxlspeckv {

// Forward declarations
class CXLMemoryAllocator;
class CXLMemoryManager;
class SpeculativePrefetcher;
class FPGACacheEngine;

// Main CXL-SpecKV system orchestrator
class CXLSpecKVSystem {
public:
    struct SystemConfig {
        // Memory configuration
        size_t l1_size_gb = 12;
        size_t l2_size_gb = 3;
        size_t l3_size_gb = 128;
        
        // Prefetcher configuration
        size_t prefetch_depth = 4;
        size_t history_length = 16;
        
        // FPGA engine configuration
        size_t num_fpga_engines = 1;
        double fpga_clock_mhz = 800.0;
        size_t data_width_bits = 512;
        size_t hbm_channels = 16;
        
        // Model configuration
        size_t num_layers = 80;
        size_t hidden_dim = 8192;
        size_t num_heads = 64;
    };
    
    CXLSpecKVSystem();
    ~CXLSpecKVSystem();
    
    // Initialize system with configuration
    bool initialize(const SystemConfig& config);
    
    // Main inference interface
    // Process a batch of tokens and generate KV-cache
    bool process_tokens(
        const std::vector<std::vector<uint32_t>>& token_batches,
        std::vector<std::vector<float>>& kv_cache_outputs
    );
    
    // Generate next token with speculative prefetching
    uint32_t generate_next_token(
        const std::vector<uint32_t>& token_history,
        uint32_t layer_id
    );
    
    // Get system statistics
    struct SystemStatistics {
        // Memory statistics
        struct {
            size_t l1_hits;
            size_t l1_misses;
            size_t l2_hits;
            size_t l3_accesses;
            double l1_hit_rate;
        } memory;
        
        // Prefetch statistics
        struct {
            size_t total_prefetches;
            size_t successful_prefetches;
            double hit_rate;
            double avg_latency_us;
        } prefetch;
        
        // FPGA engine statistics
        struct {
            size_t total_compressions;
            size_t total_decompressions;
            double avg_compression_ratio;
            double throughput_gbps;
        } fpga;
        
        // Overall performance
        double throughput_tokens_per_sec;
        double avg_latency_ms;
    };
    
    SystemStatistics get_statistics() const;
    void reset_statistics();
    
    // Get component interfaces (for advanced usage)
    CXLMemoryAllocator* get_allocator() const { return allocator_.get(); }
    CXLMemoryManager* get_memory_manager() const;
    SpeculativePrefetcher* get_prefetcher() const;
    FPGACacheEngine* get_cache_engine() const;

private:
    SystemConfig config_;
    std::unique_ptr<CXLMemoryAllocator> allocator_;
    
    // Component pointers (owned by allocator, but cached for direct access)
    CXLMemoryManager* memory_manager_;
    SpeculativePrefetcher* prefetcher_;
    FPGACacheEngine* cache_engine_;
    
    bool initialized_;
    
    // Helper functions
    void update_statistics();
};

} // namespace cxlspeckv

