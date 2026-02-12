# CXL-SpecKV Architecture Documentation

## Overview

CXL-SpecKV is a disaggregated KV-cache system for LLM serving that combines:
- **CXL Memory Disaggregation**: 4-8× memory capacity expansion
- **Speculative Prefetching**: 95% prediction accuracy with <10μs latency
- **FPGA-Accelerated Compression**: 3-4× compression ratio

The system follows a layered architecture from kernel driver to Python/vLLM integration.

## System Architecture

### Architecture Layers

#### 1. Kernel Driver (`driver/`)

**Files:**
- `speckv_kernel_module.c`: Linux kernel module
- `uapi/speckv_ioctl.h`: IOCTL interface definitions

**Features:**
- `/dev/speckv0` character device
- IOCTL commands:
  - `SPECKV_IOCTL_DMA_BATCH`: Submit DMA operations
  - `SPECKV_IOCTL_PREFETCH`: Submit prefetch requests
  - `SPECKV_IOCTL_SET_PARAM`: Set runtime parameters
  - `SPECKV_IOCTL_POLL_DONE`: Poll for completion

**Usage:**
```bash
sudo insmod driver/speckv_kernel_module.ko
```

#### 2. User-space Driver (`host/src/speckv_driver.cpp`)

**Class:** `SpeckvDriver`

**Features:**
- Opens `/dev/speckv0`
- Wraps IOCTL calls
- DMA batch submission
- Prefetch request submission
- Parameter configuration

#### 3. Memory Allocator (`host/src/speckv_allocator.cpp`)

**Class:** `SpeckvAllocator`

**Features:**
- Memory allocation/deallocation
- Page table management
- Virtual to physical address mapping
- L1/L2 cache tracking
- Synchronous fetch on cache miss

**Key Methods:**
- `alloc()`: Allocate KV cache region
- `access()`: Access KV entry (triggers fetch if needed)
- `prefetch()`: Issue speculative prefetch

#### 4. C API (`host/src/speckv_c_api.cpp`)

**Header:** `host/include/speckv.h`

**Functions:**
- `speckv_init()`: Initialize system
- `speckv_alloc()`: Allocate memory
- `speckv_access()`: Access memory
- `speckv_prefetch()`: Issue prefetch
- `speckv_set_prefetch_depth()`: Configure prefetch depth
- `speckv_set_compression_scheme()`: Configure compression

#### 5. Python Integration (`host/python/`)

**Files:**
- `speckv_ctypes.py`: ctypes wrapper for C API
- `vllm_speckv_backend.py`: vLLM KV allocator backend

**Usage:**
```python
from host.python.vllm_speckv_backend import CxlSpeckvKVAllocator

allocator = CxlSpeckvKVAllocator(lib_path="./libspeckv.so")
handle = allocator.allocate(num_tokens=1024, ...)
```

### Core Components (Legacy Implementation)

#### 1. CXL Memory Manager (`src/cxl_memory/`)

Implements a three-tier memory hierarchy:

- **L1 (GPU Local Cache)**: 8-16GB, stores most recently accessed KV-cache entries
- **L2 (Prefetch Buffer)**: 2-4GB, GPU-side buffer for speculated KV-cache entries
- **L3 (CXL Memory Pool)**: 64-256GB, main KV-cache storage on FPGA-attached memory

**Key Features:**
- Demand-driven allocation policy
- LRU replacement for L1 cache
- Hot-cold page classification
- Cache coherence protocol (FPGA-managed, CXL 2.0 semantics)
- Page migration between tiers

**Coherence Model:**

CXL-SpecKV implements a **device-mediated coherent shared memory model**:
- The FPGA acts as the coherence manager and CXL home agent
- GPU accesses KV-cache via PCIe; FPGA translates to CXL operations
- Directory-based coherence maintained on FPGA
- Provides CXL-consistent memory semantics without GPU protocol modifications
- See `docs/COHERENCE_CLARIFICATION.md` for detailed explanation

#### 2. Speculative Prefetcher (`src/prefetcher/`)

Predicts future token sequences using a lightweight LSTM model:

- **Architecture**: 2-layer LSTM with 128 hidden units
- **Model Size**: 128K parameters (512KB in FP16)
- **Prediction Latency**: <10μs
- **Accuracy**: 95% top-4 accuracy

**Algorithm** (from paper Algorithm 1):
1. Predict next k tokens using LSTM
2. Compute KV-cache addresses for predicted tokens
3. Issue non-blocking DMA prefetches
4. Handle mispredictions with lazy invalidation

#### 3. FPGA Cache Engine (`src/fpga_engine/`)

Implements compression/decompression pipeline:

