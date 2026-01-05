#include "VulkanWaterPolygon.h"
#include <SDL3/SDL_log.h>
#include <cassert>

VulkanWaterPolygon::VulkanWaterPolygon(MemoryAllocator* allocator) :
    m_device(VK_NULL_HANDLE),
    m_physicalDevice(VK_NULL_HANDLE),
    m_initialized(false),
    m_uniformBuffer(VK_NULL_HANDLE),
    m_uniformBufferMemory(VK_NULL_HANDLE),
    m_uniformBufferMapped(nullptr),
    m_allocator(allocator)
{
    assert(m_allocator != nullptr);
    memset(&m_bufferData, 0, sizeof(m_bufferData));
    m_bufferData.vertexCount = 0;
}

VulkanWaterPolygon::~VulkanWaterPolygon() {
}

void VulkanWaterPolygon::init(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_initialized = true;
}

void VulkanWaterPolygon::cleanup() {
    if (m_uniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_uniformBuffer, nullptr);
        m_uniformBuffer = VK_NULL_HANDLE;
    }
    if (m_uniformBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_uniformBufferMemory, nullptr);
        m_uniformBufferMemory = VK_NULL_HANDLE;
    }
    m_uniformBufferMapped = nullptr;
    m_initialized = false;
}

uint32_t VulkanWaterPolygon::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(false && "VulkanWaterPolygon: failed to find suitable memory type!");
    return 0;
}

void VulkanWaterPolygon::createUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(WaterPolygonBufferData);

    SDL_Log("VulkanWaterPolygon::createUniformBuffer - Creating buffer of size %zu bytes", (size_t)bufferSize);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    assert(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_uniformBuffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_uniformBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uniformBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(m_device, m_uniformBuffer, m_uniformBufferMemory, 0);

    // Map the buffer memory for persistent updating
    vkMapMemory(m_device, m_uniformBufferMemory, 0, bufferSize, 0, &m_uniformBufferMapped);

    SDL_Log("VulkanWaterPolygon::createUniformBuffer - Buffer created successfully: %p", (void*)m_uniformBuffer);
}

void VulkanWaterPolygon::updateUniformBuffer(const float* vertices, int vertexCount) {
    assert(vertexCount >= 0 && vertexCount <= 8);

    // If buffer not created yet, just store the data for later
    if (m_uniformBufferMapped == nullptr) {
        SDL_Log("VulkanWaterPolygon::updateUniformBuffer - buffer not mapped yet, storing data for later");
        m_bufferData.vertexCount = vertexCount;
        for (int i = 0; i < vertexCount * 2; ++i) {
            m_bufferData.vertices[i] = vertices[i];
        }
        // Pad remaining vertices
        if (vertexCount > 0) {
            for (int i = vertexCount; i < 8; ++i) {
                m_bufferData.vertices[i * 2] = vertices[(vertexCount - 1) * 2];
                m_bufferData.vertices[i * 2 + 1] = vertices[(vertexCount - 1) * 2 + 1];
            }
        }
        return;
    }

    m_bufferData.vertexCount = vertexCount;
    for (int i = 0; i < vertexCount * 2; ++i) {
        m_bufferData.vertices[i] = vertices[i];
    }

    // Pad remaining vertices with last vertex (for shader simplicity)
    if (vertexCount > 0) {
        for (int i = vertexCount; i < 8; ++i) {
            m_bufferData.vertices[i * 2] = vertices[(vertexCount - 1) * 2];
            m_bufferData.vertices[i * 2 + 1] = vertices[(vertexCount - 1) * 2 + 1];
        }
    }

    memcpy(m_uniformBufferMapped, &m_bufferData, sizeof(WaterPolygonBufferData));
}
