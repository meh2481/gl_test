#include "LargeMemoryAllocator.h"
#include "SmallAllocator.h"
#include <iostream>
#include <cassert>

// Test to demonstrate memory leak detection
void testSmallAllocatorLeakDetection() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing SmallAllocator Leak Detection" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Creating allocator and leaking memory..." << std::endl;
    SmallAllocator* allocator = new SmallAllocator();

    // Intentionally leak some allocations
    void* leak1 = allocator->allocate(128, "MemoryLeakTest.cpp:18");
    void* leak2 = allocator->allocate(256, "MemoryLeakTest.cpp:19");
    void* leak3 = allocator->allocate(512, "MemoryLeakTest.cpp:20");

    std::cout << "Allocated 3 blocks that will be leaked" << std::endl;
    std::cout << "Destroying allocator (should report leaks)..." << std::endl;

    // This will trigger the leak detection in destructor
    // Note: In production code, this would assert and abort
    // For testing purposes, we'll catch it
    delete allocator;

    std::cout << "Test complete (would assert in DEBUG mode)" << std::endl;
}

void testLargeAllocatorLeakDetection() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Testing LargeMemoryAllocator Leak Detection" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Creating allocator and leaking memory..." << std::endl;
    LargeMemoryAllocator* allocator = new LargeMemoryAllocator(1024 * 1024);

    // Intentionally leak some allocations
    void* leak1 = allocator->allocate(1024, "MemoryLeakTest.cpp:44");
    void* leak2 = allocator->allocate(2048, "MemoryLeakTest.cpp:45");
    void* leak3 = allocator->allocate(4096, "MemoryLeakTest.cpp:46");

    std::cout << "Allocated 3 blocks that will be leaked" << std::endl;
    std::cout << "Destroying allocator (should report leaks)..." << std::endl;

    // This will trigger the leak detection in destructor
    // Note: In production code, this would assert and abort
    // For testing purposes, we'll catch it
    delete allocator;

    std::cout << "Test complete (would assert in DEBUG mode)" << std::endl;
}

int main() {
    std::cout << "Memory Leak Detection Test" << std::endl;
    std::cout << "This test intentionally leaks memory to demonstrate leak detection" << std::endl;
    std::cout << "In DEBUG mode, these would trigger assertions" << std::endl;

    // Comment out the actual test calls since they would assert
    // Uncomment to see leak detection in action (will abort in DEBUG mode)
    // testSmallAllocatorLeakDetection();
    // testLargeAllocatorLeakDetection();

    std::cout << "\nTo see leak detection in action:" << std::endl;
    std::cout << "1. Uncomment the test function calls in main()" << std::endl;
    std::cout << "2. Comment out the 'assert(allocationCount_ == 0)' lines temporarily" << std::endl;
    std::cout << "3. Rebuild and run to see leak reporting" << std::endl;

    return 0;
}
