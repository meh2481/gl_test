#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "resource.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanDescriptor.h"
#include "VulkanPipeline.h"
#include "VulkanLight.h"
#include <vector>

// Forward declarations
struct SpriteBatch;

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    void initialize(SDL_Window* window, int preferredGpuIndex = -1);
    int getSelectedGpuIndex() const { return m_selectedGpuIndex; }
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);
    void createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createParticlePipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool additive = true);
    void associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId);
    void setCurrentPipeline(uint64_t id);
    void setPipelinesToDraw(const std::vector<uint64_t>& pipelineIds);
    void setDebugDrawData(const std::vector<float>& vertexData);
    void setDebugLineDrawData(const std::vector<float>& vertexData);
    void setDebugTriangleDrawData(const std::vector<float>& vertexData);
    void setSpriteDrawData(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices);
    void setSpriteBatches(const std::vector<SpriteBatch>& batches);
    void setParticleDrawData(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices);
    void loadTexture(uint64_t textureId, const ResourceData& imageData);
    void loadAtlasTexture(uint64_t atlasId, const ResourceData& atlasData);
    void createDescriptorSetForTextures(uint64_t descriptorId, const std::vector<uint64_t>& textureIds);
    void setShaderParameters(int pipelineId, int paramCount, const float* params);
    void setPipelineParallaxDepth(int pipelineId, float depth);
    bool getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const;
    void setCameraTransform(float offsetX, float offsetY, float zoom);
    void render(float time);
    void cleanup();

    // Light management
    int addLight(float x, float y, float z, float r, float g, float b, float intensity);
    void updateLight(int lightId, float x, float y, float z, float r, float g, float b, float intensity);
    void removeLight(int lightId);
    void clearLights();
    void setAmbientLight(float r, float g, float b);

#ifdef DEBUG
    // ImGui integration - getters for Vulkan handles
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    uint32_t getGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    uint32_t getSwapchainImageCount() const { return m_swapchainImageCount; }
    VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    VkSampleCountFlagBits getMsaaSamples() const { return m_msaaSamples; }

    // Get texture data for ImGui rendering
    bool getTextureForImGui(uint64_t textureId, VkImageView* imageView, VkSampler* sampler) const;

    // Set ImGui render callback
    void setImGuiRenderCallback(void (*callback)(VkCommandBuffer)) { m_imguiRenderCallback = callback; }
#endif

private:
    // Helper managers for different Vulkan subsystems
    VulkanBuffer m_bufferManager;
    VulkanTexture m_textureManager;
    VulkanDescriptor m_descriptorManager;
    VulkanPipeline m_pipelineManager;
    VulkanLight m_lightManager;

    // Vulkan core handles
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkSwapchainKHR m_swapchain;
    VkImage* m_swapchainImages;
    uint32_t m_swapchainImageCount;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    VkImageView* m_swapchainImageViews;
    VkRenderPass m_renderPass;
    VkCommandPool m_commandPool;
    VkCommandBuffer* m_commandBuffers;

    // Static fullscreen quad vertex buffer
    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;

    // Dynamic buffers managed by VulkanBuffer
    VulkanBuffer::DynamicBuffer m_debugLineBuffer;
    VulkanBuffer::DynamicBuffer m_debugTriangleBuffer;
    VulkanBuffer::IndexedBuffer m_spriteBuffer;
    VulkanBuffer::IndexedBuffer m_particleBuffer;

    // Sprite batch data
    struct BatchDrawData {
        uint64_t textureId;
        uint64_t normalMapId;
        uint64_t descriptorId;
        int pipelineId;
        uint32_t indexCount;
        uint32_t firstIndex;
    };
    std::vector<BatchDrawData> m_spriteBatches;

    // Camera transform
    float m_cameraOffsetX;
    float m_cameraOffsetY;
    float m_cameraZoom;

    // Synchronization
    VkSemaphore m_imageAvailableSemaphores[2];
    VkSemaphore m_renderFinishedSemaphores[2];
    VkFence m_inFlightFences[2];
    size_t m_currentFrame;
    uint32_t m_graphicsQueueFamilyIndex;
    VkFramebuffer* m_swapchainFramebuffers;

    // MSAA resources
    VkSampleCountFlagBits m_msaaSamples;
    VkImage m_msaaColorImage;
    VkDeviceMemory m_msaaColorImageMemory;
    VkImageView m_msaaColorImageView;

    // GPU selection
    int m_selectedGpuIndex;
    int m_preferredGpuIndex;

#ifdef DEBUG
    void (*m_imguiRenderCallback)(VkCommandBuffer);
#endif

    // Helper functions for device/swapchain setup
    void createInstance(SDL_Window* window);
    void createSurface(SDL_Window* window);
    void pickPhysicalDevice(int preferredGpuIndex);
    void createLogicalDevice();
    void createSwapchain(SDL_Window* window);
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createVertexBuffer();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();

    // Device selection helpers
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    int rateDevice(VkPhysicalDevice device);
    VkDeviceSize getDeviceLocalMemory(VkPhysicalDevice device);

    // Swapchain helpers
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats, uint32_t formatCount);
    VkPresentModeKHR chooseSwapPresentMode(const VkPresentModeKHR* availablePresentModes, uint32_t presentModeCount);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Command buffer recording
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float time);

    // MSAA helpers
    VkSampleCountFlagBits getMaxUsableSampleCount();
    void createMsaaColorResources();
};