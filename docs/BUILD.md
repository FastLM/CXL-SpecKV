# Building CXL-SpecKV

## Prerequisites

### For Kernel Driver
- Linux kernel headers (for your running kernel)
- GCC compiler
- Make

### For User-space Library
- CMake 3.18 or higher
- C++17 compatible compiler (GCC 7+, Clang 8+)
- CUDA 11.8+ (for GPU support)
- Python 3.8+ (optional, for Python bindings)
- pybind11 (optional, for Python bindings)

### For FPGA
- Intel Quartus Prime (for FPGA synthesis)
- CXL 2.0 compatible hardware

## Build Instructions

### 1. Build Kernel Driver

```bash
cd driver
make
```

This will create `speckv_kernel_module.ko`.

**Load Module:**
```bash
sudo insmod speckv_kernel_module.ko
```

**Check if device is created:**
```bash
ls -l /dev/speckv0
dmesg | tail
```

**Unload Module:**
```bash
sudo rmmod speckv_kernel_module
```

**Test Driver:**
```bash
cd tests
gcc test_speckv.c -o test_speckv
sudo ./test_speckv
```

You should see DMA operations logged in `dmesg`.

### 2. Build User-space Library

**Basic Build:**
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Build with Python Bindings:**
```bash
mkdir build && cd build
cmake -DBUILD_PYTHON_BINDINGS=ON ..
make -j$(nproc)
```

**Build Options:**
- `-DBUILD_PYTHON_BINDINGS=ON`: Build Python bindings (requires pybind11)
- `-DCMAKE_BUILD_TYPE=Release`: Release build (default: Debug)
- `-DCUDA_ARCH=sm_80`: CUDA architecture (default: auto-detect)
- `-DUSE_CUDA=OFF`: Disable CUDA support

This creates:
- `libcxlspeckv.so`: Shared library
- `cxlspeckv_demo`: Demo executable

### 3. Build FPGA Hardware

**Using Quartus Prime:**
```bash
cd hardware/scripts
quartus_sh -t build_quartus.tcl
```

**Manual Build:**
1. Create new Quartus project
2. Add all RTL files from `hardware/rtl/` directory
3. Set top-level entity: `cxl_speckv_top`
4. Set device: Intel Agilex-7
5. Run compilation

See `hardware/README.md` for detailed FPGA build instructions.

## Running the Demo

```bash
cd build
./cxlspeckv_demo
```

## Testing

**User-space Tests:**
```bash
cd build
ctest
```

**Kernel Driver Test:**
```bash
cd tests
gcc test_speckv.c -o test_speckv
sudo ./test_speckv
```

## Installation

**Install User-space Library:**
```bash
cd build
make install
```

This installs:
- Library: `lib/libcxlspeckv.so`
- Headers: `include/cxlspeckv/`
- Python module: `python/cxlspeckv_py.so` (if built)

## Troubleshooting

### Kernel Module Issues

**Module won't load:**
- Check kernel logs: `dmesg | tail`
- Ensure kernel headers match running kernel
- Check for conflicting modules

**Permission denied:**
- Ensure user is in appropriate group or use `sudo`
- Check device permissions: `ls -l /dev/speckv0`
- Set permissions: `sudo chmod 666 /dev/speckv0`

### User-space Build Issues

**CUDA not found:**
- Ensure CUDA is installed and `CUDA_PATH` is set
- Or disable CUDA: `cmake -DUSE_CUDA=OFF ..`

**Python bindings not building:**
- Install pybind11: `pip install pybind11`
- Or disable: `cmake -DBUILD_PYTHON_BINDINGS=OFF ..`

**Library not found at runtime:**
- Set LD_LIBRARY_PATH: `export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH`

### FPGA Build Issues

**Quartus not found:**
- Ensure Quartus Prime is installed and in PATH
- Or use manual build process

**Synthesis errors:**
- Check RTL syntax: `vlog hardware/rtl/*.v`
- Verify device family and part number
- Check timing constraints

## Quick Start

Complete build sequence:

```bash
# 1. Build and load kernel driver
cd driver
make
sudo insmod speckv_kernel_module.ko

# 2. Build user-space library
cd ..
mkdir build && cd build
cmake ..
make -j$(nproc)

# 3. Test
cd ../tests
gcc test_speckv.c -o test_speckv -I../host/include -L../build -lcxlspeckv
LD_LIBRARY_PATH=../build sudo ./test_speckv

# 4. Run demo
cd ../build
./cxlspeckv_demo
```

## Next Steps

After building:
1. See `README.md` for usage examples
2. See `docs/ARCHITECTURE.md` for system architecture
3. See `hardware/README.md` for FPGA implementation details
