#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "../resources/resource.h"
#include "../core/Vector.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanDescriptor.h"
#include "VulkanPipeline.h"
#include "VulkanLight.h"

// Forward declarations
struct SpriteBatch;
struct ParticleBatch;
class MemoryAllocator;
class ConsoleBuffer;

class VulkanRenderer {
public:
    VulkanRenderer(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator, ConsoleBuffer* consoleBuffer);
    ~VulkanRenderer();

    void initialize(SDL_Window* window, int preferredGpuIndex = -1);
    int getSelectedGpuIndex() const { return m_selectedGpuIndex; }
    void createFadePipeline(const ResourceData& vertShader, const ResourceData& fragShader);
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);
    void createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createAnimTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createParticlePipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode = 0);
    void destroyPipeline(uint64_t id);
    void associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId);
    void setCurrentPipeline(uint64_t id);
    void setPipelinesToDraw(const Vector<uint64_t>& pipelineIds);
    void setDebugDrawData(const Vector<float>& vertexData);
    void setDebugLineDrawData(const Vector<float>& vertexData);
    void setDebugTriangleDrawData(const Vector<float>& vertexData);
    void setSpriteDrawData(const Vector<float>& vertexData, const Vector<uint16_t>& indices);
    void setSpriteBatches(const Vector<SpriteBatch>& batches);
    void setParticleBatches(const Vector<ParticleBatch>& batches);
    void setParticleDrawData(const Vector<float>& vertexData, const Vector<uint16_t>& indices, uint64_t textureId = 0);
    void loadTexture(uint64_t textureId, const ResourceData& imageData);
    void loadAtlasTexture(uint64_t atlasId, const ResourceData& atlasData);
    void createDescriptorSetForTextures(uint64_t descriptorId, const Vector<uint64_t>& textureIds);
    void setShaderParameters(int pipelineId, int paramCount, const float* params);
    void setPipelineParallaxDepth(int pipelineId, float depth);
    void markPipelineAsWater(int pipelineId);
    void setWaterRipples(int pipelineId, int rippleCount, const ShaderRippleData* ripples);
    bool getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const;
    void setCameraTransform(float offsetX, float offsetY, float zoom);
    void setClearColor(float r, float g, float b, float a = 1.0f);
    void setFadeOverlay(float r, float g, float b, float alpha);
    void render(float time);
    void cleanup();

    // Reflection/render-to-texture support
    void enableReflection(float surfaceY);
    void disableReflection();
    uint64_t getReflectionTextureId() const { return m_reflectionTextureId; }
    bool isReflectionEnabled() const { return m_reflectionEnabled; }

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
    VulkanBuffer::DynamicBuffer m_fadeOverlayBuffer;
    VulkanBuffer::IndexedBuffer m_spriteBuffer;
    VulkanBuffer::IndexedBuffer m_particleBuffer;

    // Sprite batch data
    struct BatchDrawData {
        uint64_t textureId;
        uint64_t normalMapId;
        uint64_t descriptorId;
        int pipelineId;
        float parallaxDepth;
        uint32_t indexCount;
        uint32_t firstIndex;
        bool isParticle;  // true = particle batch, false = sprite batch

        // Animation parameters
        float spinSpeed;
        float centerX, centerY;
        float blinkSecondsOn, blinkSecondsOff, blinkRiseTime, blinkFallTime;
        float waveWavelength, waveSpeed, waveAngle, waveAmplitude;
        float colorR, colorG, colorB, colorA;
        float colorEndR, colorEndG, colorEndB, colorEndA;
        float colorCycleTime;
    };
    Vector<BatchDrawData> m_spriteBatches;
    Vector<BatchDrawData> m_particleBatches;
    Vector<BatchDrawData> m_allBatches;  // Combined and sorted

    // Helper to rebuild combined batch list from sprite and particle batches
    void rebuildAllBatches();

    // Particle texture ID for rendering
    uint64_t m_particleTextureId;

    // Camera transform
    float m_cameraOffsetX;
    float m_cameraOffsetY;
    float m_cameraZoom;

    // Clear/background color
    float m_clearColorR;
    float m_clearColorG;
    float m_clearColorB;
    float m_clearColorA;

    // Fade overlay for scene transitions
    float m_fadeOverlayR;
    float m_fadeOverlayG;
    float m_fadeOverlayB;
    float m_fadeOverlayAlpha;

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

    // Memory allocator
    MemoryAllocator* m_allocator;

    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* m_consoleBuffer;

    // Reflection render target for water effects
    VkRenderPass m_reflectionRenderPass;
    VkFramebuffer m_reflectionFramebuffer;
    uint64_t m_reflectionTextureId;
    bool m_reflectionEnabled;
    float m_reflectionSurfaceY;  // Y coordinate of water surface for reflection clipping

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

    // Reflection rendering
    void createReflectionResources();
    void destroyReflectionResources();
    void recordReflectionPass(VkCommandBuffer commandBuffer, float time);

    // MSAA helpers
    VkSampleCountFlagBits getMaxUsableSampleCount();
    void createMsaaColorResources();
};