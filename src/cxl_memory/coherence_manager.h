#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

namespace cxlspeckv {

// Forward declarations
class SpeckvDriver;

/**
 * CoherenceManager
 * 
 * Software-side coherence manager that coordinates with the FPGA directory controller.
 * This class maintains coherence metadata on the host side and issues coherence
 * operations to the FPGA via PCIe/MMIO.
 * 
 * Architecture:
 * - FPGA acts as the home agent and maintains the authoritative directory
 * - This class maintains a shadow copy for fast lookups and batching
 * - GPU accesses are translated through FPGA to maintain coherence
 */
class CoherenceManager {
public:
    // Coherence states (matches MESI protocol on FPGA)
    enum class CoherenceState : uint8_t {
        INVALID = 0,    // Not cached or invalid
        SHARED = 1,     // Read-only, may be shared with others
        EXCLUSIVE = 2,  // Read-only, not shared
        MODIFIED = 3    // Modified, must writeback
    };
    
    // Memory tier for tracking location
    enum class MemoryTier : uint8_t {
        L1_GPU = 0,     // GPU local HBM
        L2_PREFETCH = 1,// GPU prefetch buffer
        L3_CXL = 2      // CXL memory pool
    };
    
    // Coherence operation types
    enum class CoherenceOp : uint8_t {
        READ = 0,
        WRITE = 1,
        INVALIDATE = 2,
        WRITEBACK = 3,
        FLUSH = 4
    };
    
    // Directory entry (shadow copy of FPGA directory)
    struct DirectoryEntry {
        uint64_t cache_line_addr;   // Cache line aligned address
        CoherenceState state;
        MemoryTier tier;
        uint64_t last_access_time;
        uint32_t access_count;
        bool pending_operation;
        
        DirectoryEntry() 
            : cache_line_addr(0)
            , state(CoherenceState::INVALID)
            , tier(MemoryTier::L3_CXL)
            , last_access_time(0)
            , access_count(0)
            , pending_operation(false) {}
    };
    
    // Statistics
    struct Statistics {
        uint64_t total_reads;
        uint64_t total_writes;
        uint64_t coherence_ops;
        uint64_t invalidations_sent;
        uint64_t writebacks_performed;
        uint64_t directory_hits;
        uint64_t directory_misses;
        
        double hit_rate() const {
            uint64_t total = directory_hits + directory_misses;
            return total > 0 ? (double)directory_hits / total : 0.0;
        }
    };

public:
    CoherenceManager(std::shared_ptr<SpeckvDriver> driver, size_t cache_line_size = 64);
    ~CoherenceManager();
    
    // Coherence operations
    
    /**
     * Request read access to a cache line
     * This will:
     * 1. Check local directory
     * 2. If miss, send request to FPGA home agent
     * 3. FPGA checks CXL memory and issues coherence actions if needed
     * 4. Update local directory state
     */
    bool request_read(uint64_t addr, void* data_out, size_t size);
    
    /**
     * Request write access to a cache line
     * This will:
     * 1. Check local directory
     * 2. Send write request to FPGA home agent
     * 3. FPGA sends invalidations to other sharers via CXL.cache
     * 4. FPGA writes to CXL memory
     * 5. Update local directory to MODIFIED state
     */
    bool request_write(uint64_t addr, const void* data, size_t size);
    
    /**
     * Invalidate a cache line
     * Used when data is evicted from GPU L1 or when receiving invalidation
     * from FPGA (due to other agent's write)
     */
    bool invalidate(uint64_t addr);
    
    /**
     * Writeback a modified cache line to CXL memory
     * Used during eviction or explicit flush
     */
    bool writeback(uint64_t addr, const void* data, size_t size);
    
    /**
     * Flush all modified cache lines
     * Writes back all MODIFIED state entries to CXL memory
     */
    bool flush_all();
    
    // Directory queries
    
    /**
     * Check if address is cached and in what state
     */
    CoherenceState get_state(uint64_t addr) const;
    
    /**
     * Check which tier the data is in
     */
    MemoryTier get_tier(uint64_t addr) const;
    
    /**
     * Check if address is in valid state (can be read)
     */
    bool is_valid(uint64_t addr) const;
    
    /**
     * Check if address is in modified state (needs writeback)
     */
    bool is_modified(uint64_t addr) const;
    
    // State transitions (called internally or by memory manager)
    
    /**
     * Promote data from L3 (CXL) to L1 (GPU)
     * Issues coherence protocol messages if needed
     */
    bool promote_to_l1(uint64_t addr);
    
    /**
     * Demote data from L1 (GPU) to L3 (CXL)
     * Writes back if modified
     */
    bool demote_to_l3(uint64_t addr);
    
    /**
     * Update tier location without changing coherence state
     */
    void update_tier(uint64_t addr, MemoryTier new_tier);
    
    // Batch operations for efficiency
    
    /**
     * Batch invalidate multiple addresses
     * More efficient than individual invalidations
     */
    bool batch_invalidate(const std::vector<uint64_t>& addrs);
    
    /**
     * Batch writeback multiple addresses
     */
    bool batch_writeback(const std::vector<std::pair<uint64_t, const void*>>& data);
    
    // Statistics and monitoring
    
    Statistics get_statistics() const;
    void reset_statistics();
    
    /**
     * Sync directory state from FPGA
     * Reads FPGA directory registers and updates local shadow copy
     */
    bool sync_directory_from_fpga();
    
    /**
     * Print directory state for debugging
     */
    void print_directory_state() const;

private:
    // Helper functions
    
    uint64_t align_to_cache_line(uint64_t addr) const {
        return addr & ~(cache_line_size_ - 1);
    }
    
    DirectoryEntry* get_entry(uint64_t addr);
    const DirectoryEntry* get_entry(uint64_t addr) const;
    
    DirectoryEntry* get_or_create_entry(uint64_t addr);
    
    bool send_coherence_op_to_fpga(CoherenceOp op, uint64_t addr, const void* data = nullptr, size_t size = 0);
    
    bool wait_for_fpga_completion();
    
    void update_statistics(CoherenceOp op, bool hit);
    
private:
    std::shared_ptr<SpeckvDriver> driver_;
    size_t cache_line_size_;
    
    // Shadow directory (local copy)
    std::unordered_map<uint64_t, std::unique_ptr<DirectoryEntry>> directory_;
    mutable std::mutex directory_mutex_;
    
    // Statistics
    mutable Statistics stats_;
    mutable std::mutex stats_mutex_;
    
    // Pending operations tracking
    std::atomic<uint32_t> pending_ops_;
};

} // namespace cxlspeckv
