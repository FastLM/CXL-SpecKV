"""
CXL-SpecKV Coherence Manager Python Bindings

This module provides Python interface to the C++ CoherenceManager class,
allowing Python applications (like vLLM) to use cache coherence features.

Example usage:
    from cxlspeckv_coherence import CoherenceManager, CoherenceState
    
    mgr = CoherenceManager("/dev/speckv0")
    
    # Read KV-cache entry
    data = mgr.request_read(addr=0x10000, size=64)
    
    # Write KV-cache entry
    mgr.request_write(addr=0x10000, data=bytes(64))
    
    # Check coherence state
    state = mgr.get_state(addr=0x10000)
    print(f"State: {state.name}")
    
    # Get statistics
    stats = mgr.get_statistics()
    print(f"Hit rate: {stats['hit_rate']:.2%}")
"""

import ctypes
from enum import IntEnum
from typing import Optional, List, Dict, Tuple
import os

# Load the shared library
_lib_path = os.path.join(os.path.dirname(__file__), "../build/libcxlspeckv.so")
if not os.path.exists(_lib_path):
    _lib_path = "libcxlspeckv.so"  # Try system path

try:
    _lib = ctypes.CDLL(_lib_path)
except OSError as e:
    raise ImportError(f"Failed to load libcxlspeckv.so: {e}")

# Enum definitions matching C++
class CoherenceState(IntEnum):
    """Cache coherence states (MESI protocol)"""
    INVALID = 0
    SHARED = 1
    EXCLUSIVE = 2
    MODIFIED = 3

class MemoryTier(IntEnum):
    """Memory tier locations"""
    L1_GPU = 0
    L2_PREFETCH = 1
    L3_CXL = 2

# C structures
class _Statistics(ctypes.Structure):
    """C structure for statistics"""
    _fields_ = [
        ("total_reads", ctypes.c_uint64),
        ("total_writes", ctypes.c_uint64),
        ("coherence_ops", ctypes.c_uint64),
        ("invalidations_sent", ctypes.c_uint64),
        ("writebacks_performed", ctypes.c_uint64),
        ("directory_hits", ctypes.c_uint64),
        ("directory_misses", ctypes.c_uint64),
    ]

# C API function signatures
_lib.coherence_manager_create.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
_lib.coherence_manager_create.restype = ctypes.c_void_p

_lib.coherence_manager_destroy.argtypes = [ctypes.c_void_p]
_lib.coherence_manager_destroy.restype = None

_lib.coherence_manager_request_read.argtypes = [
    ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t
]
_lib.coherence_manager_request_read.restype = ctypes.c_bool

_lib.coherence_manager_request_write.argtypes = [
    ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t
]
_lib.coherence_manager_request_write.restype = ctypes.c_bool

_lib.coherence_manager_invalidate.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
_lib.coherence_manager_invalidate.restype = ctypes.c_bool

_lib.coherence_manager_writeback.argtypes = [
    ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t
]
_lib.coherence_manager_writeback.restype = ctypes.c_bool

_lib.coherence_manager_flush_all.argtypes = [ctypes.c_void_p]
_lib.coherence_manager_flush_all.restype = ctypes.c_bool

_lib.coherence_manager_get_state.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
_lib.coherence_manager_get_state.restype = ctypes.c_int

_lib.coherence_manager_get_tier.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
_lib.coherence_manager_get_tier.restype = ctypes.c_int

_lib.coherence_manager_promote_to_l1.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
_lib.coherence_manager_promote_to_l1.restype = ctypes.c_bool

_lib.coherence_manager_demote_to_l3.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
_lib.coherence_manager_demote_to_l3.restype = ctypes.c_bool

_lib.coherence_manager_batch_invalidate.argtypes = [
    ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t
]
_lib.coherence_manager_batch_invalidate.restype = ctypes.c_bool

_lib.coherence_manager_get_statistics.argtypes = [
    ctypes.c_void_p, ctypes.POINTER(_Statistics)
]
_lib.coherence_manager_get_statistics.restype = None

