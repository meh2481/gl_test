#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "../resources/resource.h"
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanDescriptor.h"
#include "VulkanPipeline.h"
#include "VulkanLight.h"
#include "VulkanWaterPolygon.h"

// Forward declarations
struct SpriteBatch;
struct ParticleBatch;
class MemoryAllocator;
class ConsoleBuffer;

class VulkanRenderer {
public:
    VulkanRenderer(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator, ConsoleBuffer* consoleBuffer);
    ~VulkanRenderer();

    void initialize(SDL_Window* window, int preferredGpuIndex = -1, VkPresentModeKHR preferredPresentMode = VK_PRESENT_MODE_FIFO_KHR);
    int getSelectedGpuIndex() const { return m_selectedGpuIndex; }
    VkPresentModeKHR getActivePresentMode() const;

    void createFadePipeline(const ResourceData& vertShader, const ResourceData& fragShader);
    void createVectorPipeline(const ResourceData& vertShader, const ResourceData& fragShader);
    void createTextPipeline(const ResourceData& vertShader, const ResourceData& fragShader);
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);
    void createPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void createTexturedPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 1);
    void createTexturedPipelineAdditive(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 1);
    void createAnimTexturedPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 1);
    void createWaterPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 2);
    void createParticlePipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode = 0);
    void destroyPipeline(Uint64 id);
    void associateDescriptorWithPipeline(Uint64 pipelineId, Uint64 descriptorId);
    void setCurrentPipeline(Uint64 id);
    void setPipelinesToDraw(const Vector<Uint64>& pipelineIds);
    void setDebugDrawData(const Vector<float>& vertexData);
    void setDebugLineDrawData(const Vector<float>& vertexData);
    void setDebugTriangleDrawData(const Vector<float>& vertexData);
    void setSpriteDrawData(const Vector<float>& vertexData, const Vector<Uint16>& indices);
    void setSpriteBatches(const Vector<SpriteBatch>& batches);
    void setParticleBatches(const Vector<ParticleBatch>& batches);
    void setParticleDrawData(const Vector<float>& vertexData, const Vector<Uint16>& indices, Uint64 textureId = 0);
    void loadTexture(Uint64 textureId, const ResourceData& imageData);
    void loadAtlasTexture(Uint64 atlasId, const ResourceData& atlasData);
    void loadVectorShape(Uint64 shapeId, const ResourceData& shapeData);
    void createDescriptorSetForTextures(Uint64 descriptorId, const Vector<Uint64>& textureIds);
    void setShaderParameters(int pipelineId, int paramCount, const float* params);
    void drawVectorShape(Uint64 shapeId, float x, float y, float scale, float r, float g, float b, float a);
    int  createVectorLayer(Uint64 shapeId, Uint64 sceneId, float x, float y, float scale, float r, float g, float b, float a);
    void setVectorLayerPosition(int layerId, float x, float y);
    void setVectorLayerColor(int layerId, float r, float g, float b, float a);
    void setVectorLayerScale(int layerId, float scale);
    void destroyVectorLayer(int layerId);
    void setActiveVectorSceneId(Uint64 sceneId);
    void clearVectorLayersForScene(Uint64 sceneId);

    // Text layer GPU management (M8 — batched GPU text rendering).
    // Creates GPU resources for one TextLayer string.  vertexData contains
    // totalVertices * 11 floats; glyphDescData is 4 uint32s per SDF glyph;
    // contourData / segmentData are the per-string flat SDF arrays.
    // Returns a non-negative textLayerGpuId on success.
    int  createTextLayerGpu(Uint64 sceneId,
                            const float*    vertexData,    int totalVertices,
                            const void*     glyphDescData, VkDeviceSize glyphDescSize,
                            const void*     contourData,   VkDeviceSize contourSize,
                            const void*     segmentData,   VkDeviceSize segmentSize);
    // Upload fresh vertex data to a previously created text layer GPU object.
    void updateTextLayerVertices(int gpuId, const float* data, int totalVertices);
    // Destroy all GPU resources for a text layer.
    void destroyTextLayerGpu(int gpuId);
    // Clear all text layer GPU objects associated with a scene.
    void clearTextLayersForScene(Uint64 sceneId);
    // Set which scene's text layers are drawn this frame.
    void setActiveTextSceneId(Uint64 sceneId);
    void setPipelineParallaxDepth(int pipelineId, float depth);
    void markPipelineAsWater(int pipelineId);
    void setWaterRipples(int pipelineId, int rippleCount, const ShaderRippleData* ripples);
    void updateWaterPolygonVertices(const float* vertices, int vertexCount);
    void createWaterDescriptorSet(Uint64 primaryTextureId, Uint64 reflectionTextureId);
    bool getTextureDimensions(Uint64 textureId, Uint32* width, Uint32* height) const;
    void setCameraTransform(float offsetX, float offsetY, float zoom);
    void setClearColor(float r, float g, float b, float a = 1.0f);
    void setFadeOverlay(float r, float g, float b, float alpha);
    void render(float time);
    void cleanup();

    // Recreate the swapchain (call after VK_ERROR_OUT_OF_DATE_KHR or window resize)
    void recreateSwapchain(SDL_Window* window);

    // Returns true if the last render() call received VK_ERROR_OUT_OF_DATE_KHR
    bool needsSwapchainRecreation() const { return m_swapchainNeedsRecreation; }

    // Reflection/render-to-texture support
    void enableReflection(float surfaceY);
    void disableReflection();
    Uint64 getReflectionTextureId() const { return m_reflectionTextureId; }
    bool isReflectionEnabled() const { return m_reflectionEnabled; }

    // Light management
    int addLight(float x, float y, float z, float r, float g, float b, float intensity);
    void updateLightPosition(int lightId, float x, float y, float z);
    void updateLightColor(int lightId, float r, float g, float b);
    void updateLightIntensity(int lightId, float intensity);
    void removeLight(int lightId);
    void clearLights();
    void setAmbientLight(float r, float g, float b);

