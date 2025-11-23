// driver/uapi/speckv_ioctl.h
#pragma once

#include <linux/ioctl.h>
#include <linux/types.h>

#define SPECKV_MAGIC  'K'

// ========== DMA 描述符 ==========
struct speckv_ioctl_dma_desc {
    __u64 fpga_addr;
    __u64 gpu_addr;
    __u32 bytes;
    __u32 flags;   // bit0=RW, bit1=compressed, bit2=prefetch
};

// batch: 用户态指向的是一个数组
struct speckv_ioctl_dma_batch {
    __u64 user_ptr;   // userspace array ptr
    __u32 count;
    __u32 reserved;
};

// ========== Prefetch ==========
struct speckv_ioctl_prefetch_req {
    __u32 req_id;
    __u16 layer;
    __u16 reserved0;
    __u32 cur_pos;
    __u32 depth_k;
    __u32 history_len;
    __u64 tokens_user_ptr;  // int32[history_len]
};

// ========== 参数设置 ==========
struct speckv_ioctl_param {
    __u32 key;   // 1 = prefetch_depth, 2 = comp_scheme
    __u32 value;
};

// key 值
#define SPECKV_PARAM_PREFETCH_DEPTH  1
#define SPECKV_PARAM_COMP_SCHEME     2

// ========== IOCTL 定义 ==========
#define SPECKV_IOCTL_DMA_BATCH   _IOW(SPECKV_MAGIC, 0x01, struct speckv_ioctl_dma_batch)
#define SPECKV_IOCTL_PREFETCH    _IOW(SPECKV_MAGIC, 0x02, struct speckv_ioctl_prefetch_req)
#define SPECKV_IOCTL_SET_PARAM   _IOW(SPECKV_MAGIC, 0x03, struct speckv_ioctl_param)
#define SPECKV_IOCTL_POLL_DONE   _IOR(SPECKV_MAGIC, 0x04, __u32)

