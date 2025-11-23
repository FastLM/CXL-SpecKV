// examples/test_speckv.c
// Kernel driver test example
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../driver/uapi/speckv_ioctl.h"

int main() {
    int fd = open("/dev/speckv0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    printf("fd = %d\n", fd);
    
    struct speckv_ioctl_dma_desc arr[2] = {
        {.fpga_addr=0x1000, .gpu_addr=0x2000, .bytes=256, .flags=1},
        {.fpga_addr=0x3000, .gpu_addr=0x4000, .bytes=512, .flags=0},
    };
    
    struct speckv_ioctl_dma_batch batch = {
        .user_ptr = (unsigned long)arr,
        .count = 2,
    };
    
    int ret = ioctl(fd, SPECKV_IOCTL_DMA_BATCH, &batch);
    if (ret < 0) {
        perror("ioctl DMA_BATCH");
        close(fd);
        return 1;
    }
    
    uint32_t done = 0;
    ret = ioctl(fd, SPECKV_IOCTL_POLL_DONE, &done);
    if (ret < 0) {
        perror("ioctl POLL_DONE");
        close(fd);
        return 1;
    }
    
    printf("DMA completed: %u\n", done);
    
    close(fd);
    return 0;
}

