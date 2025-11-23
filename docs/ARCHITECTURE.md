# CXL-SpecKV Architecture Documentation

## Overview

CXL-SpecKV is a disaggregated KV-cache system for LLM serving that combines:
- **CXL Memory Disaggregation**: 4-8× memory capacity expansion
- **Speculative Prefetching**: 95% prediction accuracy with <10μs latency
- **FPGA-Accelerated Compression**: 3-4× compression ratio

## System Architecture

### Components

#### 1. CXL Memory Manager (`src/cxl_memory/`)

Implements a three-tier memory hierarchy:

- **L1 (GPU Local Cache)**: 8-16GB, stores most recently accessed KV-cache entries
- **L2 (Prefetch Buffer)**: 2-4GB, GPU-side buffer for speculated KV-cache entries
- **L3 (CXL Memory Pool)**: 64-256GB, main KV-cache storage on FPGA-attached memory

**Key Features:**
- Demand-driven allocation policy
- LRU replacement for L1 cache
- Hot-cold page classification
- Cache coherence protocol (CXL 2.0)
- Page migration between tiers

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

#### 4. System Integration (`src/integration/`)

Provides memory allocator interface compatible with:
- vLLM
- TensorRT-LLM

**API:**
- `cxl_malloc()`: Allocate memory with layer hint
- `cxl_free()`: Deallocate memory
- `cxl_access()`: Access with automatic prefetch
- `prefetch_hint()`: Issue speculative prefetch

## Data Flow

### Token Generation Flow

1. **GPU generates token** → sends prefetch hint to prefetcher
2. **Prefetcher predicts** next k tokens using LSTM (<10μs)
3. **Prefetcher issues** DMA transfers for predicted KV-cache entries
4. **FPGA cache engine** compresses/decompresses data as needed
5. **CXL memory manager** handles address translation and coherence
6. **Next token generation** finds KV-cache already in L1/L2 (95% hit rate)

### Memory Access Flow

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

## Performance Characteristics

From paper evaluation:

- **Throughput**: 3.2× improvement over GPU-only baseline
- **Memory Expansion**: 8× capacity (24× with compression)
- **Latency Overhead**: 8.2% per-token decode latency
- **Accuracy**: 99.5% preservation
- **Energy Efficiency**: 1.90× better J/token

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

## Future Work

As mentioned in the paper:
- Dynamic activation sparsity in attention
- Multi-tenant workload management
- Adaptation for 32K-128K context windows
- Extension to fine-tuning/training
- CXL 3.0 support (128GB/s bandwidth)
- Heterogeneous memory tiers

