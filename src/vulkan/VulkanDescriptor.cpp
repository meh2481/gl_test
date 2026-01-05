#include "VulkanDescriptor.h"
#include "../debug/ConsoleBuffer.h"
#include "VulkanTexture.h"
#include <cassert>
#include <vector>

// Animation push constant size: 6 base + 7 params + 20 animation = 33 floats
// Water polygon vertices now passed via uniform buffer instead of push constants
static const uint32_t ANIM_PUSH_CONSTANT_FLOAT_COUNT = 33;

// Helper function to convert VkResult to readable string for error logging
static const char* vkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        default: return "VK_UNKNOWN_ERROR";
    }
}

VulkanDescriptor::VulkanDescriptor(MemoryAllocator* allocator) :
    m_device(VK_NULL_HANDLE),
    m_textureManager(nullptr),
    m_initialized(false),
    m_singleTextureDescriptorSetLayout(VK_NULL_HANDLE),
    m_singleTextureDescriptorPool(VK_NULL_HANDLE),
    m_singleTextureDescriptorSets(*allocator, "VulkanDescriptor::m_singleTextureDescriptorSets"),
    m_singleTexturePipelineLayout(VK_NULL_HANDLE),
    m_dualTextureDescriptorSetLayout(VK_NULL_HANDLE),
    m_dualTextureDescriptorPool(VK_NULL_HANDLE),
    m_dualTextureDescriptorSets(*allocator, "VulkanDescriptor::m_dualTextureDescriptorSets"),
    m_dualTexturePipelineLayout(VK_NULL_HANDLE),
    m_animSingleTexturePipelineLayout(VK_NULL_HANDLE),
    m_animDualTexturePipelineLayout(VK_NULL_HANDLE),
    m_lightDescriptorSetLayout(VK_NULL_HANDLE),
    m_lightDescriptorPool(VK_NULL_HANDLE),
    m_lightDescriptorSet(VK_NULL_HANDLE),
    m_waterPolygonDescriptorSetLayout(VK_NULL_HANDLE),
    m_waterPolygonDescriptorPool(VK_NULL_HANDLE),
    m_waterPolygonDescriptorSet(VK_NULL_HANDLE),
    m_waterDescriptorSetLayout(VK_NULL_HANDLE),
    m_waterDescriptorPool(VK_NULL_HANDLE),
    m_waterDescriptorSet(VK_NULL_HANDLE),
    m_waterPipelineLayout(VK_NULL_HANDLE),
    m_allocator(allocator)
{
    assert(m_allocator != nullptr);
}

VulkanDescriptor::~VulkanDescriptor() {
}

void VulkanDescriptor::init(VkDevice device, ConsoleBuffer* consoleBuffer) {
    m_consoleBuffer = consoleBuffer;
    assert(m_consoleBuffer != nullptr);
    m_device = device;
    m_initialized = true;
}

void VulkanDescriptor::cleanup() {
    if (m_singleTexturePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_singleTexturePipelineLayout, nullptr);
        m_singleTexturePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_dualTexturePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_dualTexturePipelineLayout, nullptr);
        m_dualTexturePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_singleTextureDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_singleTextureDescriptorPool, nullptr);
        m_singleTextureDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_dualTextureDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_dualTextureDescriptorPool, nullptr);
        m_dualTextureDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_lightDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_lightDescriptorPool, nullptr);
        m_lightDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_singleTextureDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_singleTextureDescriptorSetLayout, nullptr);
        m_singleTextureDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_dualTextureDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_dualTextureDescriptorSetLayout, nullptr);
        m_dualTextureDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_lightDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_lightDescriptorSetLayout, nullptr);
        m_lightDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_waterPolygonDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_waterPolygonDescriptorSetLayout, nullptr);
        m_waterPolygonDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_waterDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_waterDescriptorSetLayout, nullptr);
        m_waterDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_waterDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_waterDescriptorPool, nullptr);
        m_waterDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_waterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_waterPipelineLayout, nullptr);
        m_waterPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_animSingleTexturePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_animSingleTexturePipelineLayout, nullptr);
        m_animSingleTexturePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_animDualTexturePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_animDualTexturePipelineLayout, nullptr);
        m_animDualTexturePipelineLayout = VK_NULL_HANDLE;
    }

    m_singleTextureDescriptorSets.clear();
    m_dualTextureDescriptorSets.clear();
    m_lightDescriptorSet = VK_NULL_HANDLE;
    m_waterPolygonDescriptorSet = VK_NULL_HANDLE;
    m_waterDescriptorSet = VK_NULL_HANDLE;
    m_initialized = false;
}

