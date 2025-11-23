# Building CXL-SpecKV

## Prerequisites

- CMake 3.18 or higher
- C++17 compatible compiler (GCC 7+, Clang 8+)
- CUDA 11.8+ (for GPU support)
- Python 3.8+ (optional, for Python bindings)
- pybind11 (optional, for Python bindings)

## Build Instructions

### Basic Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build with Python Bindings

```bash
mkdir build && cd build
cmake -DBUILD_PYTHON_BINDINGS=ON ..
make -j$(nproc)
```

### Build Options

- `-DBUILD_PYTHON_BINDINGS=ON`: Build Python bindings (requires pybind11)
- `-DCMAKE_BUILD_TYPE=Release`: Release build (default: Debug)
- `-DCUDA_ARCH=sm_80`: CUDA architecture (default: auto-detect)

## Running the Demo

```bash
./build/cxlspeckv_demo
```

## Testing

```bash
cd build
ctest
```

## Installation

```bash
cd build
make install
```

This installs:
- Library: `lib/libcxlspeckv.so`
- Headers: `include/cxlspeckv/`
- Python module: `python/cxlspeckv_py.so` (if built)

## FPGA Synthesis

For FPGA implementation, see `hardware/` directory (requires Intel Quartus Prime).

## Troubleshooting

### CUDA not found
- Ensure CUDA is installed and `CUDA_PATH` is set
- Or disable CUDA: `cmake -DUSE_CUDA=OFF ..`

### Python bindings not building
- Install pybind11: `pip install pybind11`
- Or disable: `cmake -DBUILD_PYTHON_BINDINGS=OFF ..`

