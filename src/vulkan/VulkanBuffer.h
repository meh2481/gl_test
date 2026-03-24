#pragma once

#include <vulkan/vulkan.h>
#include "../core/Vector.h"
#include <cstdint>

// Forward declaration
class ConsoleBuffer;

// Helper class for managing Vulkan buffers
class VulkanBuffer {
public:
    VulkanBuffer();
    ~VulkanBuffer();

    // Initialization - must be called before any other operations
    void init(VkDevice device, VkPhysicalDevice physicalDevice, ConsoleBuffer* consoleBuffer);
    void cleanup();

    // Buffer creation and management
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyDataToBuffer(VkDeviceMemory bufferMemory, const void* data, uint64_t size);

    // Vertex buffer utilities
    struct DynamicBuffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
        uint64_t currentSize;
        uint32_t count;
    };

    void createDynamicVertexBuffer(DynamicBuffer& dynBuffer, uint64_t initialSize);
    void updateDynamicVertexBuffer(DynamicBuffer& dynBuffer, const Vector<float>& vertexData,
                                   uint32_t floatsPerVertex);
    void destroyDynamicBuffer(DynamicBuffer& dynBuffer);

    // Indexed buffer (vertex + index)
    struct IndexedBuffer {
        VkBuffer vertexBuffer;
        VkDeviceMemory vertexMemory;
        uint64_t vertexSize;
        uint32_t vertexCount;
        VkBuffer indexBuffer;
        VkDeviceMemory indexMemory;
        uint64_t indexSize;
        uint32_t indexCount;
    };

    void createIndexedBuffer(IndexedBuffer& buffer, uint64_t initialVertexSize, uint64_t initialIndexSize);
    void updateIndexedBuffer(IndexedBuffer& buffer, const Vector<float>& vertexData,
                            const Vector<uint16_t>& indices, uint32_t floatsPerVertex);
    void destroyIndexedBuffer(IndexedBuffer& buffer);

    // Memory type finding utility
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    ConsoleBuffer* m_consoleBuffer;
    bool m_initialized;
};
