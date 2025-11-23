#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace cxlspeckv {

// Forward declarations
class CXLMemoryManager;
class SpeculativePrefetcher;
class FPGACacheEngine;

// Memory allocator interface for LLM serving frameworks
// Compatible with vLLM and TensorRT-LLM
class CXLMemoryAllocator {
public:
    CXLMemoryAllocator();
    ~CXLMemoryAllocator();

    // Initialize the allocator
    bool initialize(
        size_t l1_size_gb = 12,
        size_t l2_size_gb = 3,
        size_t l3_size_gb = 128
    );

    // Memory allocation API (compatible with CUDA memory allocators)
    void* cxl_malloc(size_t size_bytes, uint32_t layer_id = 0, void* hint = nullptr);
    void cxl_free(void* ptr);
    
    // Access with automatic prefetch
    void* cxl_access(void* handle, size_t offset, size_t size_bytes);
    
    // Prefetch hint API
    void prefetch_hint(const std::vector<uint32_t>& token_history, uint32_t layer_id);
    
    // Statistics
    struct AllocatorStatistics {
        size_t total_allocations;
        size_t total_deallocations;
        size_t current_allocated_bytes;
        size_t peak_allocated_bytes;
        double prefetch_hit_rate;
    };
    
    AllocatorStatistics get_statistics() const;

private:
    std::unique_ptr<CXLMemoryManager> memory_manager_;
    std::unique_ptr<SpeculativePrefetcher> prefetcher_;
    std::unique_ptr<FPGACacheEngine> cache_engine_;
    
    // Handle to virtual address mapping
    struct AllocationHandle {
        uint64_t virtual_addr;
        size_t size_bytes;
        uint32_t layer_id;
    };
    
    std::unordered_map<void*, AllocationHandle> handle_map_;
    std::mutex handle_map_mutex_;
    
    // Statistics
    mutable AllocatorStatistics stats_;
    mutable std::mutex stats_mutex_;
    
    bool initialized_;
};

} // namespace cxlspeckv

