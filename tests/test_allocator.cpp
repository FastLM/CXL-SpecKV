// tests/test_allocator.cpp
// Test memory allocator functionality
#include "../host/include/speckv_allocator.hpp"
#include "../host/include/speckv_driver.hpp"
#include <iostream>
#include <cassert>

#define TEST_PASSED 0
#define TEST_FAILED 1

int test_basic_allocation() {
    std::cout << "Testing basic allocation...\n";
    
    try {
        SpeckvDriver driver("/dev/speckv0");
        if (!driver.ok()) {
            std::cerr << "Failed to open driver\n";
            return TEST_FAILED;
        }
        
        SpeckvAllocator allocator(&driver);
        
        // Allocate 1MB
        uint64_t handle = allocator.alloc(1024 * 1024);
        if (handle == 0) {
            std::cerr << "Allocation failed\n";
            return TEST_FAILED;
        }
        
        std::cout << "  Allocated handle: " << handle << "\n";
        
        // Free
        allocator.free(handle);
        std::cout << "  Free successful\n";
        
        return TEST_PASSED;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return TEST_FAILED;
    }
}

int test_multiple_allocations() {
    std::cout << "Testing multiple allocations...\n";
    
    try {
        SpeckvDriver driver("/dev/speckv0");
        SpeckvAllocator allocator(&driver);
        
        const int num_allocs = 10;
        uint64_t handles[num_allocs];
        
        // Allocate multiple blocks
        for (int i = 0; i < num_allocs; i++) {
            handles[i] = allocator.alloc(4096 * (i + 1));
            if (handles[i] == 0) {
                std::cerr << "Allocation " << i << " failed\n";
                return TEST_FAILED;
            }
        }
        
        std::cout << "  Allocated " << num_allocs << " blocks\n";
        
        // Free all
        for (int i = 0; i < num_allocs; i++) {
            allocator.free(handles[i]);
        }
        
        std::cout << "  All freed successfully\n";
        
        return TEST_PASSED;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return TEST_FAILED;
    }
}

int test_access() {
    std::cout << "Testing memory access...\n";
    
    try {
        SpeckvDriver driver("/dev/speckv0");
        SpeckvAllocator allocator(&driver);
        
        uint64_t handle = allocator.alloc(4096);
        if (handle == 0) {
            return TEST_FAILED;
        }
        
        // Access at different offsets
        void *ptr1 = allocator.access(handle, 0, 1024);
        void *ptr2 = allocator.access(handle, 1024, 1024);
        void *ptr3 = allocator.access(handle, 2048, 1024);
        
        if (!ptr1 || !ptr2 || !ptr3) {
            std::cerr << "Access failed\n";
            allocator.free(handle);
            return TEST_FAILED;
        }
        
        std::cout << "  Access successful: " << ptr1 << ", " << ptr2 << ", " << ptr3 << "\n";
        
        allocator.free(handle);
        return TEST_PASSED;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return TEST_FAILED;
    }
}

int test_prefetch() {
    std::cout << "Testing prefetch...\n";
    
    try {
        SpeckvDriver driver("/dev/speckv0");
        SpeckvAllocator allocator(&driver);
        
        int32_t tokens[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        
        allocator.prefetch(
            1,      // req_id
            0,      // layer
            100,    // cur_pos
            4,      // depth_k
            tokens,
            16      // history_len
        );
        
        std::cout << "  Prefetch submitted\n";
        
        return TEST_PASSED;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return TEST_FAILED;
    }
}

int main() {
    std::cout << "=== Allocator Test Suite ===\n";
    
    int result1 = test_basic_allocation();
    int result2 = test_multiple_allocations();
    int result3 = test_access();
    int result4 = test_prefetch();
    
    if (result1 == TEST_PASSED && result2 == TEST_PASSED && 
        result3 == TEST_PASSED && result4 == TEST_PASSED) {
        std::cout << "=== All tests passed ===\n";
        return 0;
    } else {
        std::cout << "=== Tests failed ===\n";
        return 1;
    }
}

