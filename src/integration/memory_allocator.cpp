#include "memory_allocator.h"
#include "../cxl_memory/cxl_memory_manager.h"
#include "../prefetcher/speculative_prefetcher.h"
#include "../fpga_engine/cache_engine.h"
#include <cstring>
#include <algorithm>

namespace cxlspeckv {

CXLMemoryAllocator::CXLMemoryAllocator() : initialized_(false) {
    stats_ = AllocatorStatistics{};
}

CXLMemoryAllocator::~CXLMemoryAllocator() = default;

bool CXLMemoryAllocator::initialize(
    size_t l1_size_gb,
    size_t l2_size_gb,
    size_t l3_size_gb
) {
    try {
        // Initialize CXL Memory Manager
        memory_manager_ = std::make_unique<CXLMemoryManager>(
            l1_size_gb, l2_size_gb, l3_size_gb
        );
        
        // Initialize Speculative Prefetcher
        prefetcher_ = std::make_unique<SpeculativePrefetcher>(
            memory_manager_.get(), 4, 16
        );
        
        // Initialize FPGA Cache Engine
        cache_engine_ = std::make_unique<FPGACacheEngine>(
            1, 800.0, 512, 16
        );
        
        initialized_ = true;
        return true;
    } catch (...) {
        initialized_ = false;
        return false;
    }
}

void* CXLMemoryAllocator::cxl_malloc(size_t size_bytes, uint32_t layer_id, void* hint) {
    if (!initialized_) {
        return nullptr;
    }
    
    // Allocate memory through CXL Memory Manager
    uint64_t virtual_addr = memory_manager_->allocate(size_bytes, layer_id);
    
    if (virtual_addr == 0) {
        return nullptr;  // Allocation failed
    }
    
    // Create handle
    AllocationHandle handle;
    handle.virtual_addr = virtual_addr;
    handle.size_bytes = size_bytes;
    handle.layer_id = layer_id;
    
    // Use virtual address as pointer (in real implementation, would map to GPU-accessible memory)
    void* ptr = reinterpret_cast<void*>(virtual_addr);
    
    {
        std::lock_guard<std::mutex> lock(handle_map_mutex_);
        handle_map_[ptr] = handle;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_allocations++;
        stats_.current_allocated_bytes += size_bytes;
        if (stats_.current_allocated_bytes > stats_.peak_allocated_bytes) {
            stats_.peak_allocated_bytes = stats_.current_allocated_bytes;
        }
    }
    
    return ptr;
}

void CXLMemoryAllocator::cxl_free(void* ptr) {
    if (!initialized_ || ptr == nullptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(handle_map_mutex_);
    
    auto it = handle_map_.find(ptr);
    if (it != handle_map_.end()) {
        AllocationHandle& handle = it->second;
        
        // Deallocate through CXL Memory Manager
        memory_manager_->deallocate(handle.virtual_addr);
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_deallocations++;
            stats_.current_allocated_bytes -= handle.size_bytes;
        }
        
        handle_map_.erase(it);
    }
}

void* CXLMemoryAllocator::cxl_access(void* handle, size_t offset, size_t size_bytes) {
    if (!initialized_ || handle == nullptr) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(handle_map_mutex_);
    
    auto it = handle_map_.find(handle);
    if (it == handle_map_.end()) {
        return nullptr;
    }
    
    AllocationHandle& alloc = it->second;
    uint64_t virtual_addr = alloc.virtual_addr + offset;
    
    // Update access tracking
    memory_manager_->update_access_tracking(virtual_addr);
    
    // Check if in L1 cache
    if (memory_manager_->is_in_cache(virtual_addr, MemoryTier::L1_GPU_LOCAL)) {
        // Already in L1, return directly
        return reinterpret_cast<void*>(virtual_addr);
    }
    
    // Check if in L2 prefetch buffer
    if (memory_manager_->is_in_cache(virtual_addr, MemoryTier::L2_PREFETCH)) {
        // Promote to L1 if hot
        if (memory_manager_->is_hot_page(virtual_addr)) {
            memory_manager_->promote_to_l1(virtual_addr);
        }
        return reinterpret_cast<void*>(virtual_addr);
    }
    
    // Not in cache - would trigger synchronous fetch in real implementation
    // For now, promote from L3 to L1
    memory_manager_->promote_to_l1(virtual_addr);
    
    return reinterpret_cast<void*>(virtual_addr);
}

void CXLMemoryAllocator::prefetch_hint(const std::vector<uint32_t>& token_history, uint32_t layer_id) {
    if (!initialized_ || !prefetcher_) {
        return;
    }
    
    // Issue speculative prefetch
    auto prefetch_requests = prefetcher_->prefetch(token_history, layer_id);
    
    // Update statistics
    auto prefetch_stats = prefetcher_->get_statistics();
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.prefetch_hit_rate = prefetch_stats.hit_rate;
    }
}

CXLMemoryAllocator::AllocatorStatistics CXLMemoryAllocator::get_statistics() const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    return stats_;
}

} // namespace cxlspeckv

