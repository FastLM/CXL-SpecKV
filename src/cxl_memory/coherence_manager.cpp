#include "coherence_manager.h"
#include "../../host/include/speckv_driver.h"
#include <cstring>
#include <iostream>
#include <chrono>

namespace cxlspeckv {

// FPGA MMIO register offsets for coherence operations
constexpr uint32_t MMIO_COHERENCE_OP_REG = 0x1000;
constexpr uint32_t MMIO_COHERENCE_ADDR_LO_REG = 0x1004;
constexpr uint32_t MMIO_COHERENCE_ADDR_HI_REG = 0x1008;
constexpr uint32_t MMIO_COHERENCE_STATUS_REG = 0x100C;
constexpr uint32_t MMIO_DIR_ENTRIES_USED_REG = 0x1010;
constexpr uint32_t MMIO_DIR_SHARED_COUNT_REG = 0x1014;
constexpr uint32_t MMIO_DIR_EXCLUSIVE_COUNT_REG = 0x1018;
constexpr uint32_t MMIO_DIR_MODIFIED_COUNT_REG = 0x101C;
constexpr uint32_t MMIO_COHERENCE_OPS_COUNT_REG = 0x1020;

CoherenceManager::CoherenceManager(std::shared_ptr<SpeckvDriver> driver, size_t cache_line_size)
    : driver_(driver)
    , cache_line_size_(cache_line_size)
    , pending_ops_(0)
{
    std::memset(&stats_, 0, sizeof(stats_));
}

CoherenceManager::~CoherenceManager() {
    // Flush all modified data before destruction
    flush_all();
}

bool CoherenceManager::request_read(uint64_t addr, void* data_out, size_t size) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    // Check local directory
    auto* entry = get_entry(cache_line_addr);
    
    if (entry && (entry->state == CoherenceState::SHARED || 
                  entry->state == CoherenceState::EXCLUSIVE ||
                  entry->state == CoherenceState::MODIFIED)) {
        // Cache hit - data is already valid
        update_statistics(CoherenceOp::READ, true);
        entry->last_access_time = std::chrono::steady_clock::now().time_since_epoch().count();
        entry->access_count++;
        
        // In real implementation, copy data from GPU/CXL memory
        // For now, just signal success
        return true;
    }
    
    // Cache miss - need to fetch from CXL memory via FPGA
    update_statistics(CoherenceOp::READ, false);
    
    // Send read request to FPGA coherence controller
    bool success = send_coherence_op_to_fpga(CoherenceOp::READ, cache_line_addr, nullptr, size);
    
    if (success) {
        // Update directory entry to SHARED state
        auto* new_entry = get_or_create_entry(cache_line_addr);
        new_entry->state = CoherenceState::SHARED;
        new_entry->tier = MemoryTier::L1_GPU;  // Data is now in GPU L1
        new_entry->last_access_time = std::chrono::steady_clock::now().time_since_epoch().count();
        new_entry->access_count = 1;
    }
    
    return success;
}

bool CoherenceManager::request_write(uint64_t addr, const void* data, size_t size) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    auto* entry = get_entry(cache_line_addr);
    
    // Check current state
    if (entry) {
        if (entry->state == CoherenceState::SHARED) {
            // Need to invalidate other sharers
            // FPGA will handle sending CXL.cache invalidations
            update_statistics(CoherenceOp::INVALIDATE, false);
            stats_.invalidations_sent++;
        }
    }
    
    update_statistics(CoherenceOp::WRITE, entry != nullptr);
    
    // Send write request to FPGA coherence controller
    // FPGA will:
    // 1. Send invalidations to other sharers via CXL.cache
    // 2. Write data to CXL memory via CXL.mem
    // 3. Update its directory to MODIFIED state
    bool success = send_coherence_op_to_fpga(CoherenceOp::WRITE, cache_line_addr, data, size);
    
    if (success) {
        // Update directory entry to MODIFIED state
        auto* new_entry = get_or_create_entry(cache_line_addr);
        new_entry->state = CoherenceState::MODIFIED;
        new_entry->tier = MemoryTier::L1_GPU;  // Data is now in GPU L1
        new_entry->last_access_time = std::chrono::steady_clock::now().time_since_epoch().count();
        new_entry->access_count++;
    }
    
    return success;
}

bool CoherenceManager::invalidate(uint64_t addr) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    auto* entry = get_entry(cache_line_addr);
    if (!entry) {
        return true;  // Already invalid
    }
    
    // If modified, need to writeback first
    if (entry->state == CoherenceState::MODIFIED) {
        // In real implementation, writeback data
        stats_.writebacks_performed++;
    }
    
    // Mark as invalid
    entry->state = CoherenceState::INVALID;
    
    // Send invalidation to FPGA
    bool success = send_coherence_op_to_fpga(CoherenceOp::INVALIDATE, cache_line_addr);
    
    stats_.invalidations_sent++;
    
    return success;
}

