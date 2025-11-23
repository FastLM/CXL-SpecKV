// host/src/speckv_allocator.cpp
#include "../include/speckv_allocator.hpp"
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

SpeckvAllocator::SpeckvAllocator(SpeckvDriver* driver) : driver_(driver) {
}

uint64_t SpeckvAllocator::alloc(size_t bytes) {
    uint64_t handle = next_handle_++;
    
    Allocation alloc;
    alloc.size_bytes = bytes;
    
    // 拆分成 pages (4KB each)
    const size_t page_size = 4096;
    size_t num_pages = (bytes + page_size - 1) / page_size;
    alloc.pages.reserve(num_pages);
    
    for (size_t i = 0; i < num_pages; ++i) {
        KvPageHandle page;
        page.virt_page_id = (handle << 32) | (i << 12);
        page.phys_page_id = 0x4000000000ULL + (handle << 20) + (i << 12);  // 简化映射
        page.page_size = page_size;
        page.flags = 0;
        alloc.pages.push_back(page);
        
        // 插入 page table
        KvVirtKey key;
        key.virt_page_id = page.virt_page_id;
        page_table_[key] = page;
    }
    
    allocs_[handle] = alloc;
    return handle;
}

void SpeckvAllocator::free(uint64_t handle) {
    auto it = allocs_.find(handle);
    if (it == allocs_.end()) return;
    
    // 从 page table 删除
    for (const auto& page : it->second.pages) {
        KvVirtKey key;
        key.virt_page_id = page.virt_page_id;
        page_table_.erase(key);
    }
    
    allocs_.erase(it);
}

void* SpeckvAllocator::access(uint64_t handle, uint64_t offset, size_t bytes) {
    auto it = allocs_.find(handle);
    if (it == allocs_.end()) return nullptr;
    
    const size_t page_size = 4096;
    uint64_t page_idx = offset / page_size;
    uint64_t page_offset = offset % page_size;
    
    if (page_idx >= it->second.pages.size()) return nullptr;
    
    KvPageHandle& page = it->second.pages[page_idx];
    
    // 检查是否在 L1/L2
    if (!is_in_l1_or_l2(page.virt_page_id)) {
        // 同步 fetch
        sync_fetch_page(page.virt_page_id);
    }
    
    // 返回 GPU 地址（简化：使用物理地址作为 GPU 地址）
    return reinterpret_cast<void*>(page.phys_page_id + page_offset);
}

void SpeckvAllocator::prefetch(uint32_t req_id,
                               uint16_t layer,
                               uint32_t cur_pos,
                               uint32_t depth_k,
                               const int32_t* tokens,
                               uint32_t history_len) {
    SpeckvPrefetchReq req;
    req.req_id = req_id;
    req.layer = layer;
    req.cur_pos = cur_pos;
    req.depth_k = depth_k;
    req.history_len = history_len;
    
    driver_->submit_prefetch(req, tokens);
}

uint64_t SpeckvAllocator::encode_virt_page(uint32_t req_id,
                                           uint16_t layer,
                                           uint16_t head,
                                           uint32_t pos,
                                           uint8_t  kind) {
    // 编码格式: [req_id:32][layer:16][head:8][pos:32][kind:1]
    return (static_cast<uint64_t>(req_id) << 32) |
           (static_cast<uint64_t>(layer) << 16) |
           (static_cast<uint64_t>(head) << 8) |
           (static_cast<uint64_t>(pos) << 1) |
           static_cast<uint64_t>(kind);
}

bool SpeckvAllocator::is_in_l1_or_l2(uint64_t virt_page_id) {
    KvVirtKey key;
    key.virt_page_id = virt_page_id;
    auto it = page_table_.find(key);
    if (it == page_table_.end()) return false;
    
    // 检查 flags: bit0 = L1, bit1 = L2
    return (it->second.flags & 0x3) != 0;
}

void SpeckvAllocator::sync_fetch_page(uint64_t virt_page_id) {
    KvVirtKey key;
    key.virt_page_id = virt_page_id;
    auto it = page_table_.find(key);
    if (it == page_table_.end()) return;
    
    // 构造 DMA 描述符
    SpeckvDmaDesc desc;
    desc.fpga_addr = it->second.phys_page_id;
    desc.gpu_addr = 0x8000000000ULL + (virt_page_id & 0xFFFFFFFFFFFFULL);  // GPU HBM 映射
    desc.bytes = it->second.page_size;
    desc.flags = 0;  // READ, not prefetch
    
    std::vector<SpeckvDmaDesc> batch = {desc};
    driver_->submit_dma_batch(batch);
    
    // 等待完成
    while (driver_->poll_complete() == 0) {
        // 轮询等待
    }
    
    // 标记为在 L2
    it->second.flags |= 0x2;  // L2 bit
}