void VulkanDescriptor::createSingleTextureDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    VkResult result = vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_singleTextureDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout (single texture) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createSingleTexturePipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 6;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_singleTextureDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_singleTexturePipelineLayout);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout (single texture) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createSingleTextureDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 100;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 100;

    VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_singleTextureDescriptorPool);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorPool (single texture) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createDualTextureDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[2];

    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    assert(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_dualTextureDescriptorSetLayout) == VK_SUCCESS);
}

void VulkanDescriptor::createDualTexturePipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 13;  // width, height, time, cameraX, cameraY, cameraZoom, param0-6

    VkDescriptorSetLayout setLayouts[] = {m_dualTextureDescriptorSetLayout, m_lightDescriptorSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    assert(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_dualTexturePipelineLayout) == VK_SUCCESS);
}

void VulkanDescriptor::createDualTextureDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 200;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 100;

    assert(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_dualTextureDescriptorPool) == VK_SUCCESS);
}

void VulkanDescriptor::createLightDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    assert(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_lightDescriptorSetLayout) == VK_SUCCESS);
}

void VulkanDescriptor::createLightDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    assert(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_lightDescriptorPool) == VK_SUCCESS);
}

void VulkanDescriptor::createSingleTextureDescriptorSet(uint64_t textureId, VkImageView imageView, VkSampler sampler) {
    if (m_singleTextureDescriptorSets.find(textureId) != nullptr) {
        return;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_singleTextureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_singleTextureDescriptorSetLayout;

    VkDescriptorSet descriptorSet;
    assert(vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) == VK_SUCCESS);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

    m_singleTextureDescriptorSets.insert(textureId, descriptorSet);
}

void VulkanDescriptor::createDualTextureDescriptorSet(uint64_t descriptorId, uint64_t texture1Id, uint64_t texture2Id) {
    if (m_dualTextureDescriptorSets.find(descriptorId) != nullptr) {
        return;
    }

    assert(m_textureManager != nullptr);

    VulkanTexture::TextureData tex1, tex2;
    assert(m_textureManager->getTexture(texture1Id, &tex1));
    assert(m_textureManager->getTexture(texture2Id, &tex2));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_dualTextureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_dualTextureDescriptorSetLayout;

    VkDescriptorSet descriptorSet;
    assert(vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) == VK_SUCCESS);

    VkDescriptorImageInfo imageInfos[2];

    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = tex1.imageView;
    imageInfos[0].sampler = tex1.sampler;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = tex2.imageView;
    imageInfos[1].sampler = tex2.sampler;

    VkWriteDescriptorSet descriptorWrites[2];

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].pNext = nullptr;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfos[0];
    descriptorWrites[0].pBufferInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].pNext = nullptr;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfos[1];
    descriptorWrites[1].pBufferInfo = nullptr;
    descriptorWrites[1].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(m_device, 2, descriptorWrites, 0, nullptr);

    m_dualTextureDescriptorSets.insert(descriptorId, descriptorSet);
}

