#pragma once

#include <vulkan/vulkan.h>
#include "../core/Vector.h"

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
    void copyDataToBuffer(VkDeviceMemory bufferMemory, const void* data, Uint64 size);

    // Vertex buffer utilities
    struct DynamicBuffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
        Uint64 currentSize;
        Uint32 count;
    };

    void createDynamicVertexBuffer(DynamicBuffer& dynBuffer, Uint64 initialSize);
    void updateDynamicVertexBuffer(DynamicBuffer& dynBuffer, const Vector<float>& vertexData,
                                   Uint32 floatsPerVertex);
    void destroyDynamicBuffer(DynamicBuffer& dynBuffer);

    // Indexed buffer (vertex + index)
    struct IndexedBuffer {
        VkBuffer vertexBuffer;
        VkDeviceMemory vertexMemory;
        Uint64 vertexSize;
        Uint32 vertexCount;
        VkBuffer indexBuffer;
        VkDeviceMemory indexMemory;
        Uint64 indexSize;
        Uint32 indexCount;
    };

    void createIndexedBuffer(IndexedBuffer& buffer, Uint64 initialVertexSize, Uint64 initialIndexSize);
    void updateIndexedBuffer(IndexedBuffer& buffer, const Vector<float>& vertexData,
                            const Vector<Uint16>& indices, Uint32 floatsPerVertex);
    void destroyIndexedBuffer(IndexedBuffer& buffer);

    // Memory type finding utility
    Uint32 findMemoryType(Uint32 typeFilter, VkMemoryPropertyFlags properties);

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    ConsoleBuffer* m_consoleBuffer;
    bool m_initialized;
};