_lib.coherence_manager_reset_statistics.argtypes = [ctypes.c_void_p]
_lib.coherence_manager_reset_statistics.restype = None


class CoherenceManager:
    """
    Python wrapper for C++ CoherenceManager
    
    Manages cache coherence for CXL-SpecKV KV-cache entries.
    Coordinates with FPGA directory controller to maintain coherent
    memory semantics across GPU and CXL memory tiers.
    """
    
    def __init__(self, device_path: str = "/dev/speckv0", cache_line_size: int = 64):
        """
        Initialize coherence manager
        
        Args:
            device_path: Path to kernel device
            cache_line_size: Cache line size in bytes (default 64)
        """
        self._handle = _lib.coherence_manager_create(
            device_path.encode('utf-8'),
            cache_line_size
        )
        if not self._handle:
            raise RuntimeError("Failed to create CoherenceManager")
    
    def __del__(self):
        """Cleanup"""
        if hasattr(self, '_handle') and self._handle:
            _lib.coherence_manager_destroy(self._handle)
    
    def __enter__(self):
        """Context manager entry"""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.flush_all()
        return False
    
    def request_read(self, addr: int, size: int) -> Optional[bytes]:
        """
        Request coherent read access to cache line
        
        Args:
            addr: Virtual address to read
            size: Number of bytes to read
            
        Returns:
            Bytes read, or None on error
        """
        buffer = ctypes.create_string_buffer(size)
        success = _lib.coherence_manager_request_read(
            self._handle, addr, buffer, size
        )
        return bytes(buffer) if success else None
    
    def request_write(self, addr: int, data: bytes) -> bool:
        """
        Request coherent write access to cache line
        
        This will:
        1. Send request to FPGA home agent
        2. FPGA sends CXL.cache invalidations to other sharers
        3. FPGA writes to CXL memory
        4. Local directory updated to MODIFIED state
        
        Args:
            addr: Virtual address to write
            data: Bytes to write
            
        Returns:
            True on success
        """
        return _lib.coherence_manager_request_write(
            self._handle, addr, data, len(data)
        )
    
    def invalidate(self, addr: int) -> bool:
        """
        Invalidate cache line
        
        Args:
            addr: Virtual address to invalidate
            
        Returns:
            True on success
        """
        return _lib.coherence_manager_invalidate(self._handle, addr)
    
    def writeback(self, addr: int, data: bytes) -> bool:
        """
        Writeback modified cache line to CXL memory
        
        Args:
            addr: Virtual address to writeback
            data: Data to writeback
            
        Returns:
            True on success
        """
        return _lib.coherence_manager_writeback(
            self._handle, addr, data, len(data)
        )
    
    def flush_all(self) -> bool:
        """
        Flush all modified cache lines
        
        Writes back all entries in MODIFIED state to CXL memory.
        Useful before shutdown or when ensuring data persistence.
        
        Returns:
            True on success
        """
        return _lib.coherence_manager_flush_all(self._handle)
    
    def get_state(self, addr: int) -> CoherenceState:
        """
        Get coherence state of cache line
        
        Args:
            addr: Virtual address to query
            
        Returns:
            CoherenceState enum value
        """
        state = _lib.coherence_manager_get_state(self._handle, addr)
        return CoherenceState(state)
    
    def get_tier(self, addr: int) -> MemoryTier:
        """
        Get memory tier of cache line
        
        Args:
            addr: Virtual address to query
            
        Returns:
            MemoryTier enum value
        """
        tier = _lib.coherence_manager_get_tier(self._handle, addr)
        return MemoryTier(tier)
    
    def is_valid(self, addr: int) -> bool:
        """Check if address is in valid state"""
        return self.get_state(addr) != CoherenceState.INVALID
    
    def is_modified(self, addr: int) -> bool:
        """Check if address is in modified state"""
        return self.get_state(addr) == CoherenceState.MODIFIED
    
    def promote_to_l1(self, addr: int) -> bool:
        """
        Promote cache line from L3 (CXL) to L1 (GPU)
        
        Args:
            addr: Virtual address to promote
            
        Returns:
            True on success
        """
        return _lib.coherence_manager_promote_to_l1(self._handle, addr)
    
    def demote_to_l3(self, addr: int) -> bool:
        """
        Demote cache line from L1 (GPU) to L3 (CXL)
        
        Writes back if modified before demotion.
        
        Args:
            addr: Virtual address to demote
            
        Returns:
            True on success
        """
        return _lib.coherence_manager_demote_to_l3(self._handle, addr)
    
    def batch_invalidate(self, addrs: List[int]) -> bool:
        """
        Invalidate multiple cache lines in batch
        
        More efficient than individual invalidations.
        
        Args:
            addrs: List of virtual addresses to invalidate
            
        Returns:
            True on success
        """
        addr_array = (ctypes.c_uint64 * len(addrs))(*addrs)
        return _lib.coherence_manager_batch_invalidate(
            self._handle, addr_array, len(addrs)
        )
    
    def get_statistics(self) -> Dict[str, any]:
        """
        Get coherence statistics
        
        Returns:
            Dictionary with statistics:
                - total_reads: Total read operations
                - total_writes: Total write operations
                - coherence_ops: Total coherence operations
                - invalidations_sent: Number of invalidations sent
                - writebacks_performed: Number of writebacks
                - directory_hits: Directory lookup hits
                - directory_misses: Directory lookup misses
                - hit_rate: Directory hit rate (0.0 - 1.0)
        """
        stats = _Statistics()
        _lib.coherence_manager_get_statistics(self._handle, ctypes.byref(stats))
        
        total = stats.directory_hits + stats.directory_misses
        hit_rate = stats.directory_hits / total if total > 0 else 0.0
        
        return {
            'total_reads': stats.total_reads,
            'total_writes': stats.total_writes,
            'coherence_ops': stats.coherence_ops,
            'invalidations_sent': stats.invalidations_sent,
            'writebacks_performed': stats.writebacks_performed,
            'directory_hits': stats.directory_hits,
            'directory_misses': stats.directory_misses,
            'hit_rate': hit_rate,
        }
    
    def reset_statistics(self):
        """Reset all statistics counters to zero"""
        _lib.coherence_manager_reset_statistics(self._handle)


