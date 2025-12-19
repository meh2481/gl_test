#include "TrigLookup.h"
#include "ResourceTypes.h"
#include "hash.h"
#include "../resources/resource.h"
#include "../debug/ConsoleBuffer.h"
#include "../memory/MemoryAllocator.h"
#include <cstring>

// Use a constant for 2*PI to avoid repeated calculations
static const float TWO_PI = 6.28318530718f;

TrigLookup::TrigLookup(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer)
    : m_allocator(allocator)
    , m_consoleBuffer(consoleBuffer)
    , m_sinTable(nullptr)
    , m_cosTable(nullptr)
    , m_numEntries(0)
    , m_angleStep(0.0f)
    , m_invAngleStep(0.0f)
{
    assert(m_allocator != nullptr);
    assert(m_consoleBuffer != nullptr);
}

TrigLookup::~TrigLookup() {
    if (m_sinTable) {
        m_allocator->free(m_sinTable);
        m_sinTable = nullptr;
    }
    if (m_cosTable) {
        m_allocator->free(m_cosTable);
        m_cosTable = nullptr;
    }
}

bool TrigLookup::load(PakResource* pakResource) {
    assert(pakResource != nullptr);

    // Get the trig table resource using the well-known name
    uint64_t trigTableId = hashCString("res/trig_table.bin");
    ResourceData resData = pakResource->getResource(trigTableId);

    if (!resData.data || resData.size < sizeof(TrigTableHeader)) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "TrigLookup: Failed to load trig table resource");
        return false;
    }

    // Read header
    TrigTableHeader* header = (TrigTableHeader*)resData.data;
    m_numEntries = header->numEntries;
    m_angleStep = header->angleStep;
    m_invAngleStep = 1.0f / m_angleStep;

    assert(m_numEntries > 0);
    assert(m_angleStep > 0.0f);

    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "TrigLookup: Loading table with %u entries, step=%f rad",
                        m_numEntries, m_angleStep);

    // Expected data size: header + sin table + cos table
    size_t expectedSize = sizeof(TrigTableHeader) + (m_numEntries * sizeof(float) * 2);
    if (resData.size < expectedSize) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "TrigLookup: Invalid data size %zu, expected %zu",
                            resData.size, expectedSize);
        return false;
    }

    // Allocate memory for sin and cos tables
    size_t tableSize = m_numEntries * sizeof(float);
    m_sinTable = (float*)m_allocator->allocate(tableSize, "TrigLookup::m_sinTable");
    m_cosTable = (float*)m_allocator->allocate(tableSize, "TrigLookup::m_cosTable");

    assert(m_sinTable != nullptr);
    assert(m_cosTable != nullptr);

    // Copy table data from resource
    const float* sinData = (const float*)(resData.data + sizeof(TrigTableHeader));
    const float* cosData = sinData + m_numEntries;

    memcpy(m_sinTable, sinData, tableSize);
    memcpy(m_cosTable, cosData, tableSize);

    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "TrigLookup: Successfully loaded trig lookup table");
    return true;
}

float TrigLookup::sin(float angle) const {
    assert(m_sinTable != nullptr);
    assert(m_numEntries > 0);

    // Fast angle normalization to [0, 2*PI) without fmodf
    // Handle negative angles
    if (angle < 0.0f) {
        // For small negative angles, just add 2*PI once
        angle += TWO_PI;
        // For very negative angles, use fast integer division
        if (angle < 0.0f) {
            int wraps = (int)(angle / TWO_PI) - 1;
            angle -= wraps * TWO_PI;
        }
    }
    // Handle angles >= 2*PI
    if (angle >= TWO_PI) {
        // For small positive angles, subtract 2*PI once
        angle -= TWO_PI;
        // For very large angles, use fast integer division
        if (angle >= TWO_PI) {
            int wraps = (int)(angle / TWO_PI);
            angle -= wraps * TWO_PI;
        }
    }

    // Convert angle to table index (floating point)
    float indexF = angle * m_invAngleStep;

    // Get integer and fractional parts for interpolation
    uint32_t index0 = (uint32_t)indexF;
    float frac = indexF - (float)index0;

    // Clamp to valid range and handle wrap-around
    if (index0 >= m_numEntries) {
        index0 = m_numEntries - 1;
    }
    uint32_t index1 = index0 + 1;
    if (index1 >= m_numEntries) {
        index1 = 0;
    }

    // Linear interpolation
    float val0 = m_sinTable[index0];
    float val1 = m_sinTable[index1];
    return val0 + (val1 - val0) * frac;
}

