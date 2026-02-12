# Coherence Protocol Implementation

This directory contains the complete implementation of CXL-SpecKV's device-mediated cache coherence protocol.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         GPU (A100)                          │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────────┐  │
│  │  L1 Cache    │  │  L2 Prefetch │  │  Coherence Mgr  │  │
│  │  (8-16 GB)   │  │  (2-4 GB)    │  │  (Software)     │  │
│  └──────────────┘  └──────────────┘  └─────────────────┘  │
└────────────────────────┬────────────────────────────────────┘
                         │ PCIe 4.0/5.0
                         │ (GPU sends requests via PCIe)
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                    FPGA (Agilex-7)                          │
│  ┌──────────────────────────────────────────────────────┐  │
│  │         Coherence Directory Controller               │  │
│  │  (Acts as CXL Home Agent & Coherence Manager)        │  │
│  │  • Maintains directory state (MESI)                  │  │
│  │  • Issues CXL.cache invalidations                    │  │
│  │  • Translates PCIe → CXL operations                  │  │
│  └──────────────────────────────────────────────────────┘  │
│  ┌────────┐  ┌──────────┐  ┌────────┐  ┌──────────────┐  │
│  │  ATU   │  │ Compress │  │  DMA   │  │   Prefetch   │  │
│  └────────┘  └──────────┘  └────────┘  └──────────────┘  │
└────────────────────────┬────────────────────────────────────┘
                         │ CXL 2.0 (CXL.cache + CXL.mem)
                         │ (True CXL protocols)
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              CXL Memory Pool (64-256 GB)                    │
│  • Type-3 CXL memory devices                                │
│  • Cache-coherent access via CXL.cache/CXL.mem             │
│  • Main KV-cache storage (L3)                               │
└─────────────────────────────────────────────────────────────┘
```

## Key Principle

**Device-Mediated Coherence:** The GPU does NOT directly execute CXL.cache protocol. Instead:

1. **GPU → FPGA**: Standard PCIe communication
2. **FPGA acts as proxy**: Translates GPU requests into CXL operations
3. **FPGA ↔ CXL Memory**: True CXL.cache + CXL.mem protocols
4. **Result**: CXL-consistent memory semantics at system level

## Components

### 1. Hardware (FPGA RTL)

#### `coherence_directory.v`
**The core coherence controller that acts as CXL home agent.**

- **Purpose**: Maintains directory-based coherence for all KV-cache entries
- **Interfaces**:
  - `gpu_req_*`: Receives requests from GPU via PCIe
  - `cxl_mem_*`: Issues CXL.mem read/write to CXL memory pool
  - `cxl_cache_inv_*`: Sends CXL.cache invalidations to sharers
  - `cxl_cache_snoop_*`: Handles snoop requests from other CXL agents
- **Features**:
  - MESI-like coherence protocol (Invalid, Shared, Exclusive, Modified)
  - Directory with 4096 entries (configurable)
  - Handles read/write/invalidate/writeback operations
  - Tracks sharers for multi-agent scenarios
  - Statistics counters for monitoring

#### Integration in `cxl_speckv_top.v`
The coherence directory is instantiated as `u_coherence_dir` and connected to:
- PCIe/MMIO interface (for GPU requests)
- CXL IP core (for CXL.mem and CXL.cache protocols)
- Status registers (for monitoring)

### 2. Software (C++ Implementation)

#### `coherence_manager.h` / `coherence_manager.cpp`
**Host-side coherence manager that coordinates with FPGA.**

- **Purpose**: Software interface to the FPGA coherence controller
- **Key Methods**:
  - `request_read()`: Issue coherent read request
  - `request_write()`: Issue coherent write request (with invalidations)
  - `invalidate()`: Invalidate cache line
  - `writeback()`: Write back modified data
  - `promote_to_l1()` / `demote_to_l3()`: Memory tier migration
  - `get_state()`: Query coherence state
- **Features**:
  - Shadow directory (local copy of FPGA directory)
  - Batch operations for efficiency
  - Statistics tracking
  - Thread-safe operations

### 3. Examples

#### `example_coherence_demo.cpp`
**Complete demonstration of coherence protocol in action.**

Shows five key scenarios:
1. **Read Path**: GPU reads from CXL memory via FPGA
2. **Write Path**: GPU writes with FPGA sending invalidations
3. **Prefetch Path**: Read-only prefetch bypassing coherence overhead
4. **Eviction/Writeback**: Modified data writeback during eviction
5. **Memory Migration**: Hot/cold page promotion/demotion

Run with:
```bash
cd examples
g++ -std=c++17 example_coherence_demo.cpp -I.. -L../build -lcxlspeckv -o coherence_demo
sudo ./coherence_demo
```

## Protocol Details

### Read Path

```
1. GPU needs KV-cache entry
2. GPU sends PCIe read request → FPGA
3. FPGA checks directory:
   - HIT (SHARED/EXCLUSIVE/MODIFIED): Fast path, data available
   - MISS: Fetch from CXL memory
4. FPGA issues CXL.mem read to CXL memory pool
5. FPGA updates directory entry to SHARED state
6. FPGA DMAs data back to GPU via PCIe
7. GPU caches in L1
```

**Result**: Next read hits in directory (no CXL access needed)

### Write Path

```
1. GPU generates new KV-cache entry
2. GPU writes to local L1 cache
3. GPU sends PCIe write notification → FPGA
4. FPGA checks directory:
   - If SHARED: Send CXL.cache invalidations to other sharers
   - If EXCLUSIVE/MODIFIED: Can write immediately
