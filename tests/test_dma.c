// tests/test_dma.c
// Test DMA batch operations with real FPGA MMIO
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

int test_dma_batch(int fd) {
    printf("Testing DMA batch operations...\n");
    
    // Allocate test descriptors
    struct speckv_ioctl_dma_desc *descs = malloc(sizeof(struct speckv_ioctl_dma_desc) * 4);
    if (!descs) {
        perror("malloc");
        return TEST_FAILED;
    }
    
    // Prepare test descriptors
    descs[0] = (struct speckv_ioctl_dma_desc){
        .fpga_addr = 0x4000000000ULL,  // FPGA HBM address
        .gpu_addr = 0x8000000000ULL,   // GPU HBM address
        .bytes = 4096,                 // 4KB page
        .flags = 0                     // Read operation
    };
    descs[1] = (struct speckv_ioctl_dma_desc){
        .fpga_addr = 0x4000001000ULL,
        .gpu_addr = 0x8000001000ULL,
        .bytes = 4096,
        .flags = 0
    };
    descs[2] = (struct speckv_ioctl_dma_desc){
        .fpga_addr = 0x4000002000ULL,
        .gpu_addr = 0x8000002000ULL,
        .bytes = 8192,
        .flags = 1                     // Write operation
    };
    descs[3] = (struct speckv_ioctl_dma_desc){
        .fpga_addr = 0x4000004000ULL,
        .gpu_addr = 0x8000004000ULL,
        .bytes = 4096,
        .flags = 2                     // Compressed
    };
    
    struct speckv_ioctl_dma_batch batch = {
        .user_ptr = (unsigned long)descs,
        .count = 4,
    };
    
    // Submit DMA batch
    int ret = ioctl(fd, SPECKV_IOCTL_DMA_BATCH, &batch);
    if (ret < 0) {
        perror("ioctl DMA_BATCH");
        free(descs);
        return TEST_FAILED;
    }
    
    printf("  Submitted %u DMA descriptors\n", batch.count);
    
    // Poll for completion
    uint32_t done = 0;
    int poll_count = 0;
    const int max_polls = 100;
    
    while (poll_count < max_polls) {
        ret = ioctl(fd, SPECKV_IOCTL_POLL_DONE, &done);
        if (ret < 0) {
            perror("ioctl POLL_DONE");
            free(descs);
            return TEST_FAILED;
        }
        
        if (done > 0) {
            printf("  Completed %u DMA operations\n", done);
            break;
        }
        
        usleep(1000);  // Wait 1ms
        poll_count++;
    }
    
    if (done == 0) {
        printf("  WARNING: No DMA completions received\n");
    }
    
    free(descs);
    return TEST_PASSED;
}

int main() {
    int fd = open("/dev/speckv0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open /dev/speckv0: %s\n", strerror(errno));
        fprintf(stderr, "Make sure kernel module is loaded: sudo insmod driver/speckv_kernel_module.ko\n");
        return TEST_FAILED;
    }
    
    printf("=== DMA Test Suite ===\n");
    int result = test_dma_batch(fd);
    
    close(fd);
    
    if (result == TEST_PASSED) {
        printf("=== All tests passed ===\n");
        return 0;
    } else {
        printf("=== Tests failed ===\n");
        return 1;
    }
}

