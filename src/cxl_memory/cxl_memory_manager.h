#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

namespace cxlspeckv {

// Memory tier definitions
enum class MemoryTier {
    L1_GPU_LOCAL = 0,    // 8-16GB GPU local cache
    L2_PREFETCH = 1,     // 2-4GB prefetch buffer
    L3_CXL_POOL = 2      // 64-256GB CXL memory pool
};

// Page state for cache coherence
enum class PageState {
    INVALID,
    SHARED,
    EXCLUSIVE,
    MODIFIED
};

// Memory page structure (4KB to match CXL transaction size)
struct MemoryPage {
    uint64_t virtual_addr;
    uint64_t physical_addr;
    MemoryTier tier;
    PageState state;
    uint32_t access_count;
    uint64_t last_access_time;
    bool is_hot;
    uint32_t layer_id;
};

// CXL Memory Manager
class CXLMemoryManager {
public:
    CXLMemoryManager(
        size_t l1_size_gb = 12,
        size_t l2_size_gb = 3,
        size_t l3_size_gb = 128,
        size_t page_size = 4096
    );
    
    ~CXLMemoryManager();

    // Memory allocation
    uint64_t allocate(size_t size_bytes, uint32_t layer_id, MemoryTier preferred_tier = MemoryTier::L3_CXL_POOL);
    void deallocate(uint64_t virtual_addr);
    
    // Address translation
    uint64_t translate_virtual_to_physical(uint64_t virtual_addr);
    bool is_in_cache(uint64_t virtual_addr, MemoryTier tier);
    
    // Page migration
    bool promote_to_l1(uint64_t virtual_addr);
    bool demote_to_l3(uint64_t virtual_addr);
    
    // Cache coherence operations
    void invalidate_page(uint64_t virtual_addr);
    void mark_modified(uint64_t virtual_addr);
    PageState get_page_state(uint64_t virtual_addr);
    
    // Hot-cold classification
    void update_access_tracking(uint64_t virtual_addr);
    bool is_hot_page(uint64_t virtual_addr);
    
    // Statistics
    struct Statistics {
        size_t l1_hits;
        size_t l1_misses;
        size_t l2_hits;
        size_t l2_misses;
        size_t l3_accesses;
        size_t migrations_l1_to_l3;
        size_t migrations_l3_to_l1;
        double l1_hit_rate;
        double l2_hit_rate;
    };
    
    Statistics get_statistics() const;
    void reset_statistics();

private:
    // Memory pools
    size_t l1_size_bytes_;
    size_t l2_size_bytes_;
    size_t l3_size_bytes_;
    size_t page_size_;
    
    // Memory tracking
    std::unordered_map<uint64_t, std::unique_ptr<MemoryPage>> page_table_;
    std::vector<uint64_t> l1_pages_;
    std::vector<uint64_t> l2_pages_;
    std::vector<uint64_t> l3_pages_;
    
    // Allocation tracking
    uint64_t next_virtual_addr_;
    uint64_t next_physical_addr_l1_;
    uint64_t next_physical_addr_l2_;
    uint64_t next_physical_addr_l3_;
    
    // LRU tracking for L1
    std::vector<uint64_t> l1_lru_list_;
    
    // Statistics
    mutable Statistics stats_;
    mutable std::mutex stats_mutex_;
    
    // Thread safety
    std::mutex page_table_mutex_;
    std::mutex allocation_mutex_;
    
    // Helper functions
    MemoryPage* get_page(uint64_t virtual_addr);
    void evict_l1_lru();
    bool can_fit_in_tier(MemoryTier tier, size_t size_bytes);
    void update_lru(uint64_t virtual_addr);
};

} // namespace cxlspeckv

