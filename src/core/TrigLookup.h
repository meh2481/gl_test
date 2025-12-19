#pragma once

#include <cstdint>
#include <cassert>

// Forward declarations
class MemoryAllocator;
class PakResource;
class ConsoleBuffer;

// Fast trigonometric lookup table
// Uses precomputed values with linear interpolation for high speed
//
// Usage example:
//   TrigLookup trigLookup(&allocator, &consoleBuffer);
//   trigLookup.load(&pakResource);
//
//   float angle = 1.57f;  // ~90 degrees
//   float s = trigLookup.sin(angle);
//   float c = trigLookup.cos(angle);
//
//   // Or get both at once:
//   float sinVal, cosVal;
//   trigLookup.sincos(angle, sinVal, cosVal);
//
class TrigLookup {
public:
    TrigLookup(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer);
    ~TrigLookup();

    // Load the trig lookup table from the resource pak
    bool load(PakResource* pakResource);

    // Fast sin/cos lookups with linear interpolation
    // angle is in radians
    float sin(float angle) const;
    float cos(float angle) const;

    // Get both sin and cos at once (slightly more efficient)
    void sincos(float angle, float& outSin, float& outCos) const;

private:
    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;
    float* m_sinTable;
    float* m_cosTable;
    uint32_t m_numEntries;
    float m_angleStep;
    float m_invAngleStep;  // 1.0 / m_angleStep for faster division
};
