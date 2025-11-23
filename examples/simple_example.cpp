// Simple example demonstrating CXL-SpecKV usage
#include "../src/cxl_speckv_system.h"
#include <iostream>

int main() {
    using namespace cxlspeckv;
    
    // Create and configure system
    CXLSpecKVSystem system;
    CXLSpecKVSystem::SystemConfig config;
    
    // Initialize
    if (!system.initialize(config)) {
        std::cerr << "Initialization failed\n";
        return 1;
    }
    
    // Use the system
    std::vector<uint32_t> tokens = {1, 2, 3, 4, 5};
    uint32_t next = system.generate_next_token(tokens, 0);
    std::cout << "Next token: " << next << std::endl;
    
    return 0;
}

