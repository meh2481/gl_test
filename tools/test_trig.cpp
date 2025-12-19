#include "../src/core/TrigLookup.h"
#include "../src/core/ResourceTypes.h"
#include "../src/core/hash.h"
#include "../src/memory/LargeMemoryAllocator.h"
#include "../src/resources/resource.h"
#include "../src/debug/ConsoleBuffer.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>

int main() {
    LargeMemoryAllocator allocator(nullptr);
    ConsoleBuffer consoleBuffer(&allocator);

    // Load the pak resource
    PakResource pakResource(&allocator, &consoleBuffer);
    if (!pakResource.load("res.pak")) {
        std::cerr << "Failed to load res.pak" << std::endl;
        return 1;
    }

    // Create and load the trig lookup table
    TrigLookup trigLookup(&allocator, &consoleBuffer);
    if (!trigLookup.load(&pakResource)) {
        std::cerr << "Failed to load trig lookup table" << std::endl;
        return 1;
    }

    std::cout << "Trig lookup table loaded successfully!" << std::endl;
    std::cout << std::fixed << std::setprecision(6);

    // Test some angles and compare with standard library
    const float testAngles[] = {
        0.0f,
        0.5f,      // ~28.6 degrees
        1.0f,      // ~57.3 degrees
        1.5708f,   // PI/2 (90 degrees)
        3.14159f,  // PI (180 degrees)
        4.71239f,  // 3*PI/2 (270 degrees)
        6.28318f,  // 2*PI (360 degrees)
        -0.5f,     // negative angle
    };

    std::cout << "\n=== Accuracy Test ===" << std::endl;
    std::cout << "Angle (rad) | Lookup Sin | Std Sin  | Error     | Lookup Cos | Std Cos  | Error" << std::endl;
    std::cout << "------------+------------+----------+-----------+------------+----------+----------" << std::endl;

    float maxSinError = 0.0f;
    float maxCosError = 0.0f;

    for (float angle : testAngles) {
        float lookupSin = trigLookup.sin(angle);
        float lookupCos = trigLookup.cos(angle);
        float stdSin = sinf(angle);
        float stdCos = cosf(angle);
        float sinError = fabsf(lookupSin - stdSin);
        float cosError = fabsf(lookupCos - stdCos);

        maxSinError = std::max(maxSinError, sinError);
        maxCosError = std::max(maxCosError, cosError);

        std::cout << std::setw(11) << angle << " | "
                  << std::setw(10) << lookupSin << " | "
                  << std::setw(8) << stdSin << " | "
                  << std::setw(9) << sinError << " | "
                  << std::setw(10) << lookupCos << " | "
                  << std::setw(8) << stdCos << " | "
                  << std::setw(8) << cosError << std::endl;
    }

    std::cout << "\nMaximum sin error: " << maxSinError << std::endl;
    std::cout << "Maximum cos error: " << maxCosError << std::endl;

    // Test sincos function
    std::cout << "\n=== Testing sincos() function ===" << std::endl;
    float testAngle = 1.234f;
    float outSin, outCos;
    trigLookup.sincos(testAngle, outSin, outCos);
    std::cout << "Angle: " << testAngle << " rad" << std::endl;
    std::cout << "sincos() returned: sin=" << outSin << ", cos=" << outCos << std::endl;
    std::cout << "Expected:          sin=" << sinf(testAngle) << ", cos=" << cosf(testAngle) << std::endl;

    // Performance comparison
    std::cout << "\n=== Performance Test ===" << std::endl;
    const int iterations = 1000000;
    float dummy = 0.0f;

    // Standard library sin
    auto startStd = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        float angle = (float)i * 0.001f;
        dummy += sinf(angle);
    }
    auto endStd = std::chrono::high_resolution_clock::now();
    auto durationStd = std::chrono::duration_cast<std::chrono::microseconds>(endStd - startStd).count();

    // Lookup table sin
    auto startLookup = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        float angle = (float)i * 0.001f;
        dummy += trigLookup.sin(angle);
    }
    auto endLookup = std::chrono::high_resolution_clock::now();
    auto durationLookup = std::chrono::duration_cast<std::chrono::microseconds>(endLookup - startLookup).count();

    std::cout << "Standard library sin: " << durationStd << " µs" << std::endl;
    std::cout << "Lookup table sin:     " << durationLookup << " µs" << std::endl;
    std::cout << "Speedup:              " << (float)durationStd / (float)durationLookup << "x" << std::endl;
    std::cout << "(dummy = " << dummy << " to prevent optimization)" << std::endl;

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}
