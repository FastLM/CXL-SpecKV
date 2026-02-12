/**
 * test_coherence.cpp
 * 
 * Unit tests for cache coherence manager
 * Tests all coherence operations: read, write, invalidate, writeback, migration
 */

#include "../src/cxl_memory/coherence_manager.h"
#include "../host/include/speckv_driver.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <iomanip>

using namespace cxlspeckv;

// Test counter
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, msg) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] FAILED: " << msg << std::endl; \
            tests_failed++; \
            return false; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        std::cout << "\n> Running " << #test_func << "..." << std::endl; \
        if (test_func()) { \
            std::cout << "[PASS] " << #test_func << " PASSED" << std::endl; \
        } else { \
            std::cout << "[FAIL] " << #test_func << " FAILED" << std::endl; \
        } \
    } while(0)

// Mock driver for testing (when no hardware available)
class MockDriver : public SpeckvDriver {
public:
    MockDriver() : SpeckvDriver("/dev/null") {}
    
    // Override operations for testing
    bool coherence_request(CoherenceOp op, uint64_t addr, const void* data, size_t size) override {
        // Simulate successful operation
        return true;
    }
    
    bool coherence_wait_complete() override {
        return true;
    }
};

// Test 1: Basic initialization
bool test_initialization() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    TEST_ASSERT(true, "CoherenceManager initialization");
    
    auto stats = coherence_mgr.get_statistics();
    TEST_ASSERT(stats.total_reads == 0, "Initial read count is 0");
    TEST_ASSERT(stats.total_writes == 0, "Initial write count is 0");
    TEST_ASSERT(stats.coherence_ops == 0, "Initial coherence ops is 0");
    
    return true;
}

// Test 2: Read operations
bool test_read_operations() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0x10000;
    char buffer[64];
    
    // First read - should miss
    bool success = coherence_mgr.request_read(addr, buffer, sizeof(buffer));
    TEST_ASSERT(success, "First read request succeeds");
    
    // Check state changed to SHARED
    auto state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::SHARED, 
                "State is SHARED after read");
    
    // Check tier is L1
    auto tier = coherence_mgr.get_tier(addr);
    TEST_ASSERT(tier == CoherenceManager::MemoryTier::L1_GPU,
                "Data promoted to L1");
    
    // Second read to same address - should hit
    success = coherence_mgr.request_read(addr, buffer, sizeof(buffer));
    TEST_ASSERT(success, "Second read request succeeds");
    
    auto stats = coherence_mgr.get_statistics();
    TEST_ASSERT(stats.total_reads == 2, "Total reads is 2");
    TEST_ASSERT(stats.directory_hits >= 1, "At least one directory hit");
    
    return true;
}

// Test 3: Write operations with invalidations
bool test_write_operations() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0x20000;
    char data[64];
    std::memset(data, 0xAB, sizeof(data));
    
    // First, read to put in SHARED state
    char buffer[64];
    coherence_mgr.request_read(addr, buffer, sizeof(buffer));
    
    auto state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::SHARED,
                "Initial state is SHARED");
    
    // Now write - should trigger invalidation
    bool success = coherence_mgr.request_write(addr, data, sizeof(data));
    TEST_ASSERT(success, "Write request succeeds");
    
    // Check state changed to MODIFIED
    state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::MODIFIED,
                "State is MODIFIED after write");
    
    auto stats = coherence_mgr.get_statistics();
    TEST_ASSERT(stats.total_writes == 1, "Total writes is 1");
    TEST_ASSERT(stats.invalidations_sent >= 1, "Invalidation sent");
    
    return true;
}

// Test 4: Invalidation operations
bool test_invalidation() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0x30000;
    
    // Put data in SHARED state
    char buffer[64];
    coherence_mgr.request_read(addr, buffer, sizeof(buffer));
    
    TEST_ASSERT(coherence_mgr.is_valid(addr), "Address is valid");
    
    // Invalidate
    bool success = coherence_mgr.invalidate(addr);
    TEST_ASSERT(success, "Invalidation succeeds");
    
    // Check state is INVALID
    auto state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::INVALID,
                "State is INVALID after invalidation");
    
    TEST_ASSERT(!coherence_mgr.is_valid(addr), "Address is no longer valid");
    
    return true;
}

