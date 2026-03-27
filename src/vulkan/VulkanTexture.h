#pragma once

#include <vulkan/vulkan.h>
#include "../resources/resource.h"
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"

class ConsoleBuffer;

// Helper class for managing Vulkan textures
class VulkanTexture {
public:
    VulkanTexture(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer);
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
        Uint32 width;
        Uint32 height;
        bool isRenderTarget;
    };

    // Texture creation and management
    void createTextureImage(Uint64 textureId, const void* imageData, Uint32 width, Uint32 height,
                           VkFormat format, Uint64 dataSize);
    void createTextureSampler(Uint64 textureId);
    bool getTexture(Uint64 textureId, TextureData* outData) const;
    bool hasTexture(Uint64 textureId) const;
    bool getTextureDimensions(Uint64 textureId, Uint32* width, Uint32* height) const;

    // Load texture from resource data (handles image header parsing)
    void loadTexture(Uint64 textureId, const ResourceData& imageData);
    void loadAtlasTexture(Uint64 atlasId, const ResourceData& atlasData);

    // Render target texture creation (for render-to-texture)
    void createRenderTargetTexture(Uint64 textureId, Uint32 width, Uint32 height, VkFormat format);

    // Cleanup individual texture
    void destroyTexture(Uint64 textureId);
    void destroyAllTextures();

    // Access textures map directly for iteration
    const HashTable<Uint64, TextureData>& getTextures() const { return m_textures; }

private:
    Uint32 findMemoryType(Uint32 typeFilter, VkMemoryPropertyFlags properties);

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkCommandPool m_commandPool;
    VkQueue m_graphicsQueue;
    bool m_initialized;

    HashTable<Uint64, TextureData> m_textures;
    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;
};
