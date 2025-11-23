// tests/test_prefetch.c
// Test speculative prefetch operations
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../driver/uapi/speckv_ioctl.h"

#define TEST_PASSED 0
#define TEST_FAILED 1

int test_prefetch_request(int fd) {
    printf("Testing prefetch request...\n");
    
    // Prepare token history (last 16 tokens)
    int32_t tokens[16] = {
        101, 102, 103, 104, 105, 106, 107, 108,
        109, 110, 111, 112, 113, 114, 115, 116
    };
    
    struct speckv_ioctl_prefetch_req req = {
        .req_id = 1,
        .layer = 0,
        .reserved0 = 0,
        .cur_pos = 100,
        .depth_k = 4,
        .history_len = 16,
        .tokens_user_ptr = (unsigned long)tokens
    };
    
    // Submit prefetch request
    int ret = ioctl(fd, SPECKV_IOCTL_PREFETCH, &req);
    if (ret < 0) {
        perror("ioctl PREFETCH");
        return TEST_FAILED;
    }
    
    printf("  Submitted prefetch: req_id=%u, layer=%u, pos=%u, k=%u\n",
           req.req_id, req.layer, req.cur_pos, req.depth_k);
    
    // Test multiple layers
    for (uint16_t layer = 0; layer < 5; layer++) {
        req.layer = layer;
        req.cur_pos = 100 + layer;
        
        ret = ioctl(fd, SPECKV_IOCTL_PREFETCH, &req);
        if (ret < 0) {
            perror("ioctl PREFETCH");
            return TEST_FAILED;
        }
        
        printf("  Prefetch layer %u submitted\n", layer);
    }
    
    return TEST_PASSED;
}

int test_prefetch_batch(int fd) {
    printf("Testing prefetch batch operations...\n");
    
    int32_t tokens[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    
    // Submit multiple prefetch requests
    for (uint32_t req_id = 1; req_id <= 10; req_id++) {
        struct speckv_ioctl_prefetch_req req = {
            .req_id = req_id,
            .layer = 0,
            .cur_pos = req_id * 10,
            .depth_k = 4,
            .history_len = 16,
            .tokens_user_ptr = (unsigned long)tokens
        };
        
        int ret = ioctl(fd, SPECKV_IOCTL_PREFETCH, &req);
        if (ret < 0) {
            perror("ioctl PREFETCH");
            return TEST_FAILED;
        }
    }
    
    printf("  Submitted 10 prefetch requests\n");
    return TEST_PASSED;
}

int main() {
    int fd = open("/dev/speckv0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open /dev/speckv0: %s\n", strerror(errno));
        fprintf(stderr, "Make sure kernel module is loaded: sudo insmod driver/speckv_kernel_module.ko\n");
        return TEST_FAILED;
    }
    
    printf("=== Prefetch Test Suite ===\n");
    int result1 = test_prefetch_request(fd);
    int result2 = test_prefetch_batch(fd);
    
    close(fd);
    
    if (result1 == TEST_PASSED && result2 == TEST_PASSED) {
        printf("=== All tests passed ===\n");
        return 0;
    } else {
        printf("=== Tests failed ===\n");
        return 1;
    }
}

