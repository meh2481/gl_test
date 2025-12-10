#pragma once

#include <vulkan/vulkan.h>
#include "../resources/resource.h"
#include <map>
#include <cstdint>

// Helper class for managing Vulkan textures
class VulkanTexture {
public:
    VulkanTexture();
    ~VulkanTexture();

    // Initialization - must be called before any other operations
    void init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue);
    void cleanup();

    // Texture data structure
    struct TextureData {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkSampler sampler;
        uint32_t width;
        uint32_t height;
        bool isRenderTarget;
    };

    // Texture creation and management
    void createTextureImage(uint64_t textureId, const void* imageData, uint32_t width, uint32_t height,
                           VkFormat format, size_t dataSize);
    void createTextureSampler(uint64_t textureId);
    bool getTexture(uint64_t textureId, TextureData* outData) const;
    bool hasTexture(uint64_t textureId) const;
    bool getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const;

    // Load texture from resource data (handles image header parsing)
    void loadTexture(uint64_t textureId, const ResourceData& imageData);
    void loadAtlasTexture(uint64_t atlasId, const ResourceData& atlasData);

    // Render target texture creation (for render-to-texture)
    void createRenderTargetTexture(uint64_t textureId, uint32_t width, uint32_t height, VkFormat format);

    // Cleanup individual texture
    void destroyTexture(uint64_t textureId);
    void destroyAllTextures();

    // Access textures map directly for iteration
    const std::map<uint64_t, TextureData>& getTextures() const { return m_textures; }

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkCommandPool m_commandPool;
    VkQueue m_graphicsQueue;
    bool m_initialized;

    std::map<uint64_t, TextureData> m_textures;
};
