// tests/test_c_api.c
// Test C API functions
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../host/include/speckv.h"

#define TEST_PASSED 0
#define TEST_FAILED 1

int test_init_finalize() {
    printf("Testing init/finalize...\n");
    
    speckv_status_t ret = speckv_init("/dev/speckv0");
    if (ret != SPECKV_OK) {
        fprintf(stderr, "speckv_init failed: %d\n", ret);
        return TEST_FAILED;
    }
    
    printf("  Initialization successful\n");
    
    speckv_finalize();
    printf("  Finalization successful\n");
    
    return TEST_PASSED;
}

int test_alloc_free() {
    printf("Testing alloc/free...\n");
    
    if (speckv_init("/dev/speckv0") != SPECKV_OK) {
        return TEST_FAILED;
    }
    
    speckv_handle_t handle;
    speckv_alloc_hint_t hint = {0, 0};
    
    // Allocate 1MB
    speckv_status_t ret = speckv_alloc(1024 * 1024, &hint, &handle);
    if (ret != SPECKV_OK) {
        fprintf(stderr, "speckv_alloc failed: %d\n", ret);
        speckv_finalize();
        return TEST_FAILED;
    }
    
    printf("  Allocated handle: %lu\n", handle);
    
    // Free
    ret = speckv_free(handle);
    if (ret != SPECKV_OK) {
        fprintf(stderr, "speckv_free failed: %d\n", ret);
        speckv_finalize();
        return TEST_FAILED;
    }
    
    printf("  Free successful\n");
    
    speckv_finalize();
    return TEST_PASSED;
}

int test_access() {
    printf("Testing access...\n");
    
    if (speckv_init("/dev/speckv0") != SPECKV_OK) {
        return TEST_FAILED;
    }
    
    speckv_handle_t handle;
    speckv_alloc_hint_t hint = {0, 0};
    
    if (speckv_alloc(4096, &hint, &handle) != SPECKV_OK) {
        speckv_finalize();
        return TEST_FAILED;
    }
    
    void *gpu_ptr = NULL;
    speckv_status_t ret = speckv_access(handle, 0, 4096, &gpu_ptr);
    if (ret != SPECKV_OK) {
        fprintf(stderr, "speckv_access failed: %d\n", ret);
        speckv_free(handle);
        speckv_finalize();
        return TEST_FAILED;
    }
    
    printf("  Access successful, GPU ptr: %p\n", gpu_ptr);
    
    speckv_free(handle);
    speckv_finalize();
    return TEST_PASSED;
}

int test_prefetch() {
    printf("Testing prefetch...\n");
    
    if (speckv_init("/dev/speckv0") != SPECKV_OK) {
        return TEST_FAILED;
    }
    
    int32_t tokens[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    
    speckv_status_t ret = speckv_prefetch(
        1,      // req_id
        0,      // layer
        100,    // cur_pos
        4,      // depth_k
        tokens,
        16      // history_len
    );
    
    if (ret != SPECKV_OK) {
        fprintf(stderr, "speckv_prefetch failed: %d\n", ret);
        speckv_finalize();
        return TEST_FAILED;
    }
    
    printf("  Prefetch successful\n");
    
    speckv_finalize();
    return TEST_PASSED;
}

int test_params() {
    printf("Testing parameter configuration...\n");
    
    if (speckv_init("/dev/speckv0") != SPECKV_OK) {
        return TEST_FAILED;
    }
    
    // Test prefetch depth
    if (speckv_set_prefetch_depth(8) != SPECKV_OK) {
        fprintf(stderr, "speckv_set_prefetch_depth failed\n");
        speckv_finalize();
        return TEST_FAILED;
    }
    
    // Test compression scheme
    if (speckv_set_compression_scheme(SPECKV_COMP_INT8_DELTA_RLE) != SPECKV_OK) {
        fprintf(stderr, "speckv_set_compression_scheme failed\n");
        speckv_finalize();
        return TEST_FAILED;
    }
    
    printf("  Parameter configuration successful\n");
    
    speckv_finalize();
    return TEST_PASSED;
}

int main() {
    printf("=== C API Test Suite ===\n");
    
    int result1 = test_init_finalize();
    int result2 = test_alloc_free();
    int result3 = test_access();
    int result4 = test_prefetch();
    int result5 = test_params();
    
    if (result1 == TEST_PASSED && result2 == TEST_PASSED && 
        result3 == TEST_PASSED && result4 == TEST_PASSED && 
        result5 == TEST_PASSED) {
        printf("=== All tests passed ===\n");
        return 0;
    } else {
        printf("=== Tests failed ===\n");
        return 1;
    }
}

