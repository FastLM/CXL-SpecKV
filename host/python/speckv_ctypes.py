# host/python/speckv_ctypes.py
import ctypes
from ctypes import (c_uint32, c_uint16, c_uint64, c_size_t, 
                    c_int32, c_void_p, c_char_p, c_int)


class SpeckvLib:
    def __init__(self, path: str, dev_path: str = "/dev/speckv0"):
        self.lib = ctypes.CDLL(path)
        
        # typedef uint64_t speckv_handle_t;
        self.handle_t = c_uint64
        
        # speckv_init
        self.lib.speckv_init.argtypes = [c_char_p]
        self.lib.speckv_init.restype = c_int
        
        # speckv_finalize
        self.lib.speckv_finalize.argtypes = []
        self.lib.speckv_finalize.restype = None
        
        # speckv_alloc
        class AllocHint(ctypes.Structure):
            _fields_ = [("preferred_node", c_uint32),
                        ("reserved", c_uint32)]
        self.AllocHint = AllocHint
        
        self.lib.speckv_alloc.argtypes = [c_size_t,
                                          ctypes.POINTER(AllocHint),
                                          ctypes.POINTER(self.handle_t)]
        self.lib.speckv_alloc.restype = c_int
        
        # speckv_free
        self.lib.speckv_free.argtypes = [self.handle_t]
        self.lib.speckv_free.restype = c_int
        
        # speckv_access
        self.lib.speckv_access.argtypes = [self.handle_t,
                                          c_uint64,
                                          c_size_t,
                                          ctypes.POINTER(c_void_p)]
        self.lib.speckv_access.restype = c_int
        
        # speckv_prefetch
        self.lib.speckv_prefetch.argtypes = [c_uint32, c_uint16, c_uint32,
                                            c_uint32,
                                            ctypes.POINTER(c_int32),
                                            c_uint32]
        self.lib.speckv_prefetch.restype = c_int
        
        # speckv_set_prefetch_depth
        self.lib.speckv_set_prefetch_depth.argtypes = [c_uint32]
        self.lib.speckv_set_prefetch_depth.restype = c_int
        
        # speckv_set_compression_scheme
        self.lib.speckv_set_compression_scheme.argtypes = [c_int]
        self.lib.speckv_set_compression_scheme.restype = c_int
        
        # 初始化
        ret = self.lib.speckv_init(dev_path.encode("ascii"))
        if ret != 0:
            raise RuntimeError(f"speckv_init failed: {ret}")
    
    def alloc(self, bytes_needed, preferred_node=0):
        hint = self.AllocHint(preferred_node, 0)
        handle = self.handle_t()
        ret = self.lib.speckv_alloc(bytes_needed, ctypes.byref(hint), ctypes.byref(handle))
        if ret != 0:
            raise RuntimeError(f"speckv_alloc failed: {ret}")
        return handle.value
    
    def free(self, handle):
        ret = self.lib.speckv_free(handle)
        if ret != 0:
            raise RuntimeError(f"speckv_free failed: {ret}")
    
    def access(self, handle, offset, length):
        gpu_ptr = c_void_p()
        ret = self.lib.speckv_access(handle, offset, length, ctypes.byref(gpu_ptr))
        if ret != 0:
            raise RuntimeError(f"speckv_access failed: {ret}")
        return gpu_ptr.value
    
    def prefetch(self, req_id, layer, cur_pos, depth_k, tokens):
        arr = (c_int32 * len(tokens))(*tokens)
        ret = self.lib.speckv_prefetch(req_id, layer, cur_pos, depth_k, arr, len(tokens))
        if ret != 0:
            raise RuntimeError(f"speckv_prefetch failed: {ret}")
    
    def set_prefetch_depth(self, depth_k):
        ret = self.lib.speckv_set_prefetch_depth(depth_k)
        if ret != 0:
            raise RuntimeError(f"speckv_set_prefetch_depth failed: {ret}")
    
    def set_compression_scheme(self, scheme):
        ret = self.lib.speckv_set_compression_scheme(scheme)
        if ret != 0:
            raise RuntimeError(f"speckv_set_compression_scheme failed: {ret}")