5. FPGA issues CXL.mem write to CXL memory
6. FPGA updates directory entry to MODIFIED state
7. Schedule lazy writeback during idle cycles
```

**Result**: Other agents' copies invalidated, coherence maintained

### Prefetch Path (Optimized)

```
1. LSTM predicts next k tokens
2. Prefetcher issues non-blocking read requests
3. FPGA directly accesses CXL memory (read-only)
4. NO directory update (bypass coherence critical path)
5. Data placed in L2 prefetch buffer
6. If prediction wrong: Simply discard, no coherence rollback
```

**Result**: <10μs prefetch latency with 95% accuracy

## Coherence States (MESI)

| State | Description | Can Read? | Can Write? | Sharers? |
|-------|-------------|-----------|------------|----------|
| **I**nvalid | Not cached or invalid | No | No | 0 |
| **S**hared | Cached, read-only | Yes | No | 1+ |
| **E**xclusive | Cached, clean, exclusive | Yes | Yes | 1 |
| **M**odified | Cached, dirty, exclusive | Yes | Yes | 1 |

## State Transitions

```
        read (miss)           write
INVALID ───────────→ SHARED ───────────→ MODIFIED
   ▲                    │                    │
   │                    │                    │
   └────────────────────┴────────────────────┘
          evict/invalidate
```

## Memory Tiers

| Tier | Location | Size | Latency | Managed By |
|------|----------|------|---------|------------|
| **L1** | GPU HBM | 8-16 GB | ~100 ns | GPU + FPGA directory |
| **L2** | GPU Prefetch | 2-4 GB | ~100 ns | Prefetcher + FPGA |
| **L3** | CXL Memory | 64-256 GB | ~300 ns | FPGA home agent |

## Key Design Decisions

1. FPGA as Home Agent, direct connection to CXL memory, works with existing PCIe GPUs by coherence design

2. Device-Mediated vs. Native Coherence

| Aspect | Device-Mediated (Our Approach) | Native CXL |
|--------|-------------------------------|------------|
| GPU Hardware | Standard PCIe GPU | CXL-enabled GPU |
| Latency | PCIe + translation (~50ns) | CXL direct (~30ns) |
| Compatibility | Works today | Future only |
| Flexibility | FPGA programmable | Fixed in hardware |
| Deployment | Immediate | Requires new GPUs |

**Our choice**: Pragmatic device-mediated approach for real-world deployment


## Usage Example

```cpp
#include "coherence_manager.h"

// Initialize
auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
CoherenceManager coherence_mgr(driver, 64);  // 64-byte cache lines

// Read KV-cache entry
uint64_t addr = 0x10000;
char buffer[64];
coherence_mgr.request_read(addr, buffer, sizeof(buffer));

// Write new KV-cache entry
char new_data[64];
coherence_mgr.request_write(addr, new_data, sizeof(new_data));

// Check coherence state
auto state = coherence_mgr.get_state(addr);
// Returns: INVALID, SHARED, EXCLUSIVE, or MODIFIED

// Promote hot data to L1
coherence_mgr.promote_to_l1(hot_addr);

// Flush all modified data
coherence_mgr.flush_all();

// Get statistics
auto stats = coherence_mgr.get_statistics();
std::cout << "Hit rate: " << stats.hit_rate() << std::endl;
```

## Testing

### Hardware Simulation
```bash
cd hardware/rtl
vlog coherence_directory.v
vsim coherence_directory
# Run test vectors
```

### Software Unit Tests
```bash
cd tests
make test_coherence
sudo ./test_coherence
```

### End-to-End Demo
```bash
cd examples
make coherence_demo
sudo ./coherence_demo
```

Expected output shows all 5 scenarios with detailed step-by-step execution.

## Monitoring and Debugging

### MMIO Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x1000 | COHERENCE_OP | Operation type (R/W/INV/WB) |
| 0x1004 | COHERENCE_ADDR_LO | Address low 32 bits |
| 0x1008 | COHERENCE_ADDR_HI | Address high 32 bits |
| 0x100C | COHERENCE_STATUS | Operation status |
| 0x1010 | DIR_ENTRIES_USED | Number of directory entries in use |
| 0x1014 | DIR_SHARED_COUNT | Entries in SHARED state |
| 0x1018 | DIR_EXCLUSIVE_COUNT | Entries in EXCLUSIVE state |
| 0x101C | DIR_MODIFIED_COUNT | Entries in MODIFIED state |
| 0x1020 | COHERENCE_OPS_COUNT | Total coherence operations |

### Debug Functions

```cpp
// Print directory state
coherence_mgr.print_directory_state();

// Sync from FPGA
coherence_mgr.sync_directory_from_fpga();

// Get detailed statistics
auto stats = coherence_mgr.get_statistics();
```

## Related Documentation

- **[ARCHITECTURE.md](../docs/ARCHITECTURE.md)**: System architecture overview

## FAQ

**Q: Does the GPU run CXL.cache protocol?**

A: No. The GPU uses PCIe to communicate with the FPGA. The FPGA acts as a CXL home agent and translates GPU requests into CXL operations.

**Q: Where is coherence managed?**

A: On the FPGA. The FPGA maintains the authoritative directory and issues all CXL.cache invalidations and CXL.mem operations.

**Q: What if I have a CXL-native GPU in the future?**

A: The system is forward-compatible. A CXL-native GPU could directly participate in CXL.cache, potentially bypassing the FPGA proxy for some operations.

**Q: Is this a "real" coherent system?**

A: Yes. We provide coherent memory semantics at the system level through device-mediated coherence. This is a standard technique in heterogeneous systems.

## References

- AMD Infinity Fabric (I/O die as coherence hub)
- Intel CXL switches (switch as coherence agent)
- NVIDIA Grace-Hopper (coherence bridge)
- ARM CMN-700 (coherent mesh with external agents)

---