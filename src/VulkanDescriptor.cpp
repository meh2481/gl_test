#include "VulkanDescriptor.h"
#include "VulkanTexture.h"
#include <cassert>
#include <vector>
#include <iostream>

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

VulkanDescriptor::VulkanDescriptor() :
    m_device(VK_NULL_HANDLE),
    m_textureManager(nullptr),
    m_initialized(false),
    m_singleTextureDescriptorSetLayout(VK_NULL_HANDLE),
    m_singleTextureDescriptorPool(VK_NULL_HANDLE),
    m_singleTexturePipelineLayout(VK_NULL_HANDLE),
    m_dualTextureDescriptorSetLayout(VK_NULL_HANDLE),
    m_dualTextureDescriptorPool(VK_NULL_HANDLE),
    m_dualTexturePipelineLayout(VK_NULL_HANDLE),
    m_lightDescriptorSetLayout(VK_NULL_HANDLE),
    m_lightDescriptorPool(VK_NULL_HANDLE),
    m_lightDescriptorSet(VK_NULL_HANDLE)
{
}

VulkanDescriptor::~VulkanDescriptor() {
}

void VulkanDescriptor::init(VkDevice device) {
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

    m_singleTextureDescriptorSets.clear();
    m_dualTextureDescriptorSets.clear();
    m_lightDescriptorSet = VK_NULL_HANDLE;
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
        std::cerr << "vkCreateDescriptorSetLayout (single texture) failed: " << vkResultToString(result) << std::endl;
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
        std::cerr << "vkCreatePipelineLayout (single texture) failed: " << vkResultToString(result) << std::endl;
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
        std::cerr << "vkCreateDescriptorPool (single texture) failed: " << vkResultToString(result) << std::endl;
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
    if (m_singleTextureDescriptorSets.find(textureId) != m_singleTextureDescriptorSets.end()) {
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

    m_singleTextureDescriptorSets[textureId] = descriptorSet;
}

void VulkanDescriptor::createDualTextureDescriptorSet(uint64_t descriptorId, uint64_t texture1Id, uint64_t texture2Id) {
    if (m_dualTextureDescriptorSets.find(descriptorId) != m_dualTextureDescriptorSets.end()) {
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

    m_dualTextureDescriptorSets[descriptorId] = descriptorSet;
}

void VulkanDescriptor::createDescriptorSetForTextures(uint64_t descriptorId, const std::vector<uint64_t>& textureIds) {
    if (textureIds.size() == 1) {
        auto it = m_singleTextureDescriptorSets.find(textureIds[0]);
        if (it != m_singleTextureDescriptorSets.end()) {
            m_singleTextureDescriptorSets[descriptorId] = it->second;
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
    auto it = m_singleTextureDescriptorSets.find(textureId);
    if (it != m_singleTextureDescriptorSets.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

VkDescriptorSet VulkanDescriptor::getDualTextureDescriptorSet(uint64_t descriptorId) const {
    auto it = m_dualTextureDescriptorSets.find(descriptorId);
    if (it != m_dualTextureDescriptorSets.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

bool VulkanDescriptor::hasSingleTextureDescriptorSet(uint64_t textureId) const {
    return m_singleTextureDescriptorSets.find(textureId) != m_singleTextureDescriptorSets.end();
}

bool VulkanDescriptor::hasDualTextureDescriptorSet(uint64_t descriptorId) const {
    return m_dualTextureDescriptorSets.find(descriptorId) != m_dualTextureDescriptorSets.end();
}

VkDescriptorSet VulkanDescriptor::getOrCreateDescriptorSet(uint64_t descriptorId, uint64_t textureId,
                                                           uint64_t normalMapId, bool usesDualTexture) {
    if (usesDualTexture) {
        auto it = m_dualTextureDescriptorSets.find(descriptorId);
        if (it != m_dualTextureDescriptorSets.end()) {
            return it->second;
        }

        if (normalMapId != 0) {
            createDualTextureDescriptorSet(descriptorId, textureId, normalMapId);
            return m_dualTextureDescriptorSets[descriptorId];
        }
    } else {
        auto it = m_singleTextureDescriptorSets.find(descriptorId);
        if (it != m_singleTextureDescriptorSets.end()) {
            return it->second;
        }

        auto texIt = m_singleTextureDescriptorSets.find(textureId);
        if (texIt != m_singleTextureDescriptorSets.end()) {
            m_singleTextureDescriptorSets[descriptorId] = texIt->second;
            return texIt->second;
        }
    }

    return VK_NULL_HANDLE;
}
