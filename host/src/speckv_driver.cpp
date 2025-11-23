// host/src/speckv_driver.cpp
#include "../include/speckv_driver.hpp"
#include "../../driver/uapi/speckv_ioctl.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <stdexcept>
#include <errno.h>

SpeckvDriver::SpeckvDriver(const std::string& dev_path) {
    fd_ = open(dev_path.c_str(), O_RDWR);
    if (fd_ < 0) {
        throw std::runtime_error("Failed to open " + dev_path + ": " + strerror(errno));
    }
}

SpeckvDriver::~SpeckvDriver() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

int SpeckvDriver::submit_dma_batch(const std::vector<SpeckvDmaDesc>& batch) {
    if (!ok()) return -1;
    if (batch.empty()) return 0;

    // 转换为内核格式
    std::vector<struct speckv_ioctl_dma_desc> descs;
    descs.reserve(batch.size());
    for (const auto& d : batch) {
        struct speckv_ioctl_dma_desc desc;
        desc.fpga_addr = d.fpga_addr;
        desc.gpu_addr = d.gpu_addr;
        desc.bytes = d.bytes;
        desc.flags = d.flags;
        descs.push_back(desc);
    }

    struct speckv_ioctl_dma_batch ioctl_batch;
    ioctl_batch.user_ptr = reinterpret_cast<uint64_t>(descs.data());
    ioctl_batch.count = descs.size();
    ioctl_batch.reserved = 0;

    int ret = ioctl(fd_, SPECKV_IOCTL_DMA_BATCH, &ioctl_batch);
    return (ret < 0) ? ret : 0;
}

int SpeckvDriver::submit_prefetch(const SpeckvPrefetchReq& req, const int32_t* tokens) {
    if (!ok()) return -1;

    struct speckv_ioctl_prefetch_req ioctl_req;
    ioctl_req.req_id = req.req_id;
    ioctl_req.layer = req.layer;
    ioctl_req.reserved0 = 0;
    ioctl_req.cur_pos = req.cur_pos;
    ioctl_req.depth_k = req.depth_k;
    ioctl_req.history_len = req.history_len;
    ioctl_req.tokens_user_ptr = reinterpret_cast<uint64_t>(tokens);

    int ret = ioctl(fd_, SPECKV_IOCTL_PREFETCH, &ioctl_req);
    return (ret < 0) ? ret : 0;
}

int SpeckvDriver::poll_complete() {
    if (!ok()) return -1;

    uint32_t done = 0;
    int ret = ioctl(fd_, SPECKV_IOCTL_POLL_DONE, &done);
    if (ret < 0) return ret;
    return static_cast<int>(done);
}

int SpeckvDriver::set_prefetch_depth(uint32_t k) {
    if (!ok()) return -1;

    struct speckv_ioctl_param param;
    param.key = SPECKV_PARAM_PREFETCH_DEPTH;
    param.value = k;

    int ret = ioctl(fd_, SPECKV_IOCTL_SET_PARAM, &param);
    return (ret < 0) ? ret : 0;
}

int SpeckvDriver::set_compression_scheme(uint32_t scheme) {
    if (!ok()) return -1;

    struct speckv_ioctl_param param;
    param.key = SPECKV_PARAM_COMP_SCHEME;
    param.value = scheme;

    int ret = ioctl(fd_, SPECKV_IOCTL_SET_PARAM, &param);
    return (ret < 0) ? ret : 0;
}