float TrigLookup::cos(float angle) const {
    assert(m_cosTable != nullptr);
    assert(m_numEntries > 0);

    // Fast angle normalization to [0, 2*PI) without fmodf
    // Handle negative angles
    if (angle < 0.0f) {
        // For small negative angles, just add 2*PI once
        angle += TWO_PI;
        // For very negative angles, use fast integer division
        if (angle < 0.0f) {
            int wraps = (int)(angle / TWO_PI) - 1;
            angle -= wraps * TWO_PI;
        }
    }
    // Handle angles >= 2*PI
    if (angle >= TWO_PI) {
        // For small positive angles, subtract 2*PI once
        angle -= TWO_PI;
        // For very large angles, use fast integer division
        if (angle >= TWO_PI) {
            int wraps = (int)(angle / TWO_PI);
            angle -= wraps * TWO_PI;
        }
    }

    // Convert angle to table index (floating point)
    float indexF = angle * m_invAngleStep;

    // Get integer and fractional parts for interpolation
    uint32_t index0 = (uint32_t)indexF;
    float frac = indexF - (float)index0;

    // Clamp to valid range and handle wrap-around
    if (index0 >= m_numEntries) {
        index0 = m_numEntries - 1;
    }
    uint32_t index1 = index0 + 1;
    if (index1 >= m_numEntries) {
        index1 = 0;
    }

    // Linear interpolation
    float val0 = m_cosTable[index0];
    float val1 = m_cosTable[index1];
    return val0 + (val1 - val0) * frac;
}

void TrigLookup::sincos(float angle, float& outSin, float& outCos) const {
    assert(m_sinTable != nullptr);
    assert(m_cosTable != nullptr);
    assert(m_numEntries > 0);

    // Fast angle normalization to [0, 2*PI) without fmodf
    // Handle negative angles
    if (angle < 0.0f) {
        // For small negative angles, just add 2*PI once
        angle += TWO_PI;
        // For very negative angles, use fast integer division
        if (angle < 0.0f) {
            int wraps = (int)(angle / TWO_PI) - 1;
            angle -= wraps * TWO_PI;
        }
    }
    // Handle angles >= 2*PI
    if (angle >= TWO_PI) {
        // For small positive angles, subtract 2*PI once
        angle -= TWO_PI;
        // For very large angles, use fast integer division
        if (angle >= TWO_PI) {
            int wraps = (int)(angle / TWO_PI);
            angle -= wraps * TWO_PI;
        }
    }

    // Convert angle to table index (floating point)
    float indexF = angle * m_invAngleStep;

    // Get integer and fractional parts for interpolation
    uint32_t index0 = (uint32_t)indexF;
    float frac = indexF - (float)index0;

    // Clamp to valid range and handle wrap-around
    if (index0 >= m_numEntries) {
        index0 = m_numEntries - 1;
    }
    uint32_t index1 = index0 + 1;
    if (index1 >= m_numEntries) {
        index1 = 0;
    }

    // Linear interpolation for sin
    float sin0 = m_sinTable[index0];
    float sin1 = m_sinTable[index1];
    outSin = sin0 + (sin1 - sin0) * frac;

    // Linear interpolation for cos
    float cos0 = m_cosTable[index0];
    float cos1 = m_cosTable[index1];
    outCos = cos0 + (cos1 - cos0) * frac;
}
