// tests/test_params.c
// Test parameter configuration
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

int test_set_prefetch_depth(int fd) {
    printf("Testing prefetch depth configuration...\n");
    
    uint32_t test_depths[] = {1, 2, 4, 8, 16};
    int num_tests = sizeof(test_depths) / sizeof(test_depths[0]);
    
    for (int i = 0; i < num_tests; i++) {
        struct speckv_ioctl_param param = {
            .key = SPECKV_PARAM_PREFETCH_DEPTH,
            .value = test_depths[i]
        };
        
        int ret = ioctl(fd, SPECKV_IOCTL_SET_PARAM, &param);
        if (ret < 0) {
            perror("ioctl SET_PARAM (prefetch_depth)");
            return TEST_FAILED;
        }
        
        printf("  Set prefetch depth to %u\n", test_depths[i]);
    }
    
    return TEST_PASSED;
}

int test_set_compression_scheme(int fd) {
    printf("Testing compression scheme configuration...\n");
    
    uint32_t schemes[] = {
        0,  // FP16
        1,  // INT8
        2   // INT8_DELTA_RLE
    };
    const char *scheme_names[] = {"FP16", "INT8", "INT8_DELTA_RLE"};
    int num_schemes = sizeof(schemes) / sizeof(schemes[0]);
    
    for (int i = 0; i < num_schemes; i++) {
        struct speckv_ioctl_param param = {
            .key = SPECKV_PARAM_COMP_SCHEME,
            .value = schemes[i]
        };
        
        int ret = ioctl(fd, SPECKV_IOCTL_SET_PARAM, &param);
        if (ret < 0) {
            perror("ioctl SET_PARAM (comp_scheme)");
            return TEST_FAILED;
        }
        
        printf("  Set compression scheme to %s (%u)\n", scheme_names[i], schemes[i]);
    }
    
    return TEST_PASSED;
}

int test_invalid_param(int fd) {
    printf("Testing invalid parameter handling...\n");
    
    struct speckv_ioctl_param param = {
        .key = 999,  // Invalid key
        .value = 123
    };
    
    int ret = ioctl(fd, SPECKV_IOCTL_SET_PARAM, &param);
    if (ret == 0) {
        printf("  ERROR: Invalid parameter accepted\n");
        return TEST_FAILED;
    }
    
    printf("  Correctly rejected invalid parameter (expected)\n");
    return TEST_PASSED;
}

int main() {
    int fd = open("/dev/speckv0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open /dev/speckv0: %s\n", strerror(errno));
        fprintf(stderr, "Make sure kernel module is loaded: sudo insmod driver/speckv_kernel_module.ko\n");
        return TEST_FAILED;
    }
    
    printf("=== Parameter Test Suite ===\n");
    int result1 = test_set_prefetch_depth(fd);
    int result2 = test_set_compression_scheme(fd);
    int result3 = test_invalid_param(fd);
    
    close(fd);
    
    if (result1 == TEST_PASSED && result2 == TEST_PASSED && result3 == TEST_PASSED) {
        printf("=== All tests passed ===\n");
        return 0;
    } else {
        printf("=== Tests failed ===\n");
        return 1;
    }
}