#ifdef DEBUG
    // ImGui integration - getters for Vulkan handles
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    Uint32 getGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    Uint32 getSwapchainImageCount() const { return m_swapchainImageCount; }
    VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    VkSampleCountFlagBits getMsaaSamples() const { return m_msaaSamples; }

    // Get texture data for ImGui rendering
    bool getTextureForImGui(Uint64 textureId, VkImageView* imageView, VkSampler* sampler) const;

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
    VulkanWaterPolygon m_waterPolygonManager;

    // Vulkan core handles
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkSwapchainKHR m_swapchain;
    VkImage* m_swapchainImages;
    Uint32 m_swapchainImageCount;
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
    VulkanBuffer::IndexedBuffer m_spriteBuffers[2];
    VulkanBuffer::IndexedBuffer m_particleBuffers[2];

    // Sprite batch data
    struct BatchDrawData {
        Uint64 textureId;
        Uint64 normalMapId;
        Uint64 descriptorId;
        int pipelineId;
        float parallaxDepth;
        Uint32 indexCount;
        Uint32 firstIndex;
        Uint32 instanceCount;
        Uint32 firstInstance;
        Uint32 orderIndex;  // Stable ordering index to preserve creation order
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
    Uint64 m_particleTextureId;

    // Vector shape rendering (analytic SDF)
    struct VectorShapeGPUData {
        VkBuffer      contourBuffer;
        VkDeviceMemory contourMemory;
        VkDeviceSize  contourSize;
        VkBuffer      segmentBuffer;
        VkDeviceMemory segmentMemory;
        VkDeviceSize  segmentSize;
        VkDescriptorSet descriptorSet;
        Uint32        numContours;
        float bboxMinX, bboxMinY, bboxMaxX, bboxMaxY;
    };
    struct VectorDrawCall {
        Uint64 shapeId;
        float x, y, scale;
        float r, g, b, a;
    };
    struct VectorLayerEntry {
        Uint64 shapeId;
        Uint64 sceneId;
        float x, y, scale;
        float r, g, b, a;
    };
    HashTable<Uint64, VectorShapeGPUData> m_vectorShapes;
    Vector<VectorDrawCall> m_vectorDrawCalls;
    HashTable<int, VectorLayerEntry> m_vectorLayers;
    int m_nextVectorLayerId;
    Uint64 m_activeVectorSceneId;

    // Batched GPU text rendering (M8)
    struct TextLayerGPUData {
        VkBuffer        vertexBuffer;
        VkDeviceMemory  vertexMemory;
        VkDeviceSize    vertexBufferSize;   // allocated capacity in bytes
        VkBuffer        glyphDescBuffer;
        VkDeviceMemory  glyphDescMemory;
        VkDeviceSize    glyphDescSize;
        VkBuffer        contourBuffer;
        VkDeviceMemory  contourMemory;
        VkDeviceSize    contourSize;
        VkBuffer        segmentBuffer;
        VkDeviceMemory  segmentMemory;
        VkDeviceSize    segmentSize;
        VkDescriptorSet descriptorSet;
        int             totalVertices;
        Uint64          sceneId;
    };
    HashTable<int, TextLayerGPUData> m_textLayers;
    int    m_nextTextLayerId;
    Uint64 m_activeTextSceneId;

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
    Uint64 m_currentFrame;
    Uint32 m_graphicsQueueFamilyIndex;
    VkFramebuffer* m_swapchainFramebuffers;

    // MSAA resources
    VkSampleCountFlagBits m_msaaSamples;
    VkImage m_msaaColorImage;
    VkDeviceMemory m_msaaColorImageMemory;
    VkImageView m_msaaColorImageView;

    // GPU selection
    int m_selectedGpuIndex;
    int m_preferredGpuIndex;

    // Present mode preference read from config (VK_PRESENT_MODE_FIFO_KHR used as default/fallback)
    VkPresentModeKHR m_preferredPresentMode;
    // Actual present mode selected after checking GPU support
    VkPresentModeKHR m_activePresentMode;

    // Set to true when render() receives VK_ERROR_OUT_OF_DATE_KHR; cleared by recreateSwapchain()
    bool m_swapchainNeedsRecreation;

    // Memory allocator
    MemoryAllocator* m_allocator;

    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* m_consoleBuffer;

    // Reflection render target for water effects
    VkRenderPass m_reflectionRenderPass;
    VkFramebuffer m_reflectionFramebuffer;
    Uint64 m_reflectionTextureId;
    // MSAA resolve image for reflection (only used when m_msaaSamples > 1).
    // The reflection render pass must use the same sample count as the pipelines
    // (created for the main pass).  This MSAA image is the color attachment;
    // the actual reflection texture is the resolve attachment.
    VkImage m_reflectionMsaaImage;
    VkDeviceMemory m_reflectionMsaaImageMemory;
    VkImageView m_reflectionMsaaImageView;
    bool m_reflectionEnabled;
    float m_reflectionSurfaceY;  // Y coordinate of water surface for reflection clipping

    // Frame counter and device-lost guard
    // Once the device is lost we log diagnostics once then stop submitting to avoid
    // flooding the log with hundreds of identical VK_ERROR_DEVICE_LOST lines.
    Uint64 m_frameCount;
    bool m_deviceLost;

    // VK_NV_device_diagnostic_checkpoints (NVIDIA-specific, Windows and Linux)
    // Inserts GPU-side breadcrumbs into the command stream so we can identify which
    // draw call was actually in flight when the GPU TDR'd.
    bool m_diagnosticCheckpointsEnabled;
    PFN_vkCmdSetCheckpointNV m_vkCmdSetCheckpointNV;
    PFN_vkGetQueueCheckpointDataNV m_vkGetQueueCheckpointDataNV;

    // Emit a detailed one-shot diagnostic log when VK_ERROR_DEVICE_LOST is received.
    void logDeviceLostDiagnostics();

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
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats, Uint32 formatCount);
    VkPresentModeKHR chooseSwapPresentMode(const VkPresentModeKHR* availablePresentModes, Uint32 presentModeCount);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window);
    Uint32 findMemoryType(Uint32 typeFilter, VkMemoryPropertyFlags properties);

    // Command buffer recording
    void recordCommandBuffer(VkCommandBuffer commandBuffer, Uint32 imageIndex, float time);

    // Reflection rendering
    void createReflectionResources();
    void destroyReflectionResources();
    void recordReflectionPass(VkCommandBuffer commandBuffer, float time);

    // MSAA helpers
    VkSampleCountFlagBits getMaxUsableSampleCount();
    void createMsaaColorResources();
};