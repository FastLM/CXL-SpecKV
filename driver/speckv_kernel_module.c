// driver/speckv_kernel_module.c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/atomic.h>
#include "uapi/speckv_ioctl.h"

#define DEVICE_NAME "speckv"
#define SPECKV_MMIO_BASE    0xE0000000  // FPGA MMIO base address (adjust for your system)
#define SPECKV_MMIO_SIZE    (128 * 1024)  // 128KB MMIO region

// MMIO register offsets
#define SPECKV_REG_DMA_RING_BASE    0x0000
#define SPECKV_REG_DMA_RING_WR      0x0008
#define SPECKV_REG_DMA_RING_RD      0x0010
#define SPECKV_REG_DMA_COMPLETE     0x0018
#define SPECKV_REG_PREFETCH_FIFO    0x0020
#define SPECKV_REG_PREFETCH_STATUS  0x0028
#define SPECKV_REG_PARAM_PREFETCH_K 0x0030
#define SPECKV_REG_PARAM_COMP_SCHEME 0x0038

#define DMA_RING_SIZE       1024
#define PREFETCH_FIFO_SIZE  256

static dev_t speckv_dev;
static struct cdev speckv_cdev;
static struct class *speckv_class;

static void __iomem *mmio_base = NULL;
static resource_size_t mmio_phys_base = SPECKV_MMIO_BASE;
static struct resource *mmio_res = NULL;

// DMA ring buffer management
static uint32_t dma_ring_wr_ptr = 0;
static uint32_t dma_ring_rd_ptr = 0;
static atomic_t dma_pending = ATOMIC_INIT(0);

// ========== 文件 open/close ==========
static int speckv_open(struct inode *inode, struct file *file)
{
    pr_info("[speckv] device opened\n");
    return 0;
}

static int speckv_release(struct inode *inode, struct file *file)
{
    pr_info("[speckv] device closed\n");
    return 0;
}

// ========== DMA 批处理 ==========
static long handle_dma_batch(unsigned long arg)
{
    struct speckv_ioctl_dma_batch batch;

    if (copy_from_user(&batch, (void __user *)arg, sizeof(batch)))
        return -EFAULT;

    if (batch.count > 4096)
        return -EINVAL;

    size_t desc_bytes = sizeof(struct speckv_ioctl_dma_desc) * batch.count;

    struct speckv_ioctl_dma_desc *descs = kmalloc(desc_bytes, GFP_KERNEL);
    if (!descs)
        return -ENOMEM;

    if (copy_from_user(descs, (void __user *)(uintptr_t)batch.user_ptr, desc_bytes)) {
        kfree(descs);
        return -EFAULT;
    }

    // Write descriptors to FPGA MMIO ring buffer
    if (!mmio_base) {
        kfree(descs);
        return -ENODEV;
    }

    for (uint32_t i = 0; i < batch.count; i++) {
        // Check ring buffer space
        uint32_t next_wr = (dma_ring_wr_ptr + 1) % DMA_RING_SIZE;
        if (next_wr == dma_ring_rd_ptr) {
            // Ring buffer full
            pr_warn("[speckv] DMA ring buffer full\n");
            break;
        }

        // Write descriptor to ring buffer
        void __iomem *ring_addr = mmio_base + SPECKV_REG_DMA_RING_BASE + 
                                  (dma_ring_wr_ptr * sizeof(struct speckv_ioctl_dma_desc));
        
        // Write descriptor fields
        iowrite64(descs[i].fpga_addr, ring_addr);
        iowrite64(descs[i].gpu_addr, ring_addr + 8);
        iowrite32(descs[i].bytes, ring_addr + 16);
        iowrite32(descs[i].flags, ring_addr + 20);

        // Update write pointer
        dma_ring_wr_ptr = next_wr;
        iowrite32(dma_ring_wr_ptr, mmio_base + SPECKV_REG_DMA_RING_WR);
        
        atomic_inc(&dma_pending);
    }

    kfree(descs);
    return 0;
}

// ========== PREFETCH ==========
static long handle_prefetch(unsigned long arg)
{
    struct speckv_ioctl_prefetch_req req;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
        return -EFAULT;

    // 把 tokens 读出来
    int32_t *tokens = kmalloc(req.history_len * sizeof(int32_t), GFP_KERNEL);
    if (!tokens)
        return -ENOMEM;

    if (copy_from_user(tokens, (void __user *)(uintptr_t)req.tokens_user_ptr,
                       req.history_len * sizeof(int32_t))) {
        kfree(tokens);
        return -EFAULT;
    }

    // Write prefetch request to FPGA FIFO
    if (!mmio_base) {
        kfree(tokens);
        return -ENODEV;
    }

    // Check FIFO status
    uint32_t fifo_status = ioread32(mmio_base + SPECKV_REG_PREFETCH_STATUS);
    if (fifo_status & 0x80000000) {  // FIFO full bit
        pr_warn("[speckv] Prefetch FIFO full\n");
        kfree(tokens);
        return -EBUSY;
    }

    // Write request header
    void __iomem *fifo_base = mmio_base + SPECKV_REG_PREFETCH_FIFO;
    iowrite32(req.req_id, fifo_base);
    iowrite16(req.layer, fifo_base + 4);
    iowrite32(req.cur_pos, fifo_base + 8);
    iowrite32(req.depth_k, fifo_base + 12);
    iowrite32(req.history_len, fifo_base + 16);

    // Write token history
    for (uint32_t i = 0; i < req.history_len; i++) {
        iowrite32(tokens[i], fifo_base + 20 + (i * 4));
    }

    // Trigger FPGA processing
    iowrite32(1, mmio_base + SPECKV_REG_PREFETCH_STATUS);  // Start bit

    kfree(tokens);
    return 0;
}

