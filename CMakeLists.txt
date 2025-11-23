cmake_minimum_required(VERSION 3.18)
project(CXL-SpecKV VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

# Find CUDA
find_package(CUDA REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CUDA_INCLUDE_DIRS})

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/host/include)
include_directories(${CMAKE_SOURCE_DIR}/driver/uapi)

# Source files (legacy components)
set(LEGACY_SOURCES
    src/cxl_memory/cxl_memory_manager.cpp
    src/prefetcher/speculative_prefetcher.cpp
    src/prefetcher/lstm_predictor.cpp
    src/fpga_engine/cache_engine.cpp
    src/integration/memory_allocator.cpp
    src/cxl_speckv_system.cpp
    src/utils/address_translation.cpp
)

# Host-side sources (new architecture)
set(HOST_SOURCES
    host/src/speckv_driver.cpp
    host/src/speckv_allocator.cpp
    host/src/speckv_c_api.cpp
)

set(SOURCES ${LEGACY_SOURCES} ${HOST_SOURCES})

# Executable
add_executable(cxlspeckv_demo src/main.cpp ${SOURCES})
target_link_libraries(cxlspeckv_demo ${CUDA_LIBRARIES})

# Create shared library
add_library(cxlspeckv SHARED ${SOURCES})
target_link_libraries(cxlspeckv ${CUDA_LIBRARIES})

# Python bindings
find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(cxlspeckv_py 
        src/python_bindings/bindings.cpp
    )
    target_link_libraries(cxlspeckv_py PRIVATE cxlspeckv)
endif()

# Install
install(TARGETS cxlspeckv DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