# Example usage
if __name__ == "__main__":
    print("CXL-SpecKV Coherence Manager Python Demo")
    print("=" * 60)
    
    try:
        with CoherenceManager() as mgr:
            print("\n[OK] CoherenceManager initialized")
            
            # Test read
            addr = 0x10000
            print(f"\nReading from address 0x{addr:X}")
            data = mgr.request_read(addr, 64)
            if data:
                print(f"   Read {len(data)} bytes")
                state = mgr.get_state(addr)
                print(f"   State: {state.name}")
                tier = mgr.get_tier(addr)
                print(f"   Tier: {tier.name}")
            
            # Test write
            print(f"\nWriting to address 0x{addr:X}")
            test_data = bytes([0xAB] * 64)
            success = mgr.request_write(addr, test_data)
            if success:
                print(f"   Write succeeded")
                state = mgr.get_state(addr)
                print(f"   State: {state.name}")
            
            # Test promotion
            addr2 = 0x20000
            print(f"\nPromoting 0x{addr2:X} to L1")
            mgr.promote_to_l1(addr2)
            tier = mgr.get_tier(addr2)
            print(f"   Tier: {tier.name}")
            
            # Get statistics
            print("\nStatistics:")
            stats = mgr.get_statistics()
            for key, value in stats.items():
                if key == 'hit_rate':
                    print(f"   {key}: {value:.2%}")
                else:
                    print(f"   {key}: {value}")
            
            print("\nDemo completed")
            
    except Exception as e:
        print(f"\nError: {e}")
