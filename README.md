# CXL-SpecKV: A Disaggregated FPGA Speculative KV-Cache for Datacenter LLM Serving

This repository implements CXL-SpecKV, a novel disaggregated KV-cache architecture that leverages Compute Express Link (CXL) interconnects and FPGA accelerators to enable efficient speculative execution and memory disaggregation for LLM serving.

## Architecture Overview

CXL-SpecKV consists of four main components:

1. **CXL Memory Manager**: Orchestrates allocation, migration, and coherence of KV-cache data across GPU local memory and FPGA-attached CXL memory pools
2. **Speculative Prefetcher**: Lightweight LSTM-based prediction module that predicts future token sequences and preloads KV-cache entries
3. **FPGA Cache Engine**: Custom FPGA accelerator implementing compression/decompression pipeline, address translation, and cache management
4. **System Integration**: Seamless integration with popular LLM serving frameworks (vLLM, TensorRT-LLM)

## Key Features

- **Memory Disaggregation**: 4-8× capacity expansion through CXL memory pooling
- **Speculative Prefetching**: 95% prediction accuracy with <10μs latency
- **FPGA-Accelerated Compression**: 3-4× compression ratio with minimal accuracy loss
- **High Performance**: 3.2× throughput improvement over GPU-only baselines

## Project Core Files Structure

```
CXL-SpecKV/
├── driver/                  # Kernel driver (IOCTL interface)
├── host/                    # User-space (C++ driver, allocator, C API, Python)
├── src/                     # Core components (memory manager, prefetcher, FPGA engine)
├── hardware/                # FPGA RTL designs
└── tests/                   # Functional tests (DMA, prefetch, allocator, C API)

```

## Requirements

- Python 3.8+
- CUDA 11.8+
- CXL 2.0 compatible hardware
- Intel Quartus Prime (for FPGA synthesis)
- PyTorch 2.0+

## Installation

### 1. Build Kernel Driver

```bash
cd driver
make
sudo insmod speckv_kernel_module.ko
```

### 2. Build User-space Library

```bash
pip install -r requirements.txt
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This creates:
- `libcxlspeckv.so`: Shared library
- `cxlspeckv_demo`: Demo executable

## Usage

### Run Tests

```bash
# Build all tests
cd tests
make

# Run individual tests
sudo ./test_dma          # Test DMA operations
sudo ./test_prefetch     # Test prefetch functionality
sudo ./test_params       # Test parameter configuration
sudo ./test_c_api        # Test C API
sudo ./test_allocator    # Test memory allocator
python3 test_python.py   # Test Python integration

# Run all tests
make test
```

### Python Integration

```python
from host.python.vllm_speckv_backend import CxlSpeckvKVAllocator

allocator = CxlSpeckvKVAllocator(lib_path="./build/libcxlspeckv.so")
handle = allocator.allocate(num_tokens=1024, num_layers=80, ...)
```

For detailed usage, see `docs/ARCHITECTURE.md` and `docs/BUILD.md`.

## Technical Documentation

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - Complete system architecture and data flow
- **[BUILD.md](docs/BUILD.md)** - Build instructions and dependencies

<!-- ### Frequently Asked Questions -->

<!-- **Q: Does the GPU directly support CXL protocols?**

A: No. Current GPUs (e.g., NVIDIA A100) use PCIe to communicate with the FPGA. The FPGA acts as a CXL home agent and coherence manager, translating GPU requests into CXL operations. This provides coherent memory semantics without requiring GPU hardware modifications. See [COHERENCE_CLARIFICATION.md](docs/COHERENCE_CLARIFICATION.md) for details. -->

## Citation

If you use this code in your research, please cite:

```
@inproceedings{cxlspeckv2025,
  title={CXL-SpecKV: A Disaggregated FPGA Speculative KV-Cache for Datacenter LLM Serving},
  author={Dong Liu and Yanxuan Yu},
  booktitle={FPGA '26},
  year={2026}
}
```

## License

MIT License

