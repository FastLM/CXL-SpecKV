#include "address_translation.h"
#include <mutex>
#include <algorithm>

namespace cxlspeckv {

AddressTranslationUnit::AddressTranslationUnit(size_t tlb_size)
    : tlb_size_(tlb_size)
{
    tlb_.resize(tlb_size_);
    for (auto& entry : tlb_) {
        entry.valid = false;
    }
    reset_statistics();
}

AddressTranslationUnit::~AddressTranslationUnit() = default;

uint64_t AddressTranslationUnit::translate(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> lock(tlb_mutex_);
    
    // Align to 4KB page
    uint64_t virtual_page = virtual_addr & ~0xFFFULL;
    size_t page_offset = virtual_addr & 0xFFFULL;
    
    // TLB lookup
    size_t tlb_index = (virtual_page >> 12) % tlb_size_;
    TLBEntry& entry = tlb_[tlb_index];
    
    if (entry.valid && entry.virtual_page == virtual_page) {
        // TLB hit
        stats_.hits++;
        return entry.physical_page + page_offset;
    }
    
    // TLB miss - perform page walk
    stats_.misses++;
    uint64_t physical_page = page_walk(virtual_addr);
    
    // Update TLB
    entry.virtual_page = virtual_page;
    entry.physical_page = physical_page & ~0xFFFULL;
    entry.valid = true;
    
    return physical_page + page_offset;
}

void AddressTranslationUnit::invalidate(uint64_t virtual_addr) {
    std::lock_guard<std::mutex> lock(tlb_mutex_);
    
    uint64_t virtual_page = virtual_addr & ~0xFFFULL;
    size_t tlb_index = (virtual_page >> 12) % tlb_size_;
    TLBEntry& entry = tlb_[tlb_index];
    
    if (entry.virtual_page == virtual_page) {
        entry.valid = false;
    }
}

void AddressTranslationUnit::invalidate_all() {
    std::lock_guard<std::mutex> lock(tlb_mutex_);
    
    for (auto& entry : tlb_) {
        entry.valid = false;
    }
}

AddressTranslationUnit::TLBStatistics AddressTranslationUnit::get_statistics() const {
    std::lock_guard<std::mutex> lock(tlb_mutex_);
    
    TLBStatistics stats = stats_;
    size_t total = stats.hits + stats.misses;
    if (total > 0) {
        stats.hit_rate = static_cast<double>(stats.hits) / total;
    }
    
    return stats;
}

void AddressTranslationUnit::reset_statistics() {
    std::lock_guard<std::mutex> lock(tlb_mutex_);
    stats_ = TLBStatistics{};
}

uint64_t AddressTranslationUnit::page_walk(uint64_t virtual_addr) {
    // Simplified page walk
    // In real implementation, would walk page tables
    // For now, use direct mapping with offset
    return 0x4000000000ULL + (virtual_addr & 0xFFFFFFFFFFFFULL);
}

} // namespace cxlspeckv

