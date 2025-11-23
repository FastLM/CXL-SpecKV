// host/include/speckv_driver.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

struct SpeckvDmaDesc {
    uint64_t fpga_addr;
    uint64_t gpu_addr;
    uint32_t bytes;
    uint32_t flags;   // bit0: RD/WR, bit1: COMPRESSED, bit2: PREFETCH
};

struct SpeckvPrefetchReq {
    uint32_t req_id;
    uint16_t layer;
    uint32_t cur_pos;
    uint32_t depth_k;
    uint32_t history_len;
    // 后面紧跟 history_len 个 int32 token id
};

class SpeckvDriver {
public:
    explicit SpeckvDriver(const std::string& dev_path);
    ~SpeckvDriver();

    bool ok() const { return fd_ >= 0; }

    int submit_dma_batch(const std::vector<SpeckvDmaDesc>& batch);
    int submit_prefetch(const SpeckvPrefetchReq& req, const int32_t* tokens);
    int poll_complete();  // 轮询 DMA 完成队列

    int set_prefetch_depth(uint32_t k);
    int set_compression_scheme(uint32_t scheme);

private:
    int fd_ = -1;
};

