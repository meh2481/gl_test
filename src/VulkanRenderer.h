#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "resource.h"
#include <vector>
#include <map>
#include <set>
#include <array>

// Forward declarations
struct SpriteBatch;

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
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    uint32_t getGraphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkRenderPass getRenderPass() const { return renderPass; }
    uint32_t getSwapchainImageCount() const { return swapchainImageCount; }
    VkCommandBuffer getCurrentCommandBuffer() const { return commandBuffers[currentFrame]; }
    VkSampleCountFlagBits getMsaaSamples() const { return msaaSamples; }

    // Get texture data for ImGui rendering
    bool getTextureForImGui(uint64_t textureId, VkImageView* imageView, VkSampler* sampler) const;

    // Set ImGui render callback
    void setImGuiRenderCallback(void (*callback)(VkCommandBuffer)) { imguiRenderCallback_ = callback; }
#endif

private:
    // Shader data storage
    std::vector<char> m_vertShaderData;
    std::vector<char> m_fragShaderData;

    // Pipelines
    std::map<uint64_t, VkPipeline> m_pipelines;
    std::map<uint64_t, bool> m_debugPipelines;  // Track which pipelines are for debug drawing
    VkPipeline m_debugLinePipeline;
    VkPipeline m_debugTrianglePipeline;
    VkPipeline m_currentPipeline;
    std::vector<uint64_t> m_pipelinesToDraw;

    // Vulkan handles and state
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkSwapchainKHR swapchain;
    VkImage* swapchainImages;
    uint32_t swapchainImageCount;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    VkImageView* swapchainImageViews;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer debugVertexBuffer;
    VkDeviceMemory debugVertexBufferMemory;
    size_t debugVertexBufferSize;
    uint32_t debugVertexCount;
    VkBuffer debugTriangleVertexBuffer;
    VkDeviceMemory debugTriangleVertexBufferMemory;
    size_t debugTriangleVertexBufferSize;
    uint32_t debugTriangleVertexCount;

    // Sprite rendering
    VkBuffer spriteVertexBuffer;
    VkDeviceMemory spriteVertexBufferMemory;
    size_t spriteVertexBufferSize;
    uint32_t spriteVertexCount;
    VkBuffer spriteIndexBuffer;
    VkDeviceMemory spriteIndexBufferMemory;
    size_t spriteIndexBufferSize;
    uint32_t spriteIndexCount;

    // Particle rendering
    VkBuffer particleVertexBuffer;
    VkDeviceMemory particleVertexBufferMemory;
    size_t particleVertexBufferSize;
    uint32_t particleVertexCount;
    VkBuffer particleIndexBuffer;
    VkDeviceMemory particleIndexBufferMemory;
    size_t particleIndexBufferSize;
    uint32_t particleIndexCount;

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

    // Texture support
    struct TextureData {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkSampler sampler;
        uint32_t width;
        uint32_t height;
    };
    std::map<uint64_t, TextureData> m_textures;

    // Generic descriptor set support - keyed by descriptor ID
    // Each descriptor can have 1 or more textures
    VkDescriptorSetLayout m_singleTextureDescriptorSetLayout;
    VkDescriptorPool m_singleTextureDescriptorPool;
    std::map<uint64_t, VkDescriptorSet> m_singleTextureDescriptorSets;
    VkPipelineLayout m_singleTexturePipelineLayout;

    VkDescriptorSetLayout m_dualTextureDescriptorSetLayout;
    VkDescriptorPool m_dualTextureDescriptorPool;
    std::map<uint64_t, VkDescriptorSet> m_dualTextureDescriptorSets;
    VkPipelineLayout m_dualTexturePipelineLayout;

    // Pipeline metadata - stores which resources each pipeline uses
    struct PipelineInfo {
        VkPipelineLayout layout;
        VkDescriptorSetLayout descriptorSetLayout;
        bool usesDualTexture;  // true = 2 textures, false = 1 texture
        bool usesExtendedPushConstants;  // true = uses extended push constants with shader parameters
        bool isParticlePipeline;  // true = particle pipeline (uses vertex colors)
        std::set<uint64_t> descriptorIds;  // Which descriptor sets this pipeline uses
    };
    std::map<uint64_t, PipelineInfo> m_pipelineInfo;

    // Per-pipeline shader parameters (e.g., light position, material properties)
    std::map<int, std::array<float, 7>> m_pipelineShaderParams;
    std::map<int, int> m_pipelineShaderParamCount;  // Track how many parameters each pipeline uses

    // Per-pipeline parallax depth (0.0 = no parallax, >0 = background depth for parallax effect)
    std::map<int, float> m_pipelineParallaxDepth;

    // Camera transform
    float m_cameraOffsetX;
    float m_cameraOffsetY;
    float m_cameraZoom;

    // Light uniform buffer system
    LightBufferData m_lightBufferData;
    VkBuffer m_lightUniformBuffer;
    VkDeviceMemory m_lightUniformBufferMemory;
    void* m_lightUniformBufferMapped;
    VkDescriptorSetLayout m_lightDescriptorSetLayout;
    VkDescriptorPool m_lightDescriptorPool;
    VkDescriptorSet m_lightDescriptorSet;
    int m_nextLightId;
    std::map<int, int> m_lightIdToIndex;  // Maps light ID to index in lights array
    bool m_lightBufferDirty;  // Track if buffer needs updating

    VkSemaphore imageAvailableSemaphores[2];
    VkSemaphore renderFinishedSemaphores[2];
    VkFence inFlightFences[2];
    size_t currentFrame;
    uint32_t graphicsQueueFamilyIndex;
    VkFramebuffer* swapchainFramebuffers;

    // MSAA (Multisampling Anti-Aliasing) resources
    VkSampleCountFlagBits msaaSamples;
    VkImage msaaColorImage;
    VkDeviceMemory msaaColorImageMemory;
    VkImageView msaaColorImageView;

    // Selected GPU tracking
    int m_selectedGpuIndex;
    int m_preferredGpuIndex;