// ========== SET_PARAM ==========
static long handle_set_param(unsigned long arg)
{
    struct speckv_ioctl_param p;
    if (copy_from_user(&p, (void __user *)arg, sizeof(p)))
        return -EFAULT;

    if (!mmio_base)
        return -ENODEV;

    // Write parameter to FPGA MMIO register
    switch (p.key) {
    case SPECKV_PARAM_PREFETCH_DEPTH:
        iowrite32(p.value, mmio_base + SPECKV_REG_PARAM_PREFETCH_K);
        break;
    case SPECKV_PARAM_COMP_SCHEME:
        iowrite32(p.value, mmio_base + SPECKV_REG_PARAM_COMP_SCHEME);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

// ========== POLL_DONE ==========
static long handle_poll_done(unsigned long arg)
{
    uint32_t done = 0;

    if (!mmio_base)
        return -ENODEV;

    // Read completion count from FPGA
    done = ioread32(mmio_base + SPECKV_REG_DMA_COMPLETE);
    
    // Update pending count
    if (done > 0) {
        atomic_sub(done, &dma_pending);
        // Clear completion register (write-back)
        iowrite32(0, mmio_base + SPECKV_REG_DMA_COMPLETE);
    }

    if (copy_to_user((void __user *)arg, &done, sizeof(done)))
        return -EFAULT;

    return 0;
}

// ========== ioctl 总入口 ==========
static long speckv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case SPECKV_IOCTL_DMA_BATCH:
        return handle_dma_batch(arg);
    case SPECKV_IOCTL_PREFETCH:
        return handle_prefetch(arg);
    case SPECKV_IOCTL_SET_PARAM:
        return handle_set_param(arg);
    case SPECKV_IOCTL_POLL_DONE:
        return handle_poll_done(arg);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations speckv_fops = {
    .owner          = THIS_MODULE,
    .open           = speckv_open,
    .release        = speckv_release,
    .unlocked_ioctl = speckv_ioctl,
};

// ========== 模块加载 ==========
static int __init speckv_init(void)
{
    int ret;

    pr_info("[speckv] loading module...\n");

    ret = alloc_chrdev_region(&speckv_dev, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

    cdev_init(&speckv_cdev, &speckv_fops);
    ret = cdev_add(&speckv_cdev, speckv_dev, 1);
    if (ret < 0) goto err_unregister;

    speckv_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(speckv_class)) {
        ret = PTR_ERR(speckv_class);
        goto err_cdev;
    }

    device_create(speckv_class, NULL, speckv_dev, NULL, "speckv0");

    // Map FPGA MMIO region
    // In real system, this would be done via device tree or PCIe BAR
    // For now, we request I/O memory region
    mmio_res = request_mem_region(mmio_phys_base, SPECKV_MMIO_SIZE, DEVICE_NAME);
    if (!mmio_res) {
        pr_err("[speckv] Failed to request MMIO region at 0x%llx\n", (unsigned long long)mmio_phys_base);
        ret = -EBUSY;
        goto err_device;
    }

    mmio_base = ioremap(mmio_phys_base, SPECKV_MMIO_SIZE);
    if (!mmio_base) {
        pr_err("[speckv] Failed to ioremap MMIO region\n");
        release_mem_region(mmio_phys_base, SPECKV_MMIO_SIZE);
        mmio_res = NULL;
        ret = -ENOMEM;
        goto err_device;
    }

    // Initialize FPGA registers
    iowrite32(0, mmio_base + SPECKV_REG_DMA_RING_WR);
    iowrite32(0, mmio_base + SPECKV_REG_DMA_RING_RD);
    iowrite32(0, mmio_base + SPECKV_REG_DMA_COMPLETE);
    iowrite32(0, mmio_base + SPECKV_REG_PREFETCH_STATUS);

    pr_info("[speckv] module loaded\n");
    return 0;

err_device:
    device_destroy(speckv_class, speckv_dev);
    class_destroy(speckv_class);
err_cdev:
    cdev_del(&speckv_cdev);
err_unregister:
    unregister_chrdev_region(speckv_dev, 1);
    return ret;
}

static void __exit speckv_exit(void)
{
    pr_info("[speckv] unloading module...\n");
    
    if (mmio_base) {
        iounmap(mmio_base);
        mmio_base = NULL;
    }
    
    if (mmio_res) {
        release_mem_region(mmio_phys_base, SPECKV_MMIO_SIZE);
        mmio_res = NULL;
    }

    device_destroy(speckv_class, speckv_dev);
    class_destroy(speckv_class);
    cdev_del(&speckv_cdev);
    unregister_chrdev_region(speckv_dev, 1);
}

module_init(speckv_init);
module_exit(speckv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dong Liu, Yanxuan Yu");
MODULE_DESCRIPTION("CXL-SpecKV minimal driver");

