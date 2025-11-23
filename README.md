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

## Project Structure

```
CXL-SpecKV/
├── src/
│   ├── cxl_memory/          # CXL memory management
│   ├── prefetcher/          # Speculative prefetching
│   ├── fpga_engine/         # FPGA cache engine
│   ├── integration/         # Framework integration
│   └── utils/               # Utility functions
├── tests/                   # Unit and integration tests
├── hardware/                # FPGA RTL designs
└── docs/                    # Documentation

```

## Requirements

- Python 3.8+
- CUDA 11.8+
- CXL 2.0 compatible hardware
- Intel Quartus Prime (for FPGA synthesis)
- PyTorch 2.0+

## Installation

```bash
pip install -r requirements.txt
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Usage

See individual component documentation in `docs/` directory.

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