bool CoherenceManager::writeback(uint64_t addr, const void* data, size_t size) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    auto* entry = get_entry(cache_line_addr);
    if (!entry || entry->state != CoherenceState::MODIFIED) {
        return true;  // Nothing to writeback
    }
    
    // Send writeback to FPGA
    bool success = send_coherence_op_to_fpga(CoherenceOp::WRITEBACK, cache_line_addr, data, size);
    
    if (success) {
        // Transition to SHARED or EXCLUSIVE state (data is clean now)
        entry->state = CoherenceState::SHARED;
        entry->tier = MemoryTier::L3_CXL;  // Data is written back to CXL
        stats_.writebacks_performed++;
    }
    
    return success;
}

bool CoherenceManager::flush_all() {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    std::cout << "CoherenceManager: Flushing all modified cache lines..." << std::endl;
    
    size_t flushed = 0;
    for (auto& [addr, entry] : directory_) {
        if (entry->state == CoherenceState::MODIFIED) {
            // In real implementation, writeback data
            send_coherence_op_to_fpga(CoherenceOp::WRITEBACK, addr);
            entry->state = CoherenceState::SHARED;
            entry->tier = MemoryTier::L3_CXL;
            flushed++;
        }
    }
    
    std::cout << "CoherenceManager: Flushed " << flushed << " cache lines" << std::endl;
    stats_.writebacks_performed += flushed;
    
    return true;
}

CoherenceManager::CoherenceState CoherenceManager::get_state(uint64_t addr) const {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    const auto* entry = get_entry(cache_line_addr);
    return entry ? entry->state : CoherenceState::INVALID;
}

CoherenceManager::MemoryTier CoherenceManager::get_tier(uint64_t addr) const {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    const auto* entry = get_entry(cache_line_addr);
    return entry ? entry->tier : MemoryTier::L3_CXL;
}

bool CoherenceManager::is_valid(uint64_t addr) const {
    CoherenceState state = get_state(addr);
    return state != CoherenceState::INVALID;
}

bool CoherenceManager::is_modified(uint64_t addr) const {
    return get_state(addr) == CoherenceState::MODIFIED;
}

bool CoherenceManager::promote_to_l1(uint64_t addr) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    auto* entry = get_or_create_entry(cache_line_addr);
    
    if (entry->tier == MemoryTier::L1_GPU) {
        return true;  // Already in L1
    }
    
    // Promote from L3 to L1
    // This involves:
    // 1. Reading data from CXL memory via FPGA
    // 2. Copying to GPU HBM
    // 3. Updating directory
    
    bool success = send_coherence_op_to_fpga(CoherenceOp::READ, cache_line_addr);
    
    if (success) {
        entry->tier = MemoryTier::L1_GPU;
        // State remains the same (SHARED/EXCLUSIVE/MODIFIED)
    }
    
    return success;
}

bool CoherenceManager::demote_to_l3(uint64_t addr) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    auto* entry = get_entry(cache_line_addr);
    if (!entry || entry->tier == MemoryTier::L3_CXL) {
        return true;  // Already in L3 or invalid
    }
    
    // If modified, writeback first
    if (entry->state == CoherenceState::MODIFIED) {
        send_coherence_op_to_fpga(CoherenceOp::WRITEBACK, cache_line_addr);
        entry->state = CoherenceState::SHARED;
        stats_.writebacks_performed++;
    }
    
    entry->tier = MemoryTier::L3_CXL;
    
    return true;
}

void CoherenceManager::update_tier(uint64_t addr, MemoryTier new_tier) {
    uint64_t cache_line_addr = align_to_cache_line(addr);
    
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    auto* entry = get_or_create_entry(cache_line_addr);
    entry->tier = new_tier;
}

bool CoherenceManager::batch_invalidate(const std::vector<uint64_t>& addrs) {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    bool all_success = true;
    for (uint64_t addr : addrs) {
        uint64_t cache_line_addr = align_to_cache_line(addr);
        auto* entry = get_entry(cache_line_addr);
        if (entry) {
            entry->state = CoherenceState::INVALID;
            // In real implementation, batch these MMIO writes
            all_success &= send_coherence_op_to_fpga(CoherenceOp::INVALIDATE, cache_line_addr);
        }
    }
    
    stats_.invalidations_sent += addrs.size();
    
    return all_success;
}

bool CoherenceManager::batch_writeback(const std::vector<std::pair<uint64_t, const void*>>& data) {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    bool all_success = true;
    for (const auto& [addr, ptr] : data) {
        uint64_t cache_line_addr = align_to_cache_line(addr);
        auto* entry = get_entry(cache_line_addr);
        if (entry && entry->state == CoherenceState::MODIFIED) {
            // In real implementation, batch these operations
            all_success &= send_coherence_op_to_fpga(CoherenceOp::WRITEBACK, cache_line_addr, ptr, cache_line_size_);
            entry->state = CoherenceState::SHARED;
            entry->tier = MemoryTier::L3_CXL;
        }
    }
    
    stats_.writebacks_performed += data.size();
    
    return all_success;
}

CoherenceManager::Statistics CoherenceManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void CoherenceManager::reset_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    std::memset(&stats_, 0, sizeof(stats_));
}