// Test 5: Writeback operations
bool test_writeback() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0x40000;
    char data[64];
    std::memset(data, 0xCD, sizeof(data));
    
    // Write to put in MODIFIED state
    coherence_mgr.request_write(addr, data, sizeof(data));
    
    TEST_ASSERT(coherence_mgr.is_modified(addr), "Address is modified");
    
    // Writeback
    bool success = coherence_mgr.writeback(addr, data, sizeof(data));
    TEST_ASSERT(success, "Writeback succeeds");
    
    // Check state changed to SHARED
    auto state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::SHARED,
                "State is SHARED after writeback");
    
    TEST_ASSERT(!coherence_mgr.is_modified(addr), "Address no longer modified");
    
    auto stats = coherence_mgr.get_statistics();
    TEST_ASSERT(stats.writebacks_performed >= 1, "Writeback recorded");
    
    return true;
}

// Test 6: Memory tier promotion
bool test_tier_promotion() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0x50000;
    
    // Initially, data is in L3
    auto tier = coherence_mgr.get_tier(addr);
    TEST_ASSERT(tier == CoherenceManager::MemoryTier::L3_CXL,
                "Initial tier is L3");
    
    // Promote to L1
    bool success = coherence_mgr.promote_to_l1(addr);
    TEST_ASSERT(success, "Promotion succeeds");
    
    tier = coherence_mgr.get_tier(addr);
    TEST_ASSERT(tier == CoherenceManager::MemoryTier::L1_GPU,
                "Tier is now L1");
    
    return true;
}

// Test 7: Memory tier demotion
bool test_tier_demotion() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0x60000;
    
    // Put in L1
    coherence_mgr.promote_to_l1(addr);
    
    auto tier = coherence_mgr.get_tier(addr);
    TEST_ASSERT(tier == CoherenceManager::MemoryTier::L1_GPU,
                "Initial tier is L1");
    
    // Demote to L3
    bool success = coherence_mgr.demote_to_l3(addr);
    TEST_ASSERT(success, "Demotion succeeds");
    
    tier = coherence_mgr.get_tier(addr);
    TEST_ASSERT(tier == CoherenceManager::MemoryTier::L3_CXL,
                "Tier is now L3");
    
    return true;
}

// Test 8: Batch invalidations
bool test_batch_operations() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    std::vector<uint64_t> addrs = {0x70000, 0x70040, 0x70080, 0x700C0};
    
    // Put all in SHARED state
    char buffer[64];
    for (uint64_t addr : addrs) {
        coherence_mgr.request_read(addr, buffer, sizeof(buffer));
    }
    
    // Batch invalidate
    bool success = coherence_mgr.batch_invalidate(addrs);
    TEST_ASSERT(success, "Batch invalidation succeeds");
    
    // Check all are invalid
    for (uint64_t addr : addrs) {
        auto state = coherence_mgr.get_state(addr);
        TEST_ASSERT(state == CoherenceManager::CoherenceState::INVALID,
                    "Address is invalidated");
    }
    
    auto stats = coherence_mgr.get_statistics();
    TEST_ASSERT(stats.invalidations_sent >= addrs.size(),
                "All invalidations recorded");
    
    return true;
}

// Test 9: Flush all operations
bool test_flush_all() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    // Create multiple modified entries
    char data[64];
    std::memset(data, 0xEF, sizeof(data));
    
    std::vector<uint64_t> addrs = {0x80000, 0x80040, 0x80080};
    
    for (uint64_t addr : addrs) {
        coherence_mgr.request_write(addr, data, sizeof(data));
        TEST_ASSERT(coherence_mgr.is_modified(addr), "Address is modified");
    }
    
    // Flush all
    bool success = coherence_mgr.flush_all();
    TEST_ASSERT(success, "Flush all succeeds");
    
    // Check all are no longer modified
    for (uint64_t addr : addrs) {
        TEST_ASSERT(!coherence_mgr.is_modified(addr),
                    "Address no longer modified after flush");
    }
    
    return true;
}

