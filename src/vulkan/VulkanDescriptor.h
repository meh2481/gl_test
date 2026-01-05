#pragma once

#include <vulkan/vulkan.h>
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include <vector>
#include <cstdint>

// Forward declarations
class VulkanTexture;
class MemoryAllocator;
class ConsoleBuffer;

// Helper class for managing Vulkan descriptor sets, pools, and layouts
class VulkanDescriptor {
public:
    VulkanDescriptor(MemoryAllocator* allocator);
    ~VulkanDescriptor();

    // Initialization - must be called before any other operations
    void init(VkDevice device, ConsoleBuffer* consoleBuffer);
    void cleanup();

    // Set texture manager reference (needed for creating descriptor sets)
    void setTextureManager(VulkanTexture* textureManager) { m_textureManager = textureManager; }

    // Descriptor set layout management
    VkDescriptorSetLayout getSingleTextureLayout() const { return m_singleTextureDescriptorSetLayout; }
    VkDescriptorSetLayout getDualTextureLayout() const { return m_dualTextureDescriptorSetLayout; }
    VkDescriptorSetLayout getLightLayout() const { return m_lightDescriptorSetLayout; }
    VkDescriptorSetLayout getWaterPolygonLayout() const { return m_waterPolygonDescriptorSetLayout; }

    // Pipeline layout management
    VkPipelineLayout getSingleTexturePipelineLayout() const { return m_singleTexturePipelineLayout; }
    VkPipelineLayout getDualTexturePipelineLayout() const { return m_dualTexturePipelineLayout; }
    VkPipelineLayout getAnimSingleTexturePipelineLayout() const { return m_animSingleTexturePipelineLayout; }
    VkPipelineLayout getAnimDualTexturePipelineLayout() const { return m_animDualTexturePipelineLayout; }
    VkPipelineLayout getWaterPipelineLayout() const { return m_waterPipelineLayout; }

    // Create layouts and pools
    void createSingleTextureDescriptorSetLayout();
    void createSingleTexturePipelineLayout();
    void createSingleTextureDescriptorPool();
    void createDualTextureDescriptorSetLayout();
    void createDualTexturePipelineLayout();
    void createDualTextureDescriptorPool();
    void createLightDescriptorSetLayout();
    void createLightDescriptorPool();
    void createWaterPolygonDescriptorSetLayout();
    void createWaterPolygonDescriptorPool();
    void createWaterPipelineLayout();
    void createAnimSingleTexturePipelineLayout();
    void createAnimDualTexturePipelineLayout();

    // Create descriptor sets
    void createSingleTextureDescriptorSet(uint64_t textureId, VkImageView imageView, VkSampler sampler);
    void createDualTextureDescriptorSet(uint64_t descriptorId, uint64_t texture1Id, uint64_t texture2Id);
    void createDescriptorSetForTextures(uint64_t descriptorId, const Vector<uint64_t>& textureIds);

    // Light descriptor set (special case - single set for all lights)
    void createLightDescriptorSet(VkBuffer lightUniformBuffer, VkDeviceSize bufferSize);
    VkDescriptorSet getLightDescriptorSet() const { return m_lightDescriptorSet; }

    // Water polygon descriptor set (special case - single set for water polygons)
    void createWaterPolygonDescriptorSet(VkBuffer waterPolygonUniformBuffer, VkDeviceSize bufferSize);
    VkDescriptorSet getWaterPolygonDescriptorSet() const { return m_waterPolygonDescriptorSet; }

    // Get descriptor sets
    VkDescriptorSet getSingleTextureDescriptorSet(uint64_t textureId) const;
    VkDescriptorSet getDualTextureDescriptorSet(uint64_t descriptorId) const;
    bool hasSingleTextureDescriptorSet(uint64_t textureId) const;
    bool hasDualTextureDescriptorSet(uint64_t descriptorId) const;

    // Get or create descriptor set lazily
    VkDescriptorSet getOrCreateDescriptorSet(uint64_t descriptorId, uint64_t textureId,
                                             uint64_t normalMapId, bool usesDualTexture);

    // Access for iteration
    const HashTable<uint64_t, VkDescriptorSet>& getSingleTextureDescriptorSets() const {
        return m_singleTextureDescriptorSets;
    }
    const HashTable<uint64_t, VkDescriptorSet>& getDualTextureDescriptorSets() const {
        return m_dualTextureDescriptorSets;
    }

private:
    VkDevice m_device;
    VulkanTexture* m_textureManager;
    bool m_initialized;

    // Single texture descriptors
    VkDescriptorSetLayout m_singleTextureDescriptorSetLayout;
    VkDescriptorPool m_singleTextureDescriptorPool;
    HashTable<uint64_t, VkDescriptorSet> m_singleTextureDescriptorSets;
    VkPipelineLayout m_singleTexturePipelineLayout;

    // Dual texture descriptors
    VkDescriptorSetLayout m_dualTextureDescriptorSetLayout;
    VkDescriptorPool m_dualTextureDescriptorPool;
    HashTable<uint64_t, VkDescriptorSet> m_dualTextureDescriptorSets;
    VkPipelineLayout m_dualTexturePipelineLayout;

    // Animation pipeline layouts (extended push constants)
    VkPipelineLayout m_animSingleTexturePipelineLayout;
    VkPipelineLayout m_animDualTexturePipelineLayout;

    // Light descriptors
    VkDescriptorSetLayout m_lightDescriptorSetLayout;
    VkDescriptorPool m_lightDescriptorPool;
    VkDescriptorSet m_lightDescriptorSet;

    // Water polygon descriptors
    VkDescriptorSetLayout m_waterPolygonDescriptorSetLayout;
    VkDescriptorPool m_waterPolygonDescriptorPool;
    VkDescriptorSet m_waterPolygonDescriptorSet;
    VkPipelineLayout m_waterPipelineLayout;

    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;
};