#ifdef DEBUG
    // ImGui render callback
    void (*imguiRenderCallback_)(VkCommandBuffer);
#endif

    // Helper functions
    void createInstance(SDL_Window* window);
    void createSurface(SDL_Window* window);
    void pickPhysicalDevice(int preferredGpuIndex);
    void createLogicalDevice();
    void createSwapchain(SDL_Window* window);
    void createImageViews();
    void createRenderPass();
    void createPipelineLayout();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createVertexBuffer();
    void createDebugVertexBuffer();
    void updateDebugVertexBuffer(const std::vector<float>& vertexData);
    void createDebugTriangleVertexBuffer();
    void updateDebugTriangleVertexBuffer(const std::vector<float>& vertexData);
    void createSpriteVertexBuffer();
    void updateSpriteVertexBuffer(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices);
    void createParticleVertexBuffer();
    void updateParticleVertexBuffer(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices);
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    int rateDevice(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats, uint32_t formatCount);
    VkPresentModeKHR chooseSwapPresentMode(const VkPresentModeKHR* availablePresentModes, uint32_t presentModeCount);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float time);
    void createTextureImage(uint64_t textureId, const void* imageData, uint32_t width, uint32_t height, VkFormat format, size_t dataSize);
    void createTextureSampler(uint64_t textureId);
    void createSingleTextureDescriptorSetLayout();
    void createSingleTextureDescriptorPool();
    void createSingleTextureDescriptorSet(uint64_t textureId);
    void createSingleTexturePipelineLayout();
    void createDualTextureDescriptorSetLayout();
    void createDualTextureDescriptorPool();
    void createDualTextureDescriptorSet(uint64_t descriptorId, uint64_t texture1Id, uint64_t texture2Id);
    void createDualTexturePipelineLayout();

    // Light uniform buffer helpers
    void createLightUniformBuffer();
    void createLightDescriptorSetLayout();
    void createLightDescriptorPool();
    void createLightDescriptorSet();
    void updateLightUniformBuffer();

    // MSAA helpers
    VkSampleCountFlagBits getMaxUsableSampleCount();
    void createMsaaColorResources();

    // Helper method to get or create descriptor set lazily
    VkDescriptorSet getOrCreateDescriptorSet(uint64_t descriptorId, uint64_t textureId, uint64_t normalMapId, bool usesDualTexture);
};