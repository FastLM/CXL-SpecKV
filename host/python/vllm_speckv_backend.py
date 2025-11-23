# host/python/vllm_speckv_backend.py
from typing import Any, Dict, List, Optional
import numpy as np
import ctypes
from .speckv_ctypes import SpeckvLib


class CxlSpeckvKVAllocator:
    def __init__(self,
                 lib_path: str,
                 dev_path: str = "/dev/speckv0",
                 page_size: int = 4096):
        self._speckv = SpeckvLib(lib_path, dev_path)
        self._page_size = page_size
        self._handle: Optional[int] = None
        self._req_id_counter = 1
        self._req_state: Dict[int, Dict[str, Any]] = {}
        
        # Model configuration (will be set by allocate)
        self._num_layers = 0
        self._num_heads = 0
        self._num_tokens = 0
        self._head_dim = 0
        self._bytes_per_element = 0

    def allocate(self,
                 num_tokens: int,
                 num_layers: int,
                 num_heads: int,
                 head_dim: int,
                 bytes_per_element: int):
        """Allocate KV cache for a request"""
        self._num_tokens = num_tokens
        self._num_layers = num_layers
        self._num_heads = num_heads
        self._head_dim = head_dim
        self._bytes_per_element = bytes_per_element
        
        total_bytes = (num_tokens * num_layers * num_heads *
                      head_dim * bytes_per_element * 2)  # K+V
        
        self._handle = self._speckv.alloc(total_bytes, preferred_node=0)
        return self._handle

    def get_kv_ptr(self,
                   req_id: int,
                   layer: int,
                   head: int,
                   pos: int,
                   kind: int,
                   entry_bytes: int) -> int:
        """Get GPU pointer for KV cache entry"""
        # 这里需要和 C++ 的 encode_virt_page 保持一致 mapping
        offset = self._calc_offset(req_id, layer, head, pos, kind, entry_bytes)
        
        import ctypes
        gpu_ptr = ctypes.c_void_p()
        ret = self._speckv.lib.speckv_access(self._handle,
                                             offset,
                                             entry_bytes,
                                             ctypes.byref(gpu_ptr))
        if ret != 0:
            raise RuntimeError(f"speckv_access failed: {ret}")
        return gpu_ptr.value  # 这是 GPU address (在 CUDA 里可以 wrap 成 tensor)

    def prefetch_step(self,
                      req_id: int,
                      layer: int,
                      cur_pos: int,
                      recent_tokens: List[int],
                      depth_k: int = 4):
        """Issue prefetch for next tokens"""
        hist_len = len(recent_tokens)
        import ctypes
        arr = (ctypes.c_int32 * hist_len)(*recent_tokens)
        ret = self._speckv.lib.speckv_prefetch(
            req_id,
            layer,
            cur_pos,
            depth_k,
            arr,
            hist_len
        )
        if ret != 0:
            raise RuntimeError(f"speckv_prefetch failed: {ret}")

    def _calc_offset(self,
                     req_id: int,
                     layer: int,
                     head: int,
                     pos: int,
                     kind: int,
                     entry_bytes: int) -> int:
        """Calculate offset for KV entry - must match C++ encode_virt_page"""
        # Layout: [req][layer][kind(K/V)][pos][head]
        # This should match the encoding in SpeckvAllocator::encode_virt_page
        return (
            (((req_id * self._num_layers + layer) * 2 + kind)
             * self._num_tokens + pos) * self._num_heads + head
        ) * entry_bytes


# Example usage in vLLM decode loop
def decode_step_example(model, kv_allocator: CxlSpeckvKVAllocator, state, ...):
    """Example decode step with prefetching"""
    # Forward pass
    logits = model(...)
    
    # Update token
    new_token = logits.argmax(-1)
    state.tokens.append(int(new_token))
    
    # Record req_id / pos
    req_id = state.req_id
    cur_pos = state.cur_pos
    
    # Prefetch for each layer
    last_tokens = state.tokens[-16:] if len(state.tokens) >= 16 else state.tokens
    for l in range(model.num_layers):
        kv_allocator.prefetch_step(
            req_id=req_id,
            layer=l,
            cur_pos=cur_pos,
            recent_tokens=last_tokens,
            depth_k=4,
        )
    
    state.cur_pos += 1
    return logits, new_token

