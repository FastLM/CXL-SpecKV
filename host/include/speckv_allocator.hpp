// host/include/speckv_allocator.hpp
#pragma once

#include "speckv_driver.hpp"
#include <unordered_map>
#include <cstdint>
#include <optional>

struct KvVirtKey {
    uint64_t virt_page_id;  // 高位编码 (req_id, layer, pos, kind)
    bool operator==(const KvVirtKey& o) const { 
        return virt_page_id == o.virt_page_id; 
    }
};

struct KvVirtKeyHash {
    size_t operator()(const KvVirtKey& k) const {
        return std::hash<uint64_t>{}(k.virt_page_id);
    }
};

struct KvPageHandle {
    uint64_t virt_page_id;
    uint64_t phys_page_id;  // FPGA HBM 内的 page index
    uint32_t page_size;
    uint32_t flags;  // bit0: in_L1, bit1: in_L2, bit2: compressed
};

class SpeckvAllocator {
public:
    explicit SpeckvAllocator(SpeckvDriver* driver);

    // 分配一整块 KV 区（包含所有层/heads/pos）
    uint64_t alloc(size_t bytes);
    void     free(uint64_t handle);

    void*    access(uint64_t handle, uint64_t offset, size_t bytes);

    void prefetch(uint32_t req_id,
                  uint16_t layer,
                  uint32_t cur_pos,
                  uint32_t depth_k,
                  const int32_t* tokens,
                  uint32_t history_len);

private:
    SpeckvDriver* driver_;

    struct Allocation {
        size_t size_bytes;
        // 逻辑上可以拆成多个 page，对应多条 KvPageHandle
        std::vector<KvPageHandle> pages;
    };
    std::unordered_map<uint64_t, Allocation> allocs_;
    std::unordered_map<KvVirtKey, KvPageHandle, KvVirtKeyHash> page_table_;

    uint64_t next_handle_ = 1;

    uint64_t encode_virt_page(uint32_t req_id,
                              uint16_t layer,
                              uint16_t head,
                              uint32_t pos,
                              uint8_t  kind);
    
    bool is_in_l1_or_l2(uint64_t virt_page_id);
    void sync_fetch_page(uint64_t virt_page_id);
};

