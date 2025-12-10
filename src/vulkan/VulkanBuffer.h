#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

// Helper class for managing Vulkan buffers
class VulkanBuffer {
public:
    VulkanBuffer();
    ~VulkanBuffer();

    // Initialization - must be called before any other operations
    void init(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanup();

    // Buffer creation and management
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyDataToBuffer(VkDeviceMemory bufferMemory, const void* data, size_t size);

    // Vertex buffer utilities
    struct DynamicBuffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
        size_t currentSize;
        uint32_t count;
    };

    void createDynamicVertexBuffer(DynamicBuffer& dynBuffer, size_t initialSize);
    void updateDynamicVertexBuffer(DynamicBuffer& dynBuffer, const std::vector<float>& vertexData,
                                   uint32_t floatsPerVertex);
    void destroyDynamicBuffer(DynamicBuffer& dynBuffer);

    // Indexed buffer (vertex + index)
    struct IndexedBuffer {
        VkBuffer vertexBuffer;
        VkDeviceMemory vertexMemory;
        size_t vertexSize;
        uint32_t vertexCount;
        VkBuffer indexBuffer;
        VkDeviceMemory indexMemory;
        size_t indexSize;
        uint32_t indexCount;
    };

    void createIndexedBuffer(IndexedBuffer& buffer, size_t initialVertexSize, size_t initialIndexSize);
    void updateIndexedBuffer(IndexedBuffer& buffer, const std::vector<float>& vertexData,
                            const std::vector<uint16_t>& indices, uint32_t floatsPerVertex);
    void destroyIndexedBuffer(IndexedBuffer& buffer);

    // Memory type finding utility
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    bool m_initialized;
};
