#include "VulkanBuffer.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cassert>

// Helper function to convert VkResult to readable string for error logging
static const char* vkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        default: return "VK_UNKNOWN_ERROR";
    }
}

VulkanBuffer::VulkanBuffer() :
    m_device(VK_NULL_HANDLE),
    m_physicalDevice(VK_NULL_HANDLE),
    m_initialized(false)
{
}

VulkanBuffer::~VulkanBuffer() {
}

void VulkanBuffer::init(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_initialized = true;
}

void VulkanBuffer::cleanup() {
    m_initialized = false;
}

uint32_t VulkanBuffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VulkanBuffer: failed to find suitable memory type for buffer allocation");
    assert(false);
    return 0;
}

void VulkanBuffer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                               VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateBuffer failed: %s", vkResultToString(result));
        assert(false);
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    result = vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkAllocateMemory failed: %s", vkResultToString(result));
        assert(false);
    }
    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
}

void VulkanBuffer::copyDataToBuffer(VkDeviceMemory bufferMemory, const void* data, size_t size) {
    void* mapped;
    vkMapMemory(m_device, bufferMemory, 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(m_device, bufferMemory);
}

void VulkanBuffer::createDynamicVertexBuffer(DynamicBuffer& dynBuffer, size_t initialSize) {
    dynBuffer.currentSize = initialSize;
    dynBuffer.count = 0;

    createBuffer(initialSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                dynBuffer.buffer, dynBuffer.memory);
}

void VulkanBuffer::updateDynamicVertexBuffer(DynamicBuffer& dynBuffer, const Vector<float>& vertexData,
                                             uint32_t floatsPerVertex) {
    if (vertexData.empty()) {
        dynBuffer.count = 0;
        return;
    }

    size_t dataSize = vertexData.size() * sizeof(float);

    // Reallocate if needed
    if (dataSize > dynBuffer.currentSize) {
        if (dynBuffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, dynBuffer.buffer, nullptr);
        }
        if (dynBuffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, dynBuffer.memory, nullptr);
        }

        dynBuffer.currentSize = dataSize * 2;
        createBuffer(dynBuffer.currentSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    dynBuffer.buffer, dynBuffer.memory);
    }

    copyDataToBuffer(dynBuffer.memory, vertexData.data(), dataSize);
    dynBuffer.count = static_cast<uint32_t>(vertexData.size() / floatsPerVertex);
}

void VulkanBuffer::destroyDynamicBuffer(DynamicBuffer& dynBuffer) {
    if (dynBuffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, dynBuffer.buffer, nullptr);
        dynBuffer.buffer = VK_NULL_HANDLE;
    }
    if (dynBuffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, dynBuffer.memory, nullptr);
        dynBuffer.memory = VK_NULL_HANDLE;
    }
    dynBuffer.currentSize = 0;
    dynBuffer.count = 0;
}

void VulkanBuffer::createIndexedBuffer(IndexedBuffer& buffer, size_t initialVertexSize, size_t initialIndexSize) {
    buffer.vertexSize = initialVertexSize;
    buffer.indexSize = initialIndexSize;
    buffer.vertexCount = 0;
    buffer.indexCount = 0;

    createBuffer(initialVertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buffer.vertexBuffer, buffer.vertexMemory);

    createBuffer(initialIndexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buffer.indexBuffer, buffer.indexMemory);
}

void VulkanBuffer::updateIndexedBuffer(IndexedBuffer& buffer, const Vector<float>& vertexData,
                                       const Vector<uint16_t>& indices, uint32_t floatsPerVertex) {
    if (vertexData.empty() || indices.empty()) {
        buffer.vertexCount = 0;
        buffer.indexCount = 0;
        return;
    }

    size_t vertexDataSize = vertexData.size() * sizeof(float);
    size_t indexDataSize = indices.size() * sizeof(uint16_t);

    // Reallocate vertex buffer if needed
    if (vertexDataSize > buffer.vertexSize) {
        if (buffer.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, buffer.vertexBuffer, nullptr);
        }
        if (buffer.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, buffer.vertexMemory, nullptr);
        }

        buffer.vertexSize = vertexDataSize * 2;
        createBuffer(buffer.vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    buffer.vertexBuffer, buffer.vertexMemory);
    }

    // Reallocate index buffer if needed
    if (indexDataSize > buffer.indexSize) {
        if (buffer.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, buffer.indexBuffer, nullptr);
        }
        if (buffer.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, buffer.indexMemory, nullptr);
        }

        buffer.indexSize = indexDataSize * 2;
        createBuffer(buffer.indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    buffer.indexBuffer, buffer.indexMemory);
    }

    copyDataToBuffer(buffer.vertexMemory, vertexData.data(), vertexDataSize);
    copyDataToBuffer(buffer.indexMemory, indices.data(), indexDataSize);

    buffer.vertexCount = static_cast<uint32_t>(vertexData.size() / floatsPerVertex);
    buffer.indexCount = static_cast<uint32_t>(indices.size());
}

void VulkanBuffer::destroyIndexedBuffer(IndexedBuffer& buffer) {
    if (buffer.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, buffer.vertexBuffer, nullptr);
        buffer.vertexBuffer = VK_NULL_HANDLE;
    }
    if (buffer.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, buffer.vertexMemory, nullptr);
        buffer.vertexMemory = VK_NULL_HANDLE;
    }
    if (buffer.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, buffer.indexBuffer, nullptr);
        buffer.indexBuffer = VK_NULL_HANDLE;
    }
    if (buffer.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, buffer.indexMemory, nullptr);
        buffer.indexMemory = VK_NULL_HANDLE;
    }
    buffer.vertexSize = 0;
    buffer.indexSize = 0;
    buffer.vertexCount = 0;
    buffer.indexCount = 0;
}
