// host/include/speckv.h
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 错误码
typedef enum {
    SPECKV_OK             = 0,
    SPECKV_ERR_GENERAL    = -1,
    SPECKV_ERR_DRIVER     = -2,
    SPECKV_ERR_NOMEM      = -3,
    SPECKV_ERR_INVAL      = -4,
} speckv_status_t;

// 分配 hint：未来可以扩展
typedef struct {
    uint32_t preferred_node;  // NUMA 节点
    uint32_t reserved;
} speckv_alloc_hint_t;

// 句柄：在 Python/vLLM 里就是个 64-bit id
typedef uint64_t speckv_handle_t;

// 初始化 / 关闭
speckv_status_t speckv_init(const char* dev_path);   // eg "/dev/speckv0"
void            speckv_finalize(void);

// 分配一块"KV 区"
speckv_status_t speckv_alloc(size_t bytes,
                             const speckv_alloc_hint_t* hint,
                             speckv_handle_t* out_handle);

// 释放
speckv_status_t speckv_free(speckv_handle_t handle);

// 访问：返回 GPU 上的虚拟地址（用于注意力计算）
// 注意：这里不会真的 memcpy，只是确保对应 KV page 在 GPU HBM（L1/L2），
// 如有必要会触发同步 fetch。
speckv_status_t speckv_access(speckv_handle_t handle,
                              uint64_t offset_bytes,
                              size_t   length_bytes,
                              void**   out_gpu_ptr);

// CXL-SpecKV 预取接口：对应 Algorithm 1
// recent_tokens: 最后 history_len 个 token id
speckv_status_t speckv_prefetch(uint32_t      req_id,
                                uint16_t      layer,
                                uint32_t      cur_pos,
                                uint32_t      depth_k,
                                const int32_t* recent_tokens,
                                uint32_t      history_len);

// 运行时调参：设置当前预取深度 k、压缩模式等
typedef enum {
    SPECKV_COMP_FP16 = 0,
    SPECKV_COMP_INT8 = 1,
    SPECKV_COMP_INT8_DELTA_RLE = 2,
} speckv_comp_scheme_t;

speckv_status_t speckv_set_prefetch_depth(uint32_t depth_k);
speckv_status_t speckv_set_compression_scheme(speckv_comp_scheme_t scheme);

#ifdef __cplusplus
}
#endif