bool CoherenceManager::sync_directory_from_fpga() {
    if (!driver_) {
        return false;
    }
    
    // Read FPGA directory statistics via MMIO
    // In real implementation, use driver_->read_mmio()
    
    std::cout << "CoherenceManager: Syncing directory state from FPGA..." << std::endl;
    
    // This would read FPGA registers and update local shadow copy
    // For now, just a placeholder
    
    return true;
}

void CoherenceManager::print_directory_state() const {
    std::lock_guard<std::mutex> lock(directory_mutex_);
    
    std::cout << "\n=== Coherence Directory State ===" << std::endl;
    std::cout << "Total entries: " << directory_.size() << std::endl;
    
    size_t invalid_count = 0, shared_count = 0, exclusive_count = 0, modified_count = 0;
    size_t l1_count = 0, l2_count = 0, l3_count = 0;
    
    for (const auto& [addr, entry] : directory_) {
        switch (entry->state) {
            case CoherenceState::INVALID: invalid_count++; break;
            case CoherenceState::SHARED: shared_count++; break;
            case CoherenceState::EXCLUSIVE: exclusive_count++; break;
            case CoherenceState::MODIFIED: modified_count++; break;
        }
        
        switch (entry->tier) {
            case MemoryTier::L1_GPU: l1_count++; break;
            case MemoryTier::L2_PREFETCH: l2_count++; break;
            case MemoryTier::L3_CXL: l3_count++; break;
        }
    }
    
    std::cout << "States: I=" << invalid_count << ", S=" << shared_count 
              << ", E=" << exclusive_count << ", M=" << modified_count << std::endl;
    std::cout << "Tiers: L1=" << l1_count << ", L2=" << l2_count << ", L3=" << l3_count << std::endl;
    
    auto stats = get_statistics();
    std::cout << "\nStatistics:" << std::endl;
    std::cout << "  Reads: " << stats.total_reads << std::endl;
    std::cout << "  Writes: " << stats.total_writes << std::endl;
    std::cout << "  Coherence ops: " << stats.coherence_ops << std::endl;
    std::cout << "  Invalidations: " << stats.invalidations_sent << std::endl;
    std::cout << "  Writebacks: " << stats.writebacks_performed << std::endl;
    std::cout << "  Directory hit rate: " << (stats.hit_rate() * 100.0) << "%" << std::endl;
    std::cout << "================================\n" << std::endl;
}

// Private helper functions

CoherenceManager::DirectoryEntry* CoherenceManager::get_entry(uint64_t addr) {
    auto it = directory_.find(addr);
    return it != directory_.end() ? it->second.get() : nullptr;
}

const CoherenceManager::DirectoryEntry* CoherenceManager::get_entry(uint64_t addr) const {
    auto it = directory_.find(addr);
    return it != directory_.end() ? it->second.get() : nullptr;
}

CoherenceManager::DirectoryEntry* CoherenceManager::get_or_create_entry(uint64_t addr) {
    auto it = directory_.find(addr);
    if (it != directory_.end()) {
        return it->second.get();
    }
    
    auto entry = std::make_unique<DirectoryEntry>();
    entry->cache_line_addr = addr;
    auto* ptr = entry.get();
    directory_[addr] = std::move(entry);
    return ptr;
}

bool CoherenceManager::send_coherence_op_to_fpga(CoherenceOp op, uint64_t addr, const void* data, size_t size) {
    if (!driver_) {
        return false;
    }
    
    // In real implementation, this would:
    // 1. Write operation type to MMIO_COHERENCE_OP_REG
    // 2. Write address to MMIO_COHERENCE_ADDR registers
    // 3. If write/writeback, write data via DMA
    // 4. Poll MMIO_COHERENCE_STATUS_REG for completion
    
    // For now, simulate the operation
    pending_ops_++;
    
    // Simulate FPGA processing time (would be actual MMIO in real implementation)
    // driver_->write_mmio(MMIO_COHERENCE_OP_REG, static_cast<uint32_t>(op));
    // driver_->write_mmio(MMIO_COHERENCE_ADDR_LO_REG, addr & 0xFFFFFFFF);
    // driver_->write_mmio(MMIO_COHERENCE_ADDR_HI_REG, addr >> 32);
    
    bool success = wait_for_fpga_completion();
    
    pending_ops_--;
    
    update_statistics(op, true);
    
    return success;
}

bool CoherenceManager::wait_for_fpga_completion() {
    // In real implementation:
    // while (driver_->read_mmio(MMIO_COHERENCE_STATUS_REG) & 0x1) {
    //     // Wait for operation complete bit
    // }
    
    // For now, assume immediate completion
    return true;
}

void CoherenceManager::update_statistics(CoherenceOp op, bool hit) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    switch (op) {
        case CoherenceOp::READ:
            stats_.total_reads++;
            if (hit) stats_.directory_hits++;
            else stats_.directory_misses++;
            break;
            
        case CoherenceOp::WRITE:
            stats_.total_writes++;
            if (hit) stats_.directory_hits++;
            else stats_.directory_misses++;
            break;
            
        case CoherenceOp::INVALIDATE:
        case CoherenceOp::WRITEBACK:
        case CoherenceOp::FLUSH:
            stats_.coherence_ops++;
            break;
    }
}

} // namespace cxlspeckv
