/**
 * coherence_c_api.cpp
 * 
 * C API wrapper for CoherenceManager to enable Python bindings
 * and other language interoperability.
 */

#include "../src/cxl_memory/coherence_manager.h"
#include "../host/include/speckv_driver.h"
#include <cstring>

using namespace cxlspeckv;

// Opaque handle type
extern "C" {

typedef void* coherence_manager_handle_t;

// Statistics structure (C-compatible)
typedef struct {
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t coherence_ops;
    uint64_t invalidations_sent;
    uint64_t writebacks_performed;
    uint64_t directory_hits;
    uint64_t directory_misses;
} coherence_statistics_t;

/**
 * Create coherence manager instance
 */
coherence_manager_handle_t coherence_manager_create(
    const char* device_path,
    size_t cache_line_size
) {
    try {
        auto driver = std::make_shared<SpeckvDriver>(device_path);
        auto* mgr = new CoherenceManager(driver, cache_line_size);
        return static_cast<coherence_manager_handle_t>(mgr);
    } catch (...) {
        return nullptr;
    }
}

/**
 * Destroy coherence manager instance
 */
void coherence_manager_destroy(coherence_manager_handle_t handle) {
    if (handle) {
        auto* mgr = static_cast<CoherenceManager*>(handle);
        delete mgr;
    }
}

/**
 * Request read access
 */
bool coherence_manager_request_read(
    coherence_manager_handle_t handle,
    uint64_t addr,
    void* data_out,
    size_t size
) {
    if (!handle || !data_out) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->request_read(addr, data_out, size);
}

/**
 * Request write access
 */
bool coherence_manager_request_write(
    coherence_manager_handle_t handle,
    uint64_t addr,
    const void* data,
    size_t size
) {
    if (!handle || !data) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->request_write(addr, data, size);
}

/**
 * Invalidate cache line
 */
bool coherence_manager_invalidate(
    coherence_manager_handle_t handle,
    uint64_t addr
) {
    if (!handle) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->invalidate(addr);
}

/**
 * Writeback cache line
 */
bool coherence_manager_writeback(
    coherence_manager_handle_t handle,
    uint64_t addr,
    const void* data,
    size_t size
) {
    if (!handle || !data) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->writeback(addr, data, size);
}

/**
 * Flush all modified cache lines
 */
bool coherence_manager_flush_all(coherence_manager_handle_t handle) {
    if (!handle) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->flush_all();
}

/**
 * Get coherence state
 */
int coherence_manager_get_state(
    coherence_manager_handle_t handle,
    uint64_t addr
) {
    if (!handle) return 0;  // INVALID
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return static_cast<int>(mgr->get_state(addr));
}

/**
 * Get memory tier
 */
int coherence_manager_get_tier(
    coherence_manager_handle_t handle,
    uint64_t addr
) {
    if (!handle) return 2;  // L3_CXL
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return static_cast<int>(mgr->get_tier(addr));
}

/**
 * Promote to L1
 */
bool coherence_manager_promote_to_l1(
    coherence_manager_handle_t handle,
    uint64_t addr
) {
    if (!handle) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->promote_to_l1(addr);
}

/**
 * Demote to L3
 */
bool coherence_manager_demote_to_l3(
    coherence_manager_handle_t handle,
    uint64_t addr
) {
    if (!handle) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    return mgr->demote_to_l3(addr);
}

/**
 * Batch invalidate
 */
bool coherence_manager_batch_invalidate(
    coherence_manager_handle_t handle,
    const uint64_t* addrs,
    size_t count
) {
    if (!handle || !addrs) return false;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    
    std::vector<uint64_t> addr_vec(addrs, addrs + count);
    return mgr->batch_invalidate(addr_vec);
}

/**
 * Get statistics
 */
void coherence_manager_get_statistics(
    coherence_manager_handle_t handle,
    coherence_statistics_t* stats_out
) {
    if (!handle || !stats_out) return;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    
    auto stats = mgr->get_statistics();
    stats_out->total_reads = stats.total_reads;
    stats_out->total_writes = stats.total_writes;
    stats_out->coherence_ops = stats.coherence_ops;
    stats_out->invalidations_sent = stats.invalidations_sent;
    stats_out->writebacks_performed = stats.writebacks_performed;
    stats_out->directory_hits = stats.directory_hits;
    stats_out->directory_misses = stats.directory_misses;
}

/**
 * Reset statistics
 */
void coherence_manager_reset_statistics(coherence_manager_handle_t handle) {
    if (!handle) return;
    auto* mgr = static_cast<CoherenceManager*>(handle);
    mgr->reset_statistics();
}

} // extern "C"