**Compression Pipeline** (20 stages):
- Stages 1-4: Input buffering
- Stages 5-8: Scaling and quantization (FP16 → INT8)
- Stages 9-14: Delta encoding
- Stages 15-18: Run-length encoding (RLE)
- Stages 19-20: Output formatting

**Compression Ratio**: 2.5-4× depending on layer (early layers: 3-4×, late layers: 2.5-3×)

**Throughput**: 51.2 GB/s per engine at 800MHz

#### 4. FPGA Hardware (`hardware/rtl/`)

**RTL Modules:**
- `atu.v`: Address Translation Unit with TLB
- `kv_compress.v`: 20-stage compression pipeline
- `kv_decompress.v`: Decompression pipeline
- `dma_engine.v`: Scatter-gather DMA engine
- `cxl_mem_if.v`: CXL memory interface
- `prefetch_core.v`: Prefetch coordination core
- `cxl_speckv_top.v`: Top-level integration module

<!-- ## Data Flow

### Prefetch Flow

1. **vLLM decode step** calls `prefetch_step()`
2. **Python backend** calls `speckv_prefetch()` C API
3. **C API** calls `SpeckvAllocator::prefetch()`
4. **Allocator** calls `SpeckvDriver::submit_prefetch()`
5. **Driver** issues IOCTL to kernel
6. **Kernel** writes to FPGA MMIO/FIFO
7. **FPGA** processes prefetch request

### Memory Access Flow

1. **vLLM** needs KV entry at (req_id, layer, pos)
2. **Python backend** calls `get_kv_ptr()`
3. **C API** calls `speckv_access()`
4. **Allocator** checks page table:
   - If in L1/L2: return GPU address
   - If not: trigger sync fetch via DMA
5. **Driver** submits DMA batch
6. **Kernel** processes DMA
7. **FPGA** fetches from CXL memory
8. **GPU** receives data in HBM

### Token Generation Flow

1. **GPU generates token** → sends prefetch hint to prefetcher
2. **Prefetcher predicts** next k tokens using LSTM (<10μs)
3. **Prefetcher issues** DMA transfers for predicted KV-cache entries
4. **FPGA cache engine** compresses/decompresses data as needed
5. **CXL memory manager** handles address translation and coherence
6. **Next token generation** finds KV-cache already in L1/L2 (95% hit rate)

### Memory Access Flow (Legacy)

```
GPU Request → L1 Cache Check
    ↓ (miss)
L2 Prefetch Buffer Check
    ↓ (miss)
CXL Memory Pool (L3)
    ↓
FPGA Cache Engine (decompress if needed)
    ↓
Transfer to L2/L1
```

## Address Encoding

Virtual address encoding (matches C++ and Python):
```
[req_id:32][layer:16][head:8][pos:32][kind:1]
```

Where:
- `req_id`: Request identifier
- `layer`: Transformer layer (0-79)
- `head`: Attention head
- `pos`: Token position
- `kind`: 0=Key, 1=Value

## Integration with vLLM

The `CxlSpeckvKVAllocator` class implements the KV cache allocator interface:

```python
class CxlSpeckvKVAllocator:
    def allocate(...)      # Allocate KV cache
    def get_kv_ptr(...)    # Get GPU pointer for entry
    def prefetch_step(...) # Issue prefetch
```

In vLLM decode loop:
```python
for layer in range(num_layers):
    kv_allocator.prefetch_step(
        req_id=req_id,
        layer=layer,
        cur_pos=cur_pos,
        recent_tokens=last_16_tokens,
        depth_k=4
    )
```

## Performance Characteristics

From paper evaluation:

- **Throughput**: 3.2× improvement over GPU-only baseline
- **Memory Expansion**: 8× capacity (24× with compression)
- **Latency Overhead**: 8.2% per-token decode latency
- **Accuracy**: 99.5% preservation
- **Energy Efficiency**: 1.90× better J/token -->

## Configuration

System can be configured via `CXLSpecKVSystem::SystemConfig`:

```cpp
config.l1_size_gb = 12;        // L1 cache size
config.l2_size_gb = 3;         // Prefetch buffer size
config.l3_size_gb = 128;       // CXL memory pool size
config.prefetch_depth = 4;      // Number of tokens to predict
config.num_fpga_engines = 1;    // Number of FPGA engines
config.fpga_clock_mhz = 800.0;  // FPGA clock frequency
```

## Usage Example

See `src/main.cpp` and `examples/simple_example.cpp` for usage examples.

<!-- ## Future Work -->

<!-- 
- Dynamic activation sparsity in attention
- Multi-tenant workload management
- Adaptation for 32K-128K context windows
- Extension to fine-tuning/training
- CXL 3.0 support (128GB/s bandwidth)
- Heterogeneous memory tiers -->