void VulkanDescriptor::createDescriptorSetForTextures(uint64_t descriptorId, const Vector<uint64_t>& textureIds) {
    if (textureIds.size() == 1) {
        const VkDescriptorSet* descSetPtr = m_singleTextureDescriptorSets.find(textureIds[0]);
        if (descSetPtr != nullptr) {
            m_singleTextureDescriptorSets.insert(descriptorId, *descSetPtr);
        }
    } else if (textureIds.size() == 2) {
        createDualTextureDescriptorSet(descriptorId, textureIds[0], textureIds[1]);
    }
}

void VulkanDescriptor::createLightDescriptorSet(VkBuffer lightUniformBuffer, VkDeviceSize bufferSize) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_lightDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_lightDescriptorSetLayout;

    assert(vkAllocateDescriptorSets(m_device, &allocInfo, &m_lightDescriptorSet) == VK_SUCCESS);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = lightUniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = bufferSize;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_lightDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
}

VkDescriptorSet VulkanDescriptor::getSingleTextureDescriptorSet(uint64_t textureId) const {
    const VkDescriptorSet* descSetPtr = m_singleTextureDescriptorSets.find(textureId);
    if (descSetPtr != nullptr) {
        return *descSetPtr;
    }
    return VK_NULL_HANDLE;
}

VkDescriptorSet VulkanDescriptor::getDualTextureDescriptorSet(uint64_t descriptorId) const {
    const VkDescriptorSet* descSetPtr = m_dualTextureDescriptorSets.find(descriptorId);
    if (descSetPtr != nullptr) {
        return *descSetPtr;
    }
    return VK_NULL_HANDLE;
}

bool VulkanDescriptor::hasSingleTextureDescriptorSet(uint64_t textureId) const {
    return m_singleTextureDescriptorSets.find(textureId) != nullptr;
}

bool VulkanDescriptor::hasDualTextureDescriptorSet(uint64_t descriptorId) const {
    return m_dualTextureDescriptorSets.find(descriptorId) != nullptr;
}

VkDescriptorSet VulkanDescriptor::getOrCreateDescriptorSet(uint64_t descriptorId, uint64_t textureId,
                                                           uint64_t normalMapId, bool usesDualTexture) {
    if (usesDualTexture) {
        const VkDescriptorSet* descSetPtr = m_dualTextureDescriptorSets.find(descriptorId);
        if (descSetPtr != nullptr) {
            return *descSetPtr;
        }

        if (normalMapId != 0) {
            createDualTextureDescriptorSet(descriptorId, textureId, normalMapId);
            const VkDescriptorSet* newDescSetPtr = m_dualTextureDescriptorSets.find(descriptorId);
            assert(newDescSetPtr != nullptr);
            return *newDescSetPtr;
        }
    } else {
        const VkDescriptorSet* descSetPtr = m_singleTextureDescriptorSets.find(descriptorId);
        if (descSetPtr != nullptr) {
            return *descSetPtr;
        }

        const VkDescriptorSet* texDescSetPtr = m_singleTextureDescriptorSets.find(textureId);
        if (texDescSetPtr != nullptr) {
            m_singleTextureDescriptorSets.insert(descriptorId, *texDescSetPtr);
            return *texDescSetPtr;
        }
    }

    return VK_NULL_HANDLE;
}

void VulkanDescriptor::createAnimSingleTexturePipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * ANIM_PUSH_CONSTANT_FLOAT_COUNT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_singleTextureDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_animSingleTexturePipelineLayout);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout (anim single texture) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createAnimDualTexturePipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * ANIM_PUSH_CONSTANT_FLOAT_COUNT;

    VkDescriptorSetLayout setLayouts[] = {m_dualTextureDescriptorSetLayout, m_lightDescriptorSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_animDualTexturePipelineLayout);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout (anim dual texture) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createWaterPolygonDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    assert(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_waterPolygonDescriptorSetLayout) == VK_SUCCESS);
}

void VulkanDescriptor::createWaterPolygonDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    assert(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_waterPolygonDescriptorPool) == VK_SUCCESS);
}

