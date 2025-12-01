#pragma once

#include <vulkan/vulkan.h>
#include <map>
#include <cstdint>
#include <cstring>

// Maximum number of lights supported in the scene
static const int MAX_LIGHTS = 8;

// Light structure for uniform buffer (must match shader layout)
struct Light {
    float posX, posY, posZ;    // Position (12 bytes)
    float padding1;             // Padding for alignment (4 bytes)
    float colorR, colorG, colorB; // Color (12 bytes)
    float intensity;            // Intensity (4 bytes)
};  // Total: 32 bytes per light

// Light uniform buffer data (must match shader layout)
struct LightBufferData {
    Light lights[MAX_LIGHTS];   // 32 * 8 = 256 bytes
    int numLights;              // 4 bytes
    float ambientR, ambientG, ambientB; // 12 bytes
    float padding[3];           // Padding to align to 16 bytes (12 bytes)
};  // Total: 284 bytes, padded to 288

// Helper class for managing Vulkan light system
class VulkanLight {
public:
    VulkanLight();
    ~VulkanLight();

    // Initialization - must be called before any other operations
    void init(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanup();

    // Light uniform buffer management
    void createLightUniformBuffer();
    void updateLightUniformBuffer();
    VkBuffer getUniformBuffer() const { return m_lightUniformBuffer; }
    VkDeviceSize getBufferSize() const { return sizeof(LightBufferData); }

    // Light management
    int addLight(float x, float y, float z, float r, float g, float b, float intensity);
    void updateLight(int lightId, float x, float y, float z, float r, float g, float b, float intensity);
    void removeLight(int lightId);
    void clearLights();
    void setAmbientLight(float r, float g, float b);

    // Check if buffer needs updating
    bool isDirty() const { return m_lightBufferDirty; }
    void markClean() { m_lightBufferDirty = false; }

    // Get light data
    const LightBufferData& getLightBufferData() const { return m_lightBufferData; }

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    bool m_initialized;

    // Light uniform buffer
    LightBufferData m_lightBufferData;
    VkBuffer m_lightUniformBuffer;
    VkDeviceMemory m_lightUniformBufferMemory;
    void* m_lightUniformBufferMapped;

    // Light tracking
    int m_nextLightId;
    std::map<int, int> m_lightIdToIndex;
    bool m_lightBufferDirty;
};
