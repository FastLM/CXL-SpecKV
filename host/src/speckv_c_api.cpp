// host/src/speckv_c_api.cpp
#include "../include/speckv.h"
#include "../include/speckv_allocator.hpp"
#include "../include/speckv_driver.hpp"
#include <memory>
#include <mutex>

static std::unique_ptr<SpeckvDriver> g_driver;
static std::unique_ptr<SpeckvAllocator> g_allocator;
static std::mutex g_mutex;
static bool g_initialized = false;

speckv_status_t speckv_init(const char* dev_path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (g_initialized) {
        return SPECKV_ERR_GENERAL;  // Already initialized
    }
    
    try {
        g_driver = std::make_unique<SpeckvDriver>(dev_path);
        if (!g_driver->ok()) {
            return SPECKV_ERR_DRIVER;
        }
        
        g_allocator = std::make_unique<SpeckvAllocator>(g_driver.get());
        g_initialized = true;
        return SPECKV_OK;
    } catch (...) {
        return SPECKV_ERR_GENERAL;
    }
}

void speckv_finalize(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_allocator.reset();
    g_driver.reset();
    g_initialized = false;
}

speckv_status_t speckv_alloc(size_t bytes,
                            const speckv_alloc_hint_t* hint,
                            speckv_handle_t* out_handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized || !out_handle) {
        return SPECKV_ERR_INVAL;
    }
    
    uint64_t handle = g_allocator->alloc(bytes);
    *out_handle = handle;
    return SPECKV_OK;
}

speckv_status_t speckv_free(speckv_handle_t handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized) {
        return SPECKV_ERR_INVAL;
    }
    
    g_allocator->free(handle);
    return SPECKV_OK;
}

speckv_status_t speckv_access(speckv_handle_t handle,
                              uint64_t offset_bytes,
                              size_t   length_bytes,
                              void**   out_gpu_ptr) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized || !out_gpu_ptr) {
        return SPECKV_ERR_INVAL;
    }
    
    void* ptr = g_allocator->access(handle, offset_bytes, length_bytes);
    if (!ptr) {
        return SPECKV_ERR_GENERAL;
    }
    
    *out_gpu_ptr = ptr;
    return SPECKV_OK;
}

speckv_status_t speckv_prefetch(uint32_t      req_id,
                                uint16_t      layer,
                                uint32_t      cur_pos,
                                uint32_t      depth_k,
                                const int32_t* recent_tokens,
                                uint32_t      history_len) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized || !recent_tokens || history_len == 0) {
        return SPECKV_ERR_INVAL;
    }
    
    g_allocator->prefetch(req_id, layer, cur_pos, depth_k, recent_tokens, history_len);
    return SPECKV_OK;
}

speckv_status_t speckv_set_prefetch_depth(uint32_t depth_k) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized) {
        return SPECKV_ERR_INVAL;
    }
    
    int ret = g_driver->set_prefetch_depth(depth_k);
    return (ret < 0) ? SPECKV_ERR_DRIVER : SPECKV_OK;
}

speckv_status_t speckv_set_compression_scheme(speckv_comp_scheme_t scheme) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    if (!g_initialized) {
        return SPECKV_ERR_INVAL;
    }
    
    int ret = g_driver->set_compression_scheme(static_cast<uint32_t>(scheme));
    return (ret < 0) ? SPECKV_ERR_DRIVER : SPECKV_OK;
}

