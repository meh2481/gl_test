#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "resource.h"

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    void initialize(SDL_Window* window, const ResourceData& vertShader, const ResourceData& fragShader);
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);
    void render(float time);
    void cleanup();

private:
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
    VkPipeline graphicsPipeline;
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
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
    void createGraphicsPipeline(const ResourceData& vertShader, const ResourceData& fragShader);
    void createFramebuffers();
    void createVertexBuffer();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    VkShaderModule createShaderModule(const ResourceData& code);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats, uint32_t formatCount);
    VkPresentModeKHR chooseSwapPresentMode(const VkPresentModeKHR* availablePresentModes, uint32_t presentModeCount);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float time);
};