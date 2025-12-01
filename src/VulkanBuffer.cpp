#include "VulkanBuffer.h"
#include <cstring>
#include <cassert>

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
    assert(false && "failed to find suitable memory type!");
    return 0;
}

void VulkanBuffer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                               VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    assert(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) == VK_SUCCESS);
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

void VulkanBuffer::updateDynamicVertexBuffer(DynamicBuffer& dynBuffer, const std::vector<float>& vertexData,
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

void VulkanBuffer::updateIndexedBuffer(IndexedBuffer& buffer, const std::vector<float>& vertexData,
                                       const std::vector<uint16_t>& indices, uint32_t floatsPerVertex) {
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
