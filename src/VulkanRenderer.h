#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "resource.h"
#include <vector>
#include <map>

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    void initialize(SDL_Window* window);
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);
    void createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void setCurrentPipeline(uint64_t id);
    void setPipelinesToDraw(const std::vector<uint64_t>& pipelineIds);
    void setDebugDrawData(const std::vector<float>& vertexData);
    void setDebugLineDrawData(const std::vector<float>& vertexData);
    void setDebugTriangleDrawData(const std::vector<float>& vertexData);
    void render(float time);
    void cleanup();

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
    VkSemaphore imageAvailableSemaphores[2];
    VkSemaphore renderFinishedSemaphores[2];
    VkFence inFlightFences[2];
    size_t currentFrame;
    uint32_t graphicsQueueFamilyIndex;
    VkFramebuffer* swapchainFramebuffers;

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
};