#include "cxl_memory_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace cxlspeckv {

CXLMemoryManager::CXLMemoryManager(
    size_t l1_size_gb,
    size_t l2_size_gb,
    size_t l3_size_gb,
    size_t page_size
) : l1_size_bytes_(l1_size_gb * 1024ULL * 1024ULL * 1024ULL),
    l2_size_bytes_(l2_size_gb * 1024ULL * 1024ULL * 1024ULL),
    l3_size_bytes_(l3_size_gb * 1024ULL * 1024ULL * 1024ULL),
    page_size_(page_size),
    next_virtual_addr_(0x100000000ULL),  // Start at 4GB
    next_physical_addr_l1_(0x8000000000ULL),  // 512GB base
    next_physical_addr_l2_(0x10000000000ULL),  // 1TB base
    next_physical_addr_l3_(0x20000000000ULL)  // 2TB base
{
    reset_statistics();
}

CXLMemoryManager::~CXLMemoryManager() = default;

uint64_t CXLMemoryManager::allocate(size_t size_bytes, uint32_t layer_id, MemoryTier preferred_tier) {
    std::lock_guard<std::mutex> alloc_lock(allocation_mutex_);
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    size_t num_pages = (size_bytes + page_size_ - 1) / page_size_;
    size_t required_bytes = num_pages * page_size_;
    
    // Determine actual tier based on availability
    MemoryTier actual_tier = preferred_tier;
    if (preferred_tier == MemoryTier::L1_GPU_LOCAL && !can_fit_in_tier(MemoryTier::L1_GPU_LOCAL, required_bytes)) {
        actual_tier = MemoryTier::L3_CXL_POOL;
    }
    
    uint64_t virtual_addr = next_virtual_addr_;
    uint64_t physical_addr_base;
    
    switch (actual_tier) {
        case MemoryTier::L1_GPU_LOCAL:
            physical_addr_base = next_physical_addr_l1_;
            next_physical_addr_l1_ += required_bytes;
            l1_pages_.push_back(virtual_addr);
            break;
        case MemoryTier::L2_PREFETCH:
            physical_addr_base = next_physical_addr_l2_;
            next_physical_addr_l2_ += required_bytes;
            l2_pages_.push_back(virtual_addr);
            break;
        case MemoryTier::L3_CXL_POOL:
            physical_addr_base = next_physical_addr_l3_;
            next_physical_addr_l3_ += required_bytes;
            l3_pages_.push_back(virtual_addr);
            break;
    }
    
    // Create page entries
    for (size_t i = 0; i < num_pages; ++i) {
        auto page = std::make_unique<MemoryPage>();
        page->virtual_addr = virtual_addr + i * page_size_;
        page->physical_addr = physical_addr_base + i * page_size_;
        page->tier = actual_tier;
        page->state = PageState::EXCLUSIVE;
        page->access_count = 0;
        page->last_access_time = std::chrono::steady_clock::now().time_since_epoch().count();
        page->is_hot = false;
        page->layer_id = layer_id;
        
        page_table_[page->virtual_addr] = std::move(page);
    }
    
    next_virtual_addr_ += required_bytes;
    return virtual_addr;
}

void CXLMemoryManager::deallocate(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    auto it = page_table_.find(virtual_addr);
    if (it != page_table_.end()) {
        MemoryTier tier = it->second->tier;
        
        // Remove from tier-specific lists
        switch (tier) {
            case MemoryTier::L1_GPU_LOCAL:
                l1_pages_.erase(std::remove(l1_pages_.begin(), l1_pages_.end(), virtual_addr), l1_pages_.end());
                l1_lru_list_.erase(std::remove(l1_lru_list_.begin(), l1_lru_list_.end(), virtual_addr), l1_lru_list_.end());
                break;
            case MemoryTier::L2_PREFETCH:
                l2_pages_.erase(std::remove(l2_pages_.begin(), l2_pages_.end(), virtual_addr), l2_pages_.end());
                break;
            case MemoryTier::L3_CXL_POOL:
                l3_pages_.erase(std::remove(l3_pages_.begin(), l3_pages_.end(), virtual_addr), l3_pages_.end());
                break;
        }
        
        page_table_.erase(it);
    }
}

uint64_t CXLMemoryManager::translate_virtual_to_physical(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    // Align to page boundary
    uint64_t page_addr = (virtual_addr / page_size_) * page_size_;
    auto it = page_table_.find(page_addr);
    if (it != page_table_.end()) {
        uint64_t offset = virtual_addr - page_addr;
        return it->second->physical_addr + offset;
    }
    return 0;  // Invalid address
}

bool CXLMemoryManager::is_in_cache(uint64_t virtual_addr, MemoryTier tier) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    uint64_t page_addr = (virtual_addr / page_size_) * page_size_;
    auto it = page_table_.find(page_addr);
    if (it != page_table_.end()) {
        return it->second->tier == tier;
    }
    return false;
}

bool CXLMemoryManager::promote_to_l1(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    if (!page || page->tier == MemoryTier::L1_GPU_LOCAL) {
        return false;
    }
    
    // Check if L1 has space, evict if needed
    size_t page_size = page_size_;
    if (!can_fit_in_tier(MemoryTier::L1_GPU_LOCAL, page_size)) {
        evict_l1_lru();
    }
    
    // Update tier
    MemoryTier old_tier = page->tier;
    page->tier = MemoryTier::L1_GPU_LOCAL;
    
    // Update lists
    switch (old_tier) {
        case MemoryTier::L2_PREFETCH:
            l2_pages_.erase(std::remove(l2_pages_.begin(), l2_pages_.end(), virtual_addr), l2_pages_.end());
            break;
        case MemoryTier::L3_CXL_POOL:
            l3_pages_.erase(std::remove(l3_pages_.begin(), l3_pages_.end(), virtual_addr), l3_pages_.end());
            stats_.migrations_l3_to_l1++;
            break;
        default:
            break;
    }
    
    l1_pages_.push_back(virtual_addr);
    update_lru(virtual_addr);
    
    return true;
}

bool CXLMemoryManager::demote_to_l3(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    if (!page || page->tier == MemoryTier::L3_CXL_POOL) {
        return false;
    }
    
    MemoryTier old_tier = page->tier;
    page->tier = MemoryTier::L3_CXL_POOL;
    
    // Update lists
    switch (old_tier) {
        case MemoryTier::L1_GPU_LOCAL:
            l1_pages_.erase(std::remove(l1_pages_.begin(), l1_pages_.end(), virtual_addr), l1_pages_.end());
            l1_lru_list_.erase(std::remove(l1_lru_list_.begin(), l1_lru_list_.end(), virtual_addr), l1_lru_list_.end());
            stats_.migrations_l1_to_l3++;
            break;
        case MemoryTier::L2_PREFETCH:
            l2_pages_.erase(std::remove(l2_pages_.begin(), l2_pages_.end(), virtual_addr), l2_pages_.end());
            break;
        default:
            break;
    }
    
    l3_pages_.push_back(virtual_addr);
    return true;
}

void CXLMemoryManager::invalidate_page(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    if (page) {
        page->state = PageState::INVALID;
    }
}

void CXLMemoryManager::mark_modified(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    if (page) {
        page->state = PageState::MODIFIED;
    }
}

PageState CXLMemoryManager::get_page_state(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    return page ? page->state : PageState::INVALID;
}

void CXLMemoryManager::update_access_tracking(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    if (page) {
        page->access_count++;
        page->last_access_time = std::chrono::steady_clock::now().time_since_epoch().count();
        
        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            if (page->tier == MemoryTier::L1_GPU_LOCAL) {
                stats_.l1_hits++;
            } else if (page->tier == MemoryTier::L2_PREFETCH) {
                stats_.l2_hits++;
            } else {
                stats_.l3_accesses++;
            }
        }
        
        update_lru(virtual_addr);
    }
}

bool CXLMemoryManager::is_hot_page(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> page_lock(page_table_mutex_);
    
    MemoryPage* page = get_page(virtual_addr);
    if (page) {
        // Classify as hot if accessed in last N tokens (simplified: high access count)
        page->is_hot = (page->access_count > 10);
        return page->is_hot;
    }
    return false;
}

CXLMemoryManager::Statistics CXLMemoryManager::get_statistics() const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    Statistics stats = stats_;
    size_t total_l1 = stats.l1_hits + stats.l1_misses;
    size_t total_l2 = stats.l2_hits + stats.l2_misses;
    
    if (total_l1 > 0) {
        stats.l1_hit_rate = static_cast<double>(stats.l1_hits) / total_l1;
    }
    if (total_l2 > 0) {
        stats.l2_hit_rate = static_cast<double>(stats.l2_hits) / total_l2;
    }
    
    return stats;
}

void CXLMemoryManager::reset_statistics() {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_ = Statistics{};
}

MemoryPage* CXLMemoryManager::get_page(uint64_t virtual_addr) {
    uint64_t page_addr = (virtual_addr / page_size_) * page_size_;
    auto it = page_table_.find(page_addr);
    return (it != page_table_.end()) ? it->second.get() : nullptr;
}

void CXLMemoryManager::evict_l1_lru() {
    if (l1_lru_list_.empty()) {
        return;
    }
    
    uint64_t lru_addr = l1_lru_list_.front();
    l1_lru_list_.erase(l1_lru_list_.begin());
    demote_to_l3(lru_addr);
}

bool CXLMemoryManager::can_fit_in_tier(MemoryTier tier, size_t size_bytes) {
    size_t available;
    size_t used;
    
    switch (tier) {
        case MemoryTier::L1_GPU_LOCAL:
            used = l1_pages_.size() * page_size_;
            available = l1_size_bytes_;
            break;
        case MemoryTier::L2_PREFETCH:
            used = l2_pages_.size() * page_size_;
            available = l2_size_bytes_;
            break;
        case MemoryTier::L3_CXL_POOL:
            used = l3_pages_.size() * page_size_;
            available = l3_size_bytes_;
            break;
    }
    
    return (used + size_bytes) <= available;
}

void CXLMemoryManager::update_lru(uint64_t virtual_addr) {
    // Remove from LRU list if present
    l1_lru_list_.erase(std::remove(l1_lru_list_.begin(), l1_lru_list_.end(), virtual_addr), l1_lru_list_.end());
    // Add to end (most recently used)
    l1_lru_list_.push_back(virtual_addr);
}

} // namespace cxlspeckv