void VulkanDescriptor::createWaterPolygonDescriptorSet(VkBuffer waterPolygonUniformBuffer, VkDeviceSize bufferSize) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_waterPolygonDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_waterPolygonDescriptorSetLayout;

    assert(vkAllocateDescriptorSets(m_device, &allocInfo, &m_waterPolygonDescriptorSet) == VK_SUCCESS);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = waterPolygonUniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = bufferSize;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_waterPolygonDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanDescriptor::createWaterPipelineLayout() {
    // Water pipeline uses: single water descriptor set (with 3 bindings) + light descriptor set
    VkDescriptorSetLayout setLayouts[] = {
        m_waterDescriptorSetLayout,
        m_lightDescriptorSetLayout
    };

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * ANIM_PUSH_CONSTANT_FLOAT_COUNT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_waterPipelineLayout);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreatePipelineLayout (water) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createWaterDescriptorSetLayout() {
    // Water descriptor set: 3 bindings in a single set
    // - binding 0: primary texture sampler
    // - binding 1: reflection texture sampler
    // - binding 2: water polygon uniform buffer
    VkDescriptorSetLayoutBinding bindings[3];

    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorCount = 1;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].pImmutableSamplers = nullptr;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_waterDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorSetLayout (water) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createWaterDescriptorPool() {
    // Pool needs space for 2 samplers + 1 uniform buffer
    VkDescriptorPoolSize poolSizes[2];

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 2;  // 2 texture samplers

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;  // 1 uniform buffer

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;  // Only one water descriptor set needed

    VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_waterDescriptorPool);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkCreateDescriptorPool (water) failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanDescriptor::createWaterDescriptorSet(uint64_t texture1Id, uint64_t texture2Id, VkBuffer waterPolygonUniformBuffer, VkDeviceSize bufferSize) {
    assert(m_textureManager != nullptr);

    VulkanTexture::TextureData tex1, tex2;
    assert(m_textureManager->getTexture(texture1Id, &tex1));
    assert(m_textureManager->getTexture(texture2Id, &tex2));

    // Reset the descriptor pool to free any previously allocated descriptor sets
    // This is necessary when reloading scenes to avoid VK_ERROR_OUT_OF_POOL_MEMORY
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE, "Resetting water descriptor pool before creating new descriptor set");
    VkResult resetResult = vkResetDescriptorPool(m_device, m_waterDescriptorPool, 0);
    if (resetResult != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkResetDescriptorPool (water) failed: %s", vkResultToString(resetResult));
        assert(false);
    }
    m_waterDescriptorSet = VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_waterDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_waterDescriptorSetLayout;

    VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_waterDescriptorSet);
    if (result != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkAllocateDescriptorSets (water) failed: %s", vkResultToString(result));
        assert(false);
    }

    // Prepare descriptor writes for all 3 bindings
    VkDescriptorImageInfo imageInfos[2];
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = tex1.imageView;
    imageInfos[0].sampler = tex1.sampler;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = tex2.imageView;
    imageInfos[1].sampler = tex2.sampler;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = waterPolygonUniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = bufferSize;

    VkWriteDescriptorSet descriptorWrites[3];

    // Binding 0: primary texture
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_waterDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfos[0];
    descriptorWrites[0].pBufferInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;
    descriptorWrites[0].pNext = nullptr;

    // Binding 1: reflection texture
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_waterDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfos[1];
    descriptorWrites[1].pBufferInfo = nullptr;
    descriptorWrites[1].pTexelBufferView = nullptr;
    descriptorWrites[1].pNext = nullptr;

    // Binding 2: water polygon uniform buffer
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_waterDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = nullptr;
    descriptorWrites[2].pBufferInfo = &bufferInfo;
    descriptorWrites[2].pTexelBufferView = nullptr;
    descriptorWrites[2].pNext = nullptr;

    vkUpdateDescriptorSets(m_device, 3, descriptorWrites, 0, nullptr);

    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Created water descriptor set with 3 bindings (2 textures + polygon uniform buffer)");
}
