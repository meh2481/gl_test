#include "VulkanLight.h"
#include <cassert>

VulkanLight::VulkanLight() :
    m_device(VK_NULL_HANDLE),
    m_physicalDevice(VK_NULL_HANDLE),
    m_initialized(false),
    m_lightUniformBuffer(VK_NULL_HANDLE),
    m_lightUniformBufferMemory(VK_NULL_HANDLE),
    m_lightUniformBufferMapped(nullptr),
    m_nextLightId(1),
    m_lightBufferDirty(true)
{
    memset(&m_lightBufferData, 0, sizeof(m_lightBufferData));
    m_lightBufferData.numLights = 0;
    m_lightBufferData.ambientR = 0.1f;
    m_lightBufferData.ambientG = 0.1f;
    m_lightBufferData.ambientB = 0.1f;
}

VulkanLight::~VulkanLight() {
}

void VulkanLight::init(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_initialized = true;
}

void VulkanLight::cleanup() {
    if (m_lightUniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_lightUniformBuffer, nullptr);
        m_lightUniformBuffer = VK_NULL_HANDLE;
    }
    if (m_lightUniformBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_lightUniformBufferMemory, nullptr);
        m_lightUniformBufferMemory = VK_NULL_HANDLE;
    }
    m_lightUniformBufferMapped = nullptr;
    m_lightIdToIndex.clear();
    m_initialized = false;
}

uint32_t VulkanLight::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(false && "VulkanLight: failed to find suitable memory type for light uniform buffer!");
    return 0;
}

void VulkanLight::createLightUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(LightBufferData);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    assert(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_lightUniformBuffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_lightUniformBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_lightUniformBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(m_device, m_lightUniformBuffer, m_lightUniformBufferMemory, 0);

    // Map the buffer memory for persistent updating
    vkMapMemory(m_device, m_lightUniformBufferMemory, 0, bufferSize, 0, &m_lightUniformBufferMapped);

    // Initialize with default data
    updateLightUniformBuffer();
}

void VulkanLight::updateLightUniformBuffer() {
    memcpy(m_lightUniformBufferMapped, &m_lightBufferData, sizeof(LightBufferData));
    m_lightBufferDirty = false;
}

int VulkanLight::addLight(float x, float y, float z, float r, float g, float b, float intensity) {
    if (m_lightBufferData.numLights >= MAX_LIGHTS) {
        return -1;
    }

    int lightId = m_nextLightId++;
    int index = m_lightBufferData.numLights;

    m_lightBufferData.lights[index].posX = x;
    m_lightBufferData.lights[index].posY = y;
    m_lightBufferData.lights[index].posZ = z;
    m_lightBufferData.lights[index].colorR = r;
    m_lightBufferData.lights[index].colorG = g;
    m_lightBufferData.lights[index].colorB = b;
    m_lightBufferData.lights[index].intensity = intensity;
    m_lightBufferData.numLights++;

    m_lightIdToIndex[lightId] = index;
    m_lightBufferDirty = true;

    return lightId;
}

void VulkanLight::updateLight(int lightId, float x, float y, float z, float r, float g, float b, float intensity) {
    auto it = m_lightIdToIndex.find(lightId);
    if (it == m_lightIdToIndex.end()) {
        return;
    }

    int index = it->second;
    m_lightBufferData.lights[index].posX = x;
    m_lightBufferData.lights[index].posY = y;
    m_lightBufferData.lights[index].posZ = z;
    m_lightBufferData.lights[index].colorR = r;
    m_lightBufferData.lights[index].colorG = g;
    m_lightBufferData.lights[index].colorB = b;
    m_lightBufferData.lights[index].intensity = intensity;
    m_lightBufferDirty = true;
}

void VulkanLight::removeLight(int lightId) {
    auto it = m_lightIdToIndex.find(lightId);
    if (it == m_lightIdToIndex.end()) {
        return;
    }

    int indexToRemove = it->second;
    int lastIndex = m_lightBufferData.numLights - 1;

    // If not the last light, swap with the last one
    if (indexToRemove != lastIndex) {
        m_lightBufferData.lights[indexToRemove] = m_lightBufferData.lights[lastIndex];

        // Update the index mapping for the swapped light
        for (auto& pair : m_lightIdToIndex) {
            if (pair.second == lastIndex) {
                pair.second = indexToRemove;
                break;
            }
        }
    }

    m_lightBufferData.numLights--;
    m_lightIdToIndex.erase(it);
    m_lightBufferDirty = true;
}

void VulkanLight::clearLights() {
    m_lightBufferData.numLights = 0;
    m_lightIdToIndex.clear();
    m_lightBufferDirty = true;
}

void VulkanLight::setAmbientLight(float r, float g, float b) {
    m_lightBufferData.ambientR = r;
    m_lightBufferData.ambientG = g;
    m_lightBufferData.ambientB = b;
    m_lightBufferDirty = true;
}
