# CXL-SpecKV FPGA Hardware

This directory contains the FPGA RTL implementation of CXL-SpecKV components.

## Directory Structure

```
hardware/
├── rtl/                    # RTL source files
│   ├── atu.v              # Address Translation Unit
│   ├── kv_compress.v      # Compression pipeline
│   ├── kv_decompress.v    # Decompression pipeline
│   ├── dma_engine.v       # DMA engine
│   ├── cxl_mem_if.v       # CXL memory interface
│   ├── coherence_directory.v  # Coherence directory controller
│   ├── prefetch_core.v    # Prefetch core
│   └── cxl_speckv_top.v   # Top-level module
└── scripts/
    └── build_quartus.tcl   # Quartus build script
```

## Components

### 1. Address Translation Unit (ATU) - `atu.v`

Implements virtual to physical address translation with TLB:
- TLB with configurable entries (default 256)
- Page table walker for TLB misses
- 4KB page size
- Pipeline latency: ~4 cycles (TLB hit), ~15 cycles (page walk)

### 2. Compression Pipeline - `kv_compress.v`

20-stage compression pipeline implementing Algorithm 2:
- Stages 1-4: Max absolute value computation
- Stages 5-8: Scale factor calculation
- Stages 9-14: FP16→INT8 quantization + delta encoding
- Stages 15-18: Run-length encoding
- Stages 19-20: Output formatting
- Throughput: 1 entry per cycle (fully pipelined)

### 3. Decompression Pipeline - `kv_decompress.v`

Inverse of compression pipeline:
- RLE decode → Delta decode → Dequantize
- Same 20-stage pipeline structure
- Throughput: 1 entry per cycle

### 4. DMA Engine - `dma_engine.v`

Scatter-gather DMA engine:
- Supports up to 16 outstanding descriptors
- AXI4 master interface
- Supports compressed/uncompressed transfers
- Completion tracking

### 5. CXL Memory Interface - `cxl_mem_if.v`

Wrapper for CXL.mem and CXL.cache protocols:
- Cache-coherent memory access
- Burst transfer support
- Cache invalidation on writes

### 6. Coherence Directory Controller - `coherence_directory.v`**

**The core coherence controller that acts as CXL home agent:**

- Directory-based coherence protocol (MESI: Invalid/Shared/Exclusive/Modified)
- Receives GPU requests via PCIe/MMIO
- Issues CXL.mem reads/writes to CXL memory pool
- Sends CXL.cache invalidations to maintain coherence
- Handles snoop requests from other CXL agents
- 4096-entry directory (configurable)
- Tracks up to 4 sharers per cache line
- Statistics: directory hits, coherence ops, state counts

this module makes the GPU ↔ FPGA ↔ CXL memory system coherent. The FPGA acts as the CXL home agent and coherence manager, translating PCIe requests from GPU into CXL-compliant operations.

### 7. Prefetch Core - `prefetch_core.v`

Implements Algorithm 1 from paper:
- Receives prefetch requests from host
- Coordinates LSTM prediction
- Address translation via ATU
- L1/L2 directory checking
- DMA descriptor generation

### 8. Top-Level Module - `cxl_speckv_top.v`

Integrates all components:
- MMIO register interface
- Component interconnection
- Coherence directory controller integration
- CXL.mem and CXL.cache interfaces
- Status and completion tracking


## Build Instructions

### Using Quartus Prime

```bash
cd hardware/scripts
quartus_sh -t build_quartus.tcl
```

### Manual Build

1. Create new Quartus project
2. Add all RTL files from `rtl/` directory
3. Set top-level entity: `cxl_speckv_top`
4. Set device: Intel Agilex-7
5. Run compilation

## Integration Notes

### LSTM Integration

The prefetch core expects an LSTM block that:
- Accepts 16 token IDs as input
- Returns top-k predicted tokens with confidences
- Latency: <10μs (80 cycles @ 800MHz)

Implementation options:
1. Soft CPU (Nios II) running C code
2. HLS-generated LSTM block
3. Custom RTL LSTM

### CXL IP Integration

Connect `cxl_mem_if.v` to Intel CXL IP core:
- CXL.mem protocol
- CXL.cache coherence
- Memory-side caching (CXL 3.0)

### HBM Integration

Connect DMA engine to HBM controller:
- AXI4 interface
- 16 channels
- 1.6 TB/s aggregate bandwidth

## Resource Utilization (Estimated)

Based on Agilex-7:
- ALMs: ~30% (from paper)
- DSP blocks: ~26%
- M20K blocks: ~16%
- Maximum frequency: 812 MHz

## Testing

### Simulation

Use ModelSim/QuestaSim:
```bash
vlog rtl/*.v
vsim cxl_speckv_top
```

### Hardware Testing

1. Program FPGA with bitstream
2. Load kernel driver
3. Run user-space tests
4. Monitor MMIO registers

<!-- ## Future Enhancements

- [ ] Complete RLE implementation
- [ ] Full FP16 arithmetic units
- [ ] LSTM RTL implementation
- [ ] Multi-engine scaling
- [ ] CXL 3.0 support
- [ ] Performance counters
- [ ] Multi-sharer tracking beyond 4 agents
- [ ] Adaptive coherence policies based on workload -->

## See Also

- **[Coherence Implementation Guide](../src/cxl_memory/COHERENCE_IMPLEMENTATION.md)**: Detailed explanation of coherence protocol
- **[Architecture Documentation](../docs/ARCHITECTURE.md)**: System overview

