#include "cxl_speckv_system.h"
#include <iostream>
#include <vector>
#include <chrono>

using namespace cxlspeckv;

int main(int argc, char* argv[]) {
    std::cout << "CXL-SpecKV System Demo\n";
    std::cout << "=====================\n\n";
    
    // Create system
    CXLSpecKVSystem system;
    
    // Configure system
    CXLSpecKVSystem::SystemConfig config;
    config.l1_size_gb = 12;
    config.l2_size_gb = 3;
    config.l3_size_gb = 128;
    config.prefetch_depth = 4;
    config.history_length = 16;
    config.num_fpga_engines = 1;
    config.fpga_clock_mhz = 800.0;
    config.num_layers = 80;
    config.hidden_dim = 8192;
    config.num_heads = 64;
    
    // Initialize system
    std::cout << "Initializing CXL-SpecKV system...\n";
    if (!system.initialize(config)) {
        std::cerr << "Failed to initialize system!\n";
        return 1;
    }
    std::cout << "System initialized successfully.\n\n";
    
    // Demo: Process some token batches
    std::cout << "Processing token batches...\n";
    std::vector<std::vector<uint32_t>> token_batches = {
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
        {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}
    };
    
    std::vector<std::vector<float>> kv_cache_outputs;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (system.process_tokens(token_batches, kv_cache_outputs)) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        std::cout << "Processed " << token_batches.size() << " batches in " << duration << " ms\n";
    } else {
        std::cerr << "Failed to process tokens!\n";
        return 1;
    }
    
    // Demo: Generate next token with prefetching
    std::cout << "\nGenerating next token with speculative prefetching...\n";
    std::vector<uint32_t> token_history = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint32_t next_token = system.generate_next_token(token_history, 0);
    std::cout << "Generated token: " << next_token << "\n";
    
    // Get and display statistics
    std::cout << "\nSystem Statistics:\n";
    std::cout << "==================\n";
    auto stats = system.get_statistics();
    std::cout << "Prefetch Hit Rate: " << (stats.prefetch.hit_rate * 100.0) << "%\n";
    std::cout << "Memory L1 Hit Rate: " << (stats.memory.l1_hit_rate * 100.0) << "%\n";
    std::cout << "FPGA Compression Ratio: " << stats.fpga.avg_compression_ratio << "x\n";
    std::cout << "FPGA Throughput: " << stats.fpga.throughput_gbps << " GB/s\n";
    
    std::cout << "\nDemo completed successfully!\n";
    return 0;
}

