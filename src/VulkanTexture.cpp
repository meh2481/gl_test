#include "VulkanTexture.h"
#include "ResourceTypes.h"
#include <cstring>
#include <cassert>
#include <iostream>

VulkanTexture::VulkanTexture() :
    m_device(VK_NULL_HANDLE),
    m_physicalDevice(VK_NULL_HANDLE),
    m_commandPool(VK_NULL_HANDLE),
    m_graphicsQueue(VK_NULL_HANDLE),
    m_initialized(false)
{
}

VulkanTexture::~VulkanTexture() {
}

void VulkanTexture::init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_commandPool = commandPool;
    m_graphicsQueue = graphicsQueue;
    m_initialized = true;
}

void VulkanTexture::cleanup() {
    destroyAllTextures();
    m_initialized = false;
}

uint32_t VulkanTexture::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(false && "VulkanTexture: failed to find suitable memory type for texture allocation!");
    return 0;
}

void VulkanTexture::createTextureImage(uint64_t textureId, const void* imageData, uint32_t width, uint32_t height,
                                       VkFormat format, size_t dataSize) {
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    assert(vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(m_device, stagingBuffer, stagingBufferMemory, 0);

    // Copy image data to staging buffer
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, dataSize, 0, &data);
    memcpy(data, imageData, dataSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    TextureData tex;
    tex.width = width;
    tex.height = height;
    tex.isRenderTarget = false;
    assert(vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) == VK_SUCCESS);

    VkMemoryRequirements imgMemRequirements;
    vkGetImageMemoryRequirements(m_device, tex.image, &imgMemRequirements);

    VkMemoryAllocateInfo imgAllocInfo{};
    imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAllocInfo.allocationSize = imgMemRequirements.size;
    imgAllocInfo.memoryTypeIndex = findMemoryType(imgMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    assert(vkAllocateMemory(m_device, &imgAllocInfo, nullptr, &tex.memory) == VK_SUCCESS);
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    // Transition image layout and copy from staging buffer
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = m_commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    assert(vkCreateImageView(m_device, &viewInfo, nullptr, &tex.imageView) == VK_SUCCESS);

    tex.sampler = VK_NULL_HANDLE; // Will be created by createTextureSampler
    m_textures[textureId] = tex;
}

void VulkanTexture::createTextureSampler(uint64_t textureId) {
    auto it = m_textures.find(textureId);
    assert(it != m_textures.end());

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    assert(vkCreateSampler(m_device, &samplerInfo, nullptr, &it->second.sampler) == VK_SUCCESS);
}

bool VulkanTexture::getTexture(uint64_t textureId, TextureData* outData) const {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        return false;
    }
    if (outData) {
        *outData = it->second;
    }
    return true;
}

bool VulkanTexture::hasTexture(uint64_t textureId) const {
    return m_textures.find(textureId) != m_textures.end();
}

bool VulkanTexture::getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        return false;
    }
    if (width) *width = it->second.width;
    if (height) *height = it->second.height;
    return true;
}

void VulkanTexture::loadTexture(uint64_t textureId, const ResourceData& imageData) {
    // If texture already exists, skip reloading (textures don't change during hot-reload)
    if (m_textures.find(textureId) != m_textures.end()) {
        std::cout << "Texture " << textureId << ": already in GPU memory (cache hit)" << std::endl;
        return;
    }

    if (imageData.type != RESOURCE_TYPE_IMAGE) {
        std::cerr << "Texture " << textureId << ": resource is not an image (type " << imageData.type << ")" << std::endl;
        assert(false && "Resource is not an image");
        return;
    }

    if (imageData.size == 40) {  // Atlas reference
        // This is an atlas reference
        const TextureHeader* texHeader = (const TextureHeader*)imageData.data;
        uint64_t atlasId = texHeader->atlasId;
        std::cout << "Texture " << textureId << ": atlas reference (atlas id: " << atlasId << ", UV: " << texHeader->coordinates[0] << "," << texHeader->coordinates[1] << " - " << texHeader->coordinates[4] << "," << texHeader->coordinates[5] << ")" << std::endl;
        // Load the atlas if not already loaded
        if (m_textures.find(atlasId) == m_textures.end()) {
            // Atlas not loaded, but we can't load it here without data
            std::cerr << "Atlas " << atlasId << " not loaded for texture " << textureId << std::endl;
            return;
        }
        // Associate the texture ID with the atlas texture
        m_textures[textureId] = m_textures[atlasId];
        createTextureSampler(textureId);
        return;
    } else {
        // Individual image
        // Parse ImageHeader to get format, width, height
        assert(imageData.size >= sizeof(ImageHeader));
        const ImageHeader* header = (const ImageHeader*)imageData.data;
        uint32_t width = header->width;
        uint32_t height = header->height;
        uint16_t format = header->format;

        const char* compressedData = imageData.data + sizeof(ImageHeader);
        size_t compressedSize = imageData.size - sizeof(ImageHeader);

        // Map our format to Vulkan format
        VkFormat vkFormat;
        const char* formatStr;
        if (format == IMAGE_FORMAT_BC1_DXT1) {
            vkFormat = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
            formatStr = "BC1/DXT1";
        } else if (format == IMAGE_FORMAT_BC3_DXT5) {
            vkFormat = VK_FORMAT_BC3_UNORM_BLOCK;
            formatStr = "BC3/DXT5";
        } else {
            std::cerr << "Texture " << textureId << ": unsupported format " << format << std::endl;
            assert(false && "Unsupported image format (expected BC1/DXT1 or BC3/DXT5)");
            return;
        }

        std::cout << "Texture " << textureId << ": uploading to GPU (" << width << "x" << height << ", " << formatStr << ", " << compressedSize << " bytes)" << std::endl;
        createTextureImage(textureId, compressedData, width, height, vkFormat, compressedSize);
        createTextureSampler(textureId);
    }
}

