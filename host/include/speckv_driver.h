#pragma once

#include <cstdint>
#include <string>
#include <memory>

namespace cxlspeckv {

/**
 * SpeckvDriver
 * 
 * Low-level driver for communicating with the FPGA via kernel module.
 * Wraps IOCTL calls to /dev/speckv0 device.
 */
class SpeckvDriver {
public:
    explicit SpeckvDriver(const std::string& device_path = "/dev/speckv0");
    ~SpeckvDriver();
    
    // Disable copy
    SpeckvDriver(const SpeckvDriver&) = delete;
    SpeckvDriver& operator=(const SpeckvDriver&) = delete;
    
    // Device operations
    bool open();
    void close();
    bool is_open() const { return fd_ >= 0; }
    
    // DMA operations
    struct DmaDescriptor {
        uint64_t src_addr;      // Source address
        uint64_t dst_addr;      // Destination address
        uint32_t size;          // Transfer size in bytes
        uint32_t flags;         // Flags (compress, decompress, etc.)
    };
    
    bool submit_dma_batch(const DmaDescriptor* descriptors, size_t count);
    bool poll_completion(uint32_t* completed_count);
    
    // Prefetch operations
    struct PrefetchRequest {
        uint32_t req_id;        // Request ID
        uint16_t layer;         // Layer ID
        uint32_t pos;           // Token position
        uint8_t depth_k;        // Prefetch depth
        uint32_t tokens[16];    // Recent token history
    };
    
    bool submit_prefetch(const PrefetchRequest& req);
    
    // Parameter configuration
    bool set_prefetch_depth(uint32_t depth);
    bool set_compression_scheme(uint32_t scheme);
    
    // MMIO register access (for coherence operations)
    bool write_mmio(uint32_t offset, uint64_t value);
    bool read_mmio(uint32_t offset, uint64_t* value);
    
    // Coherence-specific operations
    enum class CoherenceOp : uint32_t {
        READ = 0,
        WRITE = 1,
        INVALIDATE = 2,
        WRITEBACK = 3,
        FLUSH = 4
    };
    
    bool coherence_request(CoherenceOp op, uint64_t addr, const void* data = nullptr, size_t size = 0);
    bool coherence_wait_complete();
    
    // Statistics
    struct Statistics {
        uint32_t total_dma_ops;
        uint32_t total_prefetch_ops;
        uint32_t total_coherence_ops;
        uint64_t bytes_transferred;
    };
    
    Statistics get_statistics() const;
    void reset_statistics();

private:
    std::string device_path_;
    int fd_;
    mutable Statistics stats_;
    
    // Helper for IOCTL calls
    bool ioctl_call(unsigned long request, void* arg);
};

} // namespace cxlspeckv
