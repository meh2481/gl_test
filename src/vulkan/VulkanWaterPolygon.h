#pragma once

#include <vulkan/vulkan.h>
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"
#include <cstdint>
#include <cstring>

// Water polygon uniform buffer data (must match shader layout with std140 alignment)
struct WaterPolygonBufferData {
    float vertices[8 * 2];  // 8 vertices × 2 coords = 16 floats = 64 bytes
    int vertexCount;        // 4 bytes at offset 64
    float padding[3];       // 12 bytes padding to align to 16-byte boundary
};  // Total: 80 bytes

// Helper class for managing water polygon uniform buffers
class VulkanWaterPolygon {
public:
    VulkanWaterPolygon(MemoryAllocator* allocator);
    ~VulkanWaterPolygon();

    // Initialization
    void init(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanup();

    // Uniform buffer management
    void createUniformBuffer();
    void updateUniformBuffer(const float* vertices, int vertexCount);
    VkBuffer getUniformBuffer() const { return m_uniformBuffer; }
    VkDeviceSize getBufferSize() const { return sizeof(WaterPolygonBufferData); }

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    bool m_initialized;

    VkBuffer m_uniformBuffer;
    VkDeviceMemory m_uniformBufferMemory;
    void* m_uniformBufferMapped;

    WaterPolygonBufferData m_bufferData;
    MemoryAllocator* m_allocator;
};