void VulkanTexture::loadAtlasTexture(uint64_t atlasId, const ResourceData& atlasData) {
    // If atlas texture already exists, skip reloading
    if (m_textures.find(atlasId) != m_textures.end()) {
        std::cout << "Atlas " << atlasId << ": already in GPU memory (cache hit)" << std::endl;
        return;
    }

    // Parse AtlasHeader to get format, width, height
    assert(atlasData.size >= sizeof(AtlasHeader));
    const AtlasHeader* header = (const AtlasHeader*)atlasData.data;
    uint32_t width = header->width;
    uint32_t height = header->height;
    uint16_t format = header->format;
    uint16_t numEntries = header->numEntries;

    // Skip past header and entries to get to the compressed image data
    size_t entriesSize = sizeof(AtlasEntry) * numEntries;
    const char* compressedData = atlasData.data + sizeof(AtlasHeader) + entriesSize;
    size_t compressedSize = atlasData.size - sizeof(AtlasHeader) - entriesSize;

    // Map our format to Vulkan format
    VkFormat vkFormat;
    const char* formatStr;
    if (format == IMAGE_FORMAT_BC1_DXT1) {
        vkFormat = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        formatStr = "BC1/DXT1";
    } else if (format == IMAGE_FORMAT_BC3_DXT5) {
        vkFormat = VK_FORMAT_BC3_UNORM_BLOCK;
        formatStr = "BC3/DXT5";
    } else {
        std::cerr << "Atlas " << atlasId << ": unsupported format " << format << std::endl;
        assert(false && "Unsupported atlas format (expected BC1/DXT1 or BC3/DXT5)");
        return;
    }

    std::cout << "Atlas " << atlasId << ": uploading to GPU (" << width << "x" << height << ", " << formatStr << ", " << numEntries << " entries, " << compressedSize << " bytes)" << std::endl;
    createTextureImage(atlasId, compressedData, width, height, vkFormat, compressedSize);
    createTextureSampler(atlasId);
}

void VulkanTexture::destroyTexture(uint64_t textureId) {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        return;
    }

    if (it->second.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, it->second.sampler, nullptr);
    }
    if (it->second.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, it->second.imageView, nullptr);
    }
    if (it->second.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, it->second.image, nullptr);
    }
    if (it->second.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, it->second.memory, nullptr);
    }

    m_textures.erase(it);
}

void VulkanTexture::destroyAllTextures() {
    for (auto& pair : m_textures) {
        if (pair.second.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, pair.second.sampler, nullptr);
        }
        if (pair.second.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, pair.second.imageView, nullptr);
        }
        if (pair.second.image != VK_NULL_HANDLE) {
            vkDestroyImage(m_device, pair.second.image, nullptr);
        }
        if (pair.second.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, pair.second.memory, nullptr);
        }
    }
    m_textures.clear();
}

void VulkanTexture::createRenderTargetTexture(uint64_t textureId, uint32_t width, uint32_t height, VkFormat format) {
    // Destroy existing texture if present
    if (m_textures.find(textureId) != m_textures.end()) {
        destroyTexture(textureId);
    }

    TextureData tex;
    tex.width = width;
    tex.height = height;
    tex.isRenderTarget = true;

    // Create image that can be used as both color attachment and sampled texture
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    assert(vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, tex.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) == VK_SUCCESS);
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    // Transition to shader read layout initially
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = m_commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    assert(vkCreateImageView(m_device, &viewInfo, nullptr, &tex.imageView) == VK_SUCCESS);

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    assert(vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) == VK_SUCCESS);

    m_textures[textureId] = tex;

    std::cout << "Created render target texture " << textureId << " (" << width << "x" << height << ")" << std::endl;
}
