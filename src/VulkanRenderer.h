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

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    void initialize(SDL_Window* window);
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);
    void createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId);
    void setCurrentPipeline(uint64_t id);
    void setPipelinesToDraw(const std::vector<uint64_t>& pipelineIds);
    void setDebugDrawData(const std::vector<float>& vertexData);
    void setDebugLineDrawData(const std::vector<float>& vertexData);
    void setDebugTriangleDrawData(const std::vector<float>& vertexData);
    void setSpriteDrawData(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices);
    void setSpriteBatches(const std::vector<SpriteBatch>& batches);
    void loadTexture(uint64_t textureId, const ResourceData& imageData);
    void createDescriptorSetForTextures(uint64_t descriptorId, const std::vector<uint64_t>& textureIds);
    void setShaderParameters(int pipelineId, int paramCount, const float* params);
    bool getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const;
    void setCameraTransform(float offsetX, float offsetY, float zoom);
    void render(float time);
    void cleanup();

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
        std::set<uint64_t> descriptorIds;  // Which descriptor sets this pipeline uses
    };
    std::map<uint64_t, PipelineInfo> m_pipelineInfo;
    
    // Per-pipeline shader parameters (e.g., light position, material properties)
    std::map<int, std::array<float, 7>> m_pipelineShaderParams;
    std::map<int, int> m_pipelineShaderParamCount;  // Track how many parameters each pipeline uses

    // Camera transform
    float m_cameraOffsetX;
    float m_cameraOffsetY;
    float m_cameraZoom;

    VkSemaphore imageAvailableSemaphores[2];
    VkSemaphore renderFinishedSemaphores[2];
    VkFence inFlightFences[2];
    size_t currentFrame;
    uint32_t graphicsQueueFamilyIndex;
    VkFramebuffer* swapchainFramebuffers;

#ifdef DEBUG
    // ImGui render callback
    void (*imguiRenderCallback_)(VkCommandBuffer);
#endif

    // Helper functions
    void createInstance(SDL_Window* window);
    void createSurface(SDL_Window* window);
    void pickPhysicalDevice();
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
    
    // Helper method to get or create descriptor set lazily
    VkDescriptorSet getOrCreateDescriptorSet(uint64_t descriptorId, uint64_t textureId, uint64_t normalMapId, bool usesDualTexture);
};