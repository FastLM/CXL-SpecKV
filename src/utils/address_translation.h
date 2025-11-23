#pragma once

#include <cstdint>

namespace cxlspeckv {

// Address Translation Unit (ATU) for virtual to physical address translation
class AddressTranslationUnit {
public:
    AddressTranslationUnit(size_t tlb_size = 1024);
    ~AddressTranslationUnit();
    
    // Translate virtual address to physical address
    uint64_t translate(uint64_t virtual_addr);
    
    // Invalidate TLB entry
    void invalidate(uint64_t virtual_addr);
    
    // Invalidate all TLB entries
    void invalidate_all();
    
    // Get TLB statistics
    struct TLBStatistics {
        size_t hits;
        size_t misses;
        double hit_rate;
    };
    
    TLBStatistics get_statistics() const;
    void reset_statistics();

private:
    struct TLBEntry {
        uint64_t virtual_page;
        uint64_t physical_page;
        bool valid;
    };
    
    std::vector<TLBEntry> tlb_;
    size_t tlb_size_;
    
    mutable TLBStatistics stats_;
    mutable std::mutex tlb_mutex_;
    
    // Page walk (simplified)
    uint64_t page_walk(uint64_t virtual_addr);
};

} // namespace cxlspeckv

