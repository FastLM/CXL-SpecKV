#!/usr/bin/env python3
# tests/test_python.py
# Test Python integration with real CXL-SpecKV API
import sys
import os

# Add host/python to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../host/python'))

try:
    from speckv_ctypes import SpeckvLib
except ImportError as e:
    print(f"Failed to import SpeckvLib: {e}")
    print("Make sure libcxlspeckv.so is built and in LD_LIBRARY_PATH")
    sys.exit(1)

def test_init():
    """Test library initialization"""
    print("Testing library initialization...")
    try:
        lib = SpeckvLib("./build/libcxlspeckv.so", "/dev/speckv0")
        print("  Initialization successful")
        return True
    except Exception as e:
        print(f"  Initialization failed: {e}")
        return False

def test_alloc_free():
    """Test memory allocation and deallocation"""
    print("Testing alloc/free...")
    try:
        lib = SpeckvLib("./build/libcxlspeckv.so", "/dev/speckv0")
        
        # Allocate 1MB
        handle = lib.alloc(1024 * 1024)
        print(f"  Allocated handle: {handle}")
        
        # Free
        lib.free(handle)
        print("  Free successful")
        return True
    except Exception as e:
        print(f"  Alloc/free failed: {e}")
        return False

def test_access():
    """Test memory access"""
    print("Testing memory access...")
    try:
        lib = SpeckvLib("./build/libcxlspeckv.so", "/dev/speckv0")
        
        handle = lib.alloc(4096)
        gpu_ptr = lib.access(handle, 0, 4096)
        print(f"  Access successful, GPU ptr: {hex(gpu_ptr)}")
        
        lib.free(handle)
        return True
    except Exception as e:
        print(f"  Access failed: {e}")
        return False

def test_prefetch():
    """Test prefetch functionality"""
    print("Testing prefetch...")
    try:
        lib = SpeckvLib("./build/libcxlspeckv.so", "/dev/speckv0")
        
        tokens = list(range(1, 17))  # [1, 2, ..., 16]
        lib.prefetch(
            req_id=1,
            layer=0,
            cur_pos=100,
            depth_k=4,
            tokens=tokens
        )
        print("  Prefetch submitted successfully")
        return True
    except Exception as e:
        print(f"  Prefetch failed: {e}")
        return False

def test_params():
    """Test parameter configuration"""
    print("Testing parameter configuration...")
    try:
        lib = SpeckvLib("./build/libcxlspeckv.so", "/dev/speckv0")
        
        # Set prefetch depth
        lib.set_prefetch_depth(8)
        print("  Set prefetch depth to 8")
        
        # Set compression scheme
        lib.set_compression_scheme(2)  # INT8_DELTA_RLE
        print("  Set compression scheme to INT8_DELTA_RLE")
        
        return True
    except Exception as e:
        print(f"  Parameter configuration failed: {e}")
        return False

def main():
    print("=== Python Integration Test Suite ===\n")
    
    results = [
        test_init(),
        test_alloc_free(),
        test_access(),
        test_prefetch(),
        test_params()
    ]
    
    if all(results):
        print("\n=== All tests passed ===")
        return 0
    else:
        print("\n=== Some tests failed ===")
        return 1

if __name__ == '__main__':
    sys.exit(main())