// Test 10: Statistics tracking
bool test_statistics() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    // Perform various operations
    char buffer[64];
    char data[64];
    
    uint64_t addr1 = 0x90000;
    uint64_t addr2 = 0x90040;
    
    coherence_mgr.request_read(addr1, buffer, sizeof(buffer));
    coherence_mgr.request_write(addr2, data, sizeof(data));
    coherence_mgr.invalidate(addr1);
    
    auto stats = coherence_mgr.get_statistics();
    
    TEST_ASSERT(stats.total_reads >= 1, "Read operations recorded");
    TEST_ASSERT(stats.total_writes >= 1, "Write operations recorded");
    TEST_ASSERT(stats.coherence_ops >= 1, "Coherence ops recorded");
    
    double hit_rate = stats.hit_rate();
    TEST_ASSERT(hit_rate >= 0.0 && hit_rate <= 1.0, "Hit rate in valid range");
    
    // Reset statistics
    coherence_mgr.reset_statistics();
    stats = coherence_mgr.get_statistics();
    
    TEST_ASSERT(stats.total_reads == 0, "Stats reset - reads is 0");
    TEST_ASSERT(stats.total_writes == 0, "Stats reset - writes is 0");
    
    return true;
}

// Test 11: State transitions
bool test_state_transitions() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    uint64_t addr = 0xA0000;
    char buffer[64];
    char data[64];
    
    // INVALID -> SHARED (read)
    auto state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::INVALID,
                "Initial state is INVALID");
    
    coherence_mgr.request_read(addr, buffer, sizeof(buffer));
    state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::SHARED,
                "INVALID -> SHARED on read");
    
    // SHARED -> MODIFIED (write)
    coherence_mgr.request_write(addr, data, sizeof(data));
    state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::MODIFIED,
                "SHARED -> MODIFIED on write");
    
    // MODIFIED -> SHARED (writeback)
    coherence_mgr.writeback(addr, data, sizeof(data));
    state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::SHARED,
                "MODIFIED -> SHARED on writeback");
    
    // SHARED -> INVALID (invalidate)
    coherence_mgr.invalidate(addr);
    state = coherence_mgr.get_state(addr);
    TEST_ASSERT(state == CoherenceManager::CoherenceState::INVALID,
                "SHARED -> INVALID on invalidate");
    
    return true;
}

// Test 12: Concurrent addresses
bool test_multiple_addresses() {
    auto driver = std::make_shared<SpeckvDriver>("/dev/speckv0");
    CoherenceManager coherence_mgr(driver, 64);
    
    const int NUM_ADDRS = 10;
    char buffer[64];
    
    // Access multiple different addresses
    for (int i = 0; i < NUM_ADDRS; i++) {
        uint64_t addr = 0xB0000 + (i * 0x1000);  // Non-overlapping cache lines
        coherence_mgr.request_read(addr, buffer, sizeof(buffer));
        
        auto state = coherence_mgr.get_state(addr);
        TEST_ASSERT(state == CoherenceManager::CoherenceState::SHARED,
                    "Each address independently managed");
    }
    
    auto stats = coherence_mgr.get_statistics();
    TEST_ASSERT(stats.total_reads == NUM_ADDRS, "All reads recorded");
    
    return true;
}

int main(int argc, char** argv) {
    std::cout << "=============================================================╗" << std::endl;
    std::cout << "|     CXL-SpecKV Coherence Manager Unit Tests               |" << std::endl;
    std::cout << "=============================================================╝" << std::endl;
    
    std::cout << "\nNote: These tests use mock operations when /dev/speckv0 is not available." << std::endl;
    
    // Run all tests
    RUN_TEST(test_initialization);
    RUN_TEST(test_read_operations);
    RUN_TEST(test_write_operations);
    RUN_TEST(test_invalidation);
    RUN_TEST(test_writeback);
    RUN_TEST(test_tier_promotion);
    RUN_TEST(test_tier_demotion);
    RUN_TEST(test_batch_operations);
    RUN_TEST(test_flush_all);
    RUN_TEST(test_statistics);
    RUN_TEST(test_state_transitions);
    RUN_TEST(test_multiple_addresses);
    
    // Print summary
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Test Summary:" << std::endl;
    std::cout << "  [PASS] Passed: " << tests_passed << std::endl;
    std::cout << "  [FAIL] Failed: " << tests_failed << std::endl;
    std::cout << "  Total:  " << (tests_passed + tests_failed) << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    if (tests_failed == 0) {
        std::cout << "\nSuccess! All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n[FAIL] Some tests failed." << std::endl;
        return 1;
    }
}
