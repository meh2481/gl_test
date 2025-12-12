#include "VulkanRenderer.h"
#include "../core/ResourceTypes.h"
#include "../scene/SceneLayer.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <SDL3/SDL_vulkan.h>

// Helper function to convert VkResult to readable string for error logging
static const char* vkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VK_UNKNOWN_ERROR";
    }
}

// Macro to check Vulkan result and log errors
#define VK_CHECK(result, msg) \
    do { \
        VkResult res = (result); \
        if (res != VK_SUCCESS) { \
            std::cerr << "Vulkan error in " << msg << ": " << vkResultToString(res) << std::endl; \
            assert(res == VK_SUCCESS); \
        } \
    } while(0)

// Reserved texture ID for reflection render target
static const uint64_t REFLECTION_TEXTURE_ID = 0xFFFFFFFF00000001ULL;
static const uint64_t REFLECTION_TEXTURE_ID_INVALID = 0xFFFFFFFFFFFFFFFFULL;

inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

VulkanRenderer::VulkanRenderer(MemoryAllocator* allocator) :
    m_pipelineManager(allocator),
    m_instance(VK_NULL_HANDLE),
    m_surface(VK_NULL_HANDLE),
    m_physicalDevice(VK_NULL_HANDLE),
    m_device(VK_NULL_HANDLE),
    m_graphicsQueue(VK_NULL_HANDLE),
    m_swapchain(VK_NULL_HANDLE),
    m_swapchainImages(nullptr),
    m_swapchainImageCount(0),
    m_swapchainImageFormat(VK_FORMAT_UNDEFINED),
    m_swapchainExtent({0, 0}),
    m_swapchainImageViews(nullptr),
    m_renderPass(VK_NULL_HANDLE),
    m_commandPool(VK_NULL_HANDLE),
    m_commandBuffers(nullptr),
    m_vertexBuffer(VK_NULL_HANDLE),
    m_vertexBufferMemory(VK_NULL_HANDLE),
    m_particleTextureId(0),
    m_cameraOffsetX(0.0f),
    m_cameraOffsetY(0.0f),
    m_cameraZoom(1.0f),
    m_clearColorR(0.0f),
    m_clearColorG(0.0f),
    m_clearColorB(0.0f),
    m_clearColorA(1.0f),
    m_currentFrame(0),
    m_graphicsQueueFamilyIndex(0),
    m_swapchainFramebuffers(nullptr),
    m_msaaSamples(VK_SAMPLE_COUNT_1_BIT),
    m_msaaColorImage(VK_NULL_HANDLE),
    m_msaaColorImageMemory(VK_NULL_HANDLE),
    m_msaaColorImageView(VK_NULL_HANDLE),
    m_selectedGpuIndex(-1),
    m_preferredGpuIndex(-1),
    m_reflectionRenderPass(VK_NULL_HANDLE),
    m_reflectionFramebuffer(VK_NULL_HANDLE),
    m_reflectionTextureId(REFLECTION_TEXTURE_ID_INVALID),
    m_reflectionEnabled(false),
    m_reflectionSurfaceY(0.0f),
    m_spriteBatches(*allocator),
    m_particleBatches(*allocator),
    m_allBatches(*allocator),
    m_allocator(allocator)
#ifdef DEBUG
    , m_imguiRenderCallback(nullptr)
#endif
{
    for (int i = 0; i < 2; ++i) {
        m_imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        m_renderFinishedSemaphores[i] = VK_NULL_HANDLE;
        m_inFlightFences[i] = VK_NULL_HANDLE;
    }

    // Initialize dynamic buffers to zero state
    m_debugLineBuffer = {VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
    m_debugTriangleBuffer = {VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
    m_spriteBuffer = {VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
    m_particleBuffer = {VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
}

VulkanRenderer::~VulkanRenderer() {
}

void VulkanRenderer::initialize(SDL_Window* window, int preferredGpuIndex) {
    createInstance(window);
    createSurface(window);
    pickPhysicalDevice(preferredGpuIndex);
    m_msaaSamples = getMaxUsableSampleCount();
    createLogicalDevice();
    createSwapchain(window);
    createImageViews();
    createMsaaColorResources();
    createRenderPass();
    createCommandPool();

    // Initialize helper managers
    m_bufferManager.init(m_device, m_physicalDevice);
    m_textureManager.init(m_device, m_physicalDevice, m_commandPool, m_graphicsQueue);
    m_descriptorManager.init(m_device);
    m_descriptorManager.setTextureManager(&m_textureManager);
    m_lightManager.init(m_device, m_physicalDevice);

    // Create descriptor layouts and pools
    m_descriptorManager.createSingleTextureDescriptorSetLayout();
    m_descriptorManager.createSingleTexturePipelineLayout();
    m_descriptorManager.createSingleTextureDescriptorPool();
    m_descriptorManager.createDualTextureDescriptorSetLayout();
    m_descriptorManager.createLightDescriptorSetLayout();
    m_descriptorManager.createDualTexturePipelineLayout();
    m_descriptorManager.createDualTextureDescriptorPool();
    m_descriptorManager.createLightDescriptorPool();
    m_descriptorManager.createAnimSingleTexturePipelineLayout();
    m_descriptorManager.createAnimDualTexturePipelineLayout();

    // Create light uniform buffer and descriptor set
    m_lightManager.createLightUniformBuffer();
    m_descriptorManager.createLightDescriptorSet(m_lightManager.getUniformBuffer(), m_lightManager.getBufferSize());

    // Initialize pipeline manager
    m_pipelineManager.init(m_device, m_renderPass, m_msaaSamples, m_swapchainExtent);
    m_pipelineManager.setDescriptorManager(&m_descriptorManager);
    m_pipelineManager.createBasePipelineLayout();

    createFramebuffers();
    createVertexBuffer();

    // Create dynamic buffers using buffer manager
    m_bufferManager.createDynamicVertexBuffer(m_debugLineBuffer, 65536);
    m_bufferManager.createDynamicVertexBuffer(m_debugTriangleBuffer, 65536);
    m_bufferManager.createIndexedBuffer(m_spriteBuffer, 4096, 2048);
    m_bufferManager.createIndexedBuffer(m_particleBuffer, 8192, 4096);

    createCommandBuffers();
    createSyncObjects();
}

void VulkanRenderer::setShaders(const ResourceData& vertShader, const ResourceData& fragShader) {
    vkDeviceWaitIdle(m_device);
    m_pipelineManager.setShaders(vertShader, fragShader);
}

void VulkanRenderer::createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline) {
    m_pipelineManager.createPipeline(id, vertShader, fragShader, isDebugPipeline);
}

void VulkanRenderer::setCurrentPipeline(uint64_t id) {
    m_pipelineManager.setCurrentPipeline(id);
}

void VulkanRenderer::associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId) {
    m_pipelineManager.associateDescriptorWithPipeline(pipelineId, descriptorId);
}

void VulkanRenderer::setPipelinesToDraw(const Vector<uint64_t>& pipelineIds) {
    m_pipelineManager.setPipelinesToDraw(pipelineIds);
}

void VulkanRenderer::render(float time) {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // Update light uniform buffer if dirty
    if (m_lightManager.isDirty()) {
        m_lightManager.updateLightUniformBuffer();
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "vkAcquireNextImageKHR failed: " << vkResultToString(result) << std::endl;
        assert(false);
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, time);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]), "vkQueueSubmit");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(m_graphicsQueue, &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % 2;
}

bool VulkanRenderer::getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const {
    return m_textureManager.getTextureDimensions(textureId, width, height);
}

#ifdef DEBUG
bool VulkanRenderer::getTextureForImGui(uint64_t textureId, VkImageView* imageView, VkSampler* sampler) const {
    VulkanTexture::TextureData texData;
    if (!m_textureManager.getTexture(textureId, &texData)) {
        return false;
    }
    if (imageView) *imageView = texData.imageView;
    if (sampler) *sampler = texData.sampler;
    return true;
}
#endif

void VulkanRenderer::cleanup() {
    // Clean up reflection resources first
    destroyReflectionResources();

    for (size_t i = 0; i < 2; i++) {
        if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        }
        if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        }
        if (m_inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }

    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    }

    // Cleanup dynamic buffers
    m_bufferManager.destroyDynamicBuffer(m_debugLineBuffer);
    m_bufferManager.destroyDynamicBuffer(m_debugTriangleBuffer);
    m_bufferManager.destroyIndexedBuffer(m_spriteBuffer);
    m_bufferManager.destroyIndexedBuffer(m_particleBuffer);

    if (m_swapchainFramebuffers != nullptr) {
        for (size_t i = 0; i < m_swapchainImageCount; i++) {
            if (m_swapchainFramebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device, m_swapchainFramebuffers[i], nullptr);
            }
        }
        delete[] m_swapchainFramebuffers;
    }

    // Cleanup managers
    m_pipelineManager.cleanup();
    m_lightManager.cleanup();
    m_descriptorManager.cleanup();
    m_textureManager.cleanup();
    m_bufferManager.cleanup();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    }

    if (m_swapchainImageViews != nullptr) {
        for (size_t i = 0; i < m_swapchainImageCount; i++) {
            if (m_swapchainImageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(m_device, m_swapchainImageViews[i], nullptr);
            }
        }
        delete[] m_swapchainImageViews;
    }

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }
    if (m_swapchainImages != nullptr) {
        delete[] m_swapchainImages;
    }

    // Clean up MSAA resources
    if (m_msaaColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_msaaColorImageView, nullptr);
    }
    if (m_msaaColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_msaaColorImage, nullptr);
    }
    if (m_msaaColorImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_msaaColorImageMemory, nullptr);
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void VulkanRenderer::createInstance(SDL_Window* window) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Shader Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    Uint32 count;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (extensions == nullptr) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions failed: " << SDL_GetError() << std::endl;
        assert(false);
    }
    createInfo.enabledExtensionCount = count;
    createInfo.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance), "vkCreateInstance");
}

void VulkanRenderer::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << std::endl;
        assert(false);
    }
}

bool VulkanRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    VkExtensionProperties* availableExtensions = new VkExtensionProperties[extensionCount];
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions);
    const char* requiredExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    bool found = false;
    for (uint32_t i = 0; i < extensionCount; i++) {
        if (strcmp(availableExtensions[i].extensionName, requiredExtensions[0]) == 0) {
            found = true;
            break;
        }
    }
    delete[] availableExtensions;
    return found;
}

bool VulkanRenderer::isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);

    if (!checkDeviceExtensionSupport(device))
        return false;

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (formatCount == 0 || presentModeCount == 0)
        return false;

    return true;
}

VkDeviceSize VulkanRenderer::getDeviceLocalMemory(VkPhysicalDevice device) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);

    VkDeviceSize maxDeviceLocalMemory = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            if (memProps.memoryHeaps[i].size > maxDeviceLocalMemory) {
                maxDeviceLocalMemory = memProps.memoryHeaps[i].size;
            }
        }
    }
    return maxDeviceLocalMemory;
}

int VulkanRenderer::rateDevice(VkPhysicalDevice device) {
    if (!isDeviceSuitable(device))
        return -1;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:      score = 10000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:    score = 5000; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       score = 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:               score = 500; break;
        default:                                        score = 100; break;
    }

    VkDeviceSize deviceLocalMemory = getDeviceLocalMemory(device);
    VkDeviceSize memoryMB = deviceLocalMemory / (1024 * 1024);
    const VkDeviceSize maxMemoryMB = 256ULL * 1024;
    if (memoryMB > maxMemoryMB) memoryMB = maxMemoryMB;
    int memoryScore = static_cast<int>(memoryMB / 64);
    score += memoryScore;

    return score;
}

void VulkanRenderer::pickPhysicalDevice(int preferredGpuIndex) {
    m_preferredGpuIndex = preferredGpuIndex;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    assert(deviceCount > 0 && "No Vulkan devices found!");

    VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
    assert(devices != nullptr);

    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices);

    std::cout << "Available Vulkan devices:" << std::endl;
    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);

        VkDeviceSize maxDeviceLocalMemory = getDeviceLocalMemory(devices[i]);

        const char* deviceTypeStr = "Unknown";
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   deviceTypeStr = "Discrete GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceTypeStr = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    deviceTypeStr = "Virtual GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            deviceTypeStr = "CPU"; break;
            default: break;
        }

        int score = rateDevice(devices[i]);
        std::cout << "  [" << i << "] " << props.deviceName
                  << " (" << deviceTypeStr << ")"
                  << " - " << (maxDeviceLocalMemory / (1024 * 1024)) << " MB"
                  << " - Score: " << score << std::endl;
    }

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int bestScore = -1;
    int selectedIndex = -1;

    if (preferredGpuIndex >= 0 && preferredGpuIndex < static_cast<int>(deviceCount)) {
        if (isDeviceSuitable(devices[preferredGpuIndex])) {
            bestDevice = devices[preferredGpuIndex];
            bestScore = rateDevice(devices[preferredGpuIndex]);
            selectedIndex = preferredGpuIndex;
            std::cout << "Using user-specified GPU at index " << preferredGpuIndex << std::endl;
        } else {
            std::cout << "Warning: User-specified GPU at index " << preferredGpuIndex
                      << " is not suitable, falling back to auto-selection" << std::endl;
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < deviceCount; ++i) {
            int score = rateDevice(devices[i]);
            if (score > bestScore) {
                bestScore = score;
                bestDevice = devices[i];
                selectedIndex = static_cast<int>(i);
            }
        }
    }

    delete[] devices;

    assert(bestDevice != VK_NULL_HANDLE && "No suitable Vulkan device found!");

    m_selectedGpuIndex = selectedIndex;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(bestDevice, &props);
    std::cout << "Selected Vulkan device: " << props.deviceName
              << " (index " << m_selectedGpuIndex << ")" << std::endl;

    m_physicalDevice = bestDevice;
}

void VulkanRenderer::createLogicalDevice() {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    VkQueueFamilyProperties* queueFamilies = new VkQueueFamilyProperties[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies);
    int graphicsFamily = -1;
    int presentFamily = -1;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
        if (presentSupport) {
            presentFamily = i;
        }
    }
    VkDeviceQueueCreateInfo queueCreateInfos[2];
    uint32_t uniqueQueueFamilies[2];
    int numUnique = 0;
    if (graphicsFamily >= 0) uniqueQueueFamilies[numUnique++] = static_cast<uint32_t>(graphicsFamily);
    if (presentFamily >= 0 && presentFamily != graphicsFamily) uniqueQueueFamilies[numUnique++] = static_cast<uint32_t>(presentFamily);
    float queuePriority = 1.0f;
    for (int i = 0; i < numUnique; i++) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = uniqueQueueFamilies[i];
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos[i] = queueCreateInfo;
    }
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(numUnique);
    createInfo.pQueueCreateInfos = queueCreateInfos;
    createInfo.pEnabledFeatures = &deviceFeatures;
    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;
    assert(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) == VK_SUCCESS);
    vkGetDeviceQueue(m_device, graphicsFamily, 0, &m_graphicsQueue);
    m_graphicsQueueFamilyIndex = graphicsFamily;
    delete[] queueFamilies;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats, uint32_t formatCount) {
    for (uint32_t i = 0; i < formatCount; i++) {
        if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM && availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::chooseSwapPresentMode(const VkPresentModeKHR* availablePresentModes, uint32_t presentModeCount) {
    for (uint32_t i = 0; i < presentModeCount; i++) {
        if (availablePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentModes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        actualExtent.width = clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

void VulkanRenderer::createSwapchain(SDL_Window* window) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[formatCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats);
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    VkPresentModeKHR* presentModes = new VkPresentModeKHR[presentModeCount];
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats, formatCount);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes, presentModeCount);
    VkExtent2D extent = chooseSwapExtent(capabilities, window);
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    assert(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) == VK_SUCCESS);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImageCount = imageCount;
    m_swapchainImages = new VkImage[imageCount];
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages);
    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
    delete[] formats;
    delete[] presentModes;
}

void VulkanRenderer::createImageViews() {
    m_swapchainImageViews = new VkImageView[m_swapchainImageCount];
    for (size_t i = 0; i < m_swapchainImageCount; i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        assert(vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]) == VK_SUCCESS);
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = m_msaaSamples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) ?
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = m_swapchainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 1;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pResolveAttachments = (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) ? nullptr : &colorAttachmentResolveRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = {colorAttachment, colorAttachmentResolve};
    uint32_t attachmentCount = (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) ? 1 : 2;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    assert(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) == VK_SUCCESS);
}

void VulkanRenderer::createFramebuffers() {
    m_swapchainFramebuffers = new VkFramebuffer[m_swapchainImageCount];
    for (size_t i = 0; i < m_swapchainImageCount; i++) {
        VkImageView attachments[2];
        uint32_t attachmentCount;

        if (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) {
            attachments[0] = m_swapchainImageViews[i];
            attachmentCount = 1;
        } else {
            attachments[0] = m_msaaColorImageView;
            attachments[1] = m_swapchainImageViews[i];
            attachmentCount = 2;
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = attachmentCount;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;
        assert(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) == VK_SUCCESS);
    }
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(false && "failed to find suitable memory type!");
    return 0;
}

void VulkanRenderer::createVertexBuffer() {
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
        1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
        1.0f,  1.0f, 1.0f, 1.0f
    };
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(vertices);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    assert(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_vertexBuffer) == VK_SUCCESS);
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexBufferMemory, 0);
    void* data;
    vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferInfo.size, 0, &data);
    memcpy(data, vertices, (size_t)bufferInfo.size);
    vkUnmapMemory(m_device, m_vertexBufferMemory);
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    assert(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) == VK_SUCCESS);
}

void VulkanRenderer::createCommandBuffers() {
    m_commandBuffers = new VkCommandBuffer[m_swapchainImageCount];
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_swapchainImageCount;
    assert(vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers) == VK_SUCCESS);
}

void VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < 2; i++) {
        assert(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) == VK_SUCCESS &&
               vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) == VK_SUCCESS &&
               vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) == VK_SUCCESS);
    }
}

VkSampleCountFlagBits VulkanRenderer::getMaxUsableSampleCount() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts;

    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
}

void VulkanRenderer::createMsaaColorResources() {
    if (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) {
        return;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_swapchainExtent.width;
    imageInfo.extent.height = m_swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_swapchainImageFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = m_msaaSamples;
    imageInfo.flags = 0;

    assert(vkCreateImage(m_device, &imageInfo, nullptr, &m_msaaColorImage) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_msaaColorImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    assert(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_msaaColorImageMemory) == VK_SUCCESS);
    vkBindImageMemory(m_device, m_msaaColorImage, m_msaaColorImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_msaaColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_swapchainImageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    assert(vkCreateImageView(m_device, &viewInfo, nullptr, &m_msaaColorImageView) == VK_SUCCESS);
}

// Texture and pipeline delegation methods

void VulkanRenderer::loadTexture(uint64_t textureId, const ResourceData& imageData) {
    m_textureManager.loadTexture(textureId, imageData);
    // Create descriptor set for the texture
    VulkanTexture::TextureData texData;
    if (m_textureManager.getTexture(textureId, &texData)) {
        m_descriptorManager.createSingleTextureDescriptorSet(textureId, texData.imageView, texData.sampler);
    }
}

void VulkanRenderer::loadAtlasTexture(uint64_t atlasId, const ResourceData& atlasData) {
    m_textureManager.loadAtlasTexture(atlasId, atlasData);
    // Create descriptor set for the atlas
    VulkanTexture::TextureData texData;
    if (m_textureManager.getTexture(atlasId, &texData)) {
        m_descriptorManager.createSingleTextureDescriptorSet(atlasId, texData.imageView, texData.sampler);
    }
}

void VulkanRenderer::createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_pipelineManager.createTexturedPipeline(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_pipelineManager.createTexturedPipelineAdditive(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createAnimTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_pipelineManager.createAnimTexturedPipeline(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createParticlePipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode) {
    m_pipelineManager.createParticlePipeline(id, vertShader, fragShader, blendMode);
}

void VulkanRenderer::createDescriptorSetForTextures(uint64_t descriptorId, const Vector<uint64_t>& textureIds) {
    m_descriptorManager.createDescriptorSetForTextures(descriptorId, textureIds);
}

void VulkanRenderer::setShaderParameters(int pipelineId, int paramCount, const float* params) {
    m_pipelineManager.setShaderParameters(pipelineId, paramCount, params);
}

void VulkanRenderer::setPipelineParallaxDepth(int pipelineId, float depth) {
    m_pipelineManager.setPipelineParallaxDepth(pipelineId, depth);
}

void VulkanRenderer::markPipelineAsWater(int pipelineId) {
    PipelineInfo* info = m_pipelineManager.getPipelineInfoMutable(pipelineId);
    if (info != nullptr) {
        info->isWaterPipeline = true;
    }
}

void VulkanRenderer::setWaterRipples(int pipelineId, int rippleCount, const ShaderRippleData* ripples) {
    m_pipelineManager.setWaterRipples(pipelineId, rippleCount, ripples);
}

void VulkanRenderer::setCameraTransform(float offsetX, float offsetY, float zoom) {
    m_cameraOffsetX = offsetX;
    m_cameraOffsetY = offsetY;
    m_cameraZoom = zoom;
}

void VulkanRenderer::setClearColor(float r, float g, float b, float a) {
    m_clearColorR = r;
    m_clearColorG = g;
    m_clearColorB = b;
    m_clearColorA = a;
}

// Buffer update methods

void VulkanRenderer::setDebugDrawData(const Vector<float>& vertexData) {
    m_bufferManager.updateDynamicVertexBuffer(m_debugLineBuffer, vertexData, 6);
}

void VulkanRenderer::setDebugLineDrawData(const Vector<float>& vertexData) {
    m_bufferManager.updateDynamicVertexBuffer(m_debugLineBuffer, vertexData, 6);
}

void VulkanRenderer::setDebugTriangleDrawData(const Vector<float>& vertexData) {
    m_bufferManager.updateDynamicVertexBuffer(m_debugTriangleBuffer, vertexData, 6);
}

void VulkanRenderer::setSpriteDrawData(const Vector<float>& vertexData, const Vector<uint16_t>& indices) {
    m_bufferManager.updateIndexedBuffer(m_spriteBuffer, vertexData, indices, 6);
}

void VulkanRenderer::setParticleDrawData(const Vector<float>& vertexData, const Vector<uint16_t>& indices, uint64_t textureId) {
    m_bufferManager.updateIndexedBuffer(m_particleBuffer, vertexData, indices, 8);
    m_particleTextureId = textureId;
}

void VulkanRenderer::setSpriteBatches(const Vector<SpriteBatch>& batches) {
    // Wait for all in-flight frames to complete before modifying shared buffers
    VkFence fences[] = {m_inFlightFences[0], m_inFlightFences[1]};
    vkWaitForFences(m_device, 2, fences, VK_TRUE, UINT64_MAX);

    m_spriteBatches.clear();

    Vector<float> allVertexData(*m_allocator, "VulkanRenderer::generateSpriteBatches::allVertexData");
    Vector<uint16_t> allIndices(*m_allocator, "VulkanRenderer::generateSpriteBatches::allIndices");
    uint32_t baseVertex = 0;

    for (const auto& batch : batches) {
        if (batch.vertices.empty() || batch.indices.empty()) {
            continue;
        }

        BatchDrawData drawData;
        drawData.textureId = batch.textureId;
        drawData.normalMapId = batch.normalMapId;
        drawData.descriptorId = batch.descriptorId;
        drawData.pipelineId = batch.pipelineId;
        drawData.parallaxDepth = batch.parallaxDepth;
        drawData.firstIndex = static_cast<uint32_t>(allIndices.size());
        drawData.indexCount = static_cast<uint32_t>(batch.indices.size());
        drawData.isParticle = false;

        // Copy animation parameters
        drawData.spinSpeed = batch.spinSpeed;
        drawData.centerX = batch.centerX;
        drawData.centerY = batch.centerY;
        drawData.blinkSecondsOn = batch.blinkSecondsOn;
        drawData.blinkSecondsOff = batch.blinkSecondsOff;
        drawData.blinkRiseTime = batch.blinkRiseTime;
        drawData.blinkFallTime = batch.blinkFallTime;
        drawData.waveWavelength = batch.waveWavelength;
        drawData.waveSpeed = batch.waveSpeed;
        drawData.waveAngle = batch.waveAngle;
        drawData.waveAmplitude = batch.waveAmplitude;
        drawData.colorR = batch.colorR;
        drawData.colorG = batch.colorG;
        drawData.colorB = batch.colorB;
        drawData.colorA = batch.colorA;
        drawData.colorEndR = batch.colorEndR;
        drawData.colorEndG = batch.colorEndG;
        drawData.colorEndB = batch.colorEndB;
        drawData.colorEndA = batch.colorEndA;
        drawData.colorCycleTime = batch.colorCycleTime;

        for (const auto& v : batch.vertices) {
            allVertexData.push_back(v.x);
            allVertexData.push_back(v.y);
            allVertexData.push_back(v.u);
            allVertexData.push_back(v.v);
            allVertexData.push_back(v.nu);
            allVertexData.push_back(v.nv);
            allVertexData.push_back(v.uvMinX);
            allVertexData.push_back(v.uvMinY);
            allVertexData.push_back(v.uvMaxX);
            allVertexData.push_back(v.uvMaxY);
        }

        for (uint16_t idx : batch.indices) {
            allIndices.push_back(idx + baseVertex);
        }

        baseVertex += static_cast<uint32_t>(batch.vertices.size());
        m_spriteBatches.push_back(drawData);
    }

    m_bufferManager.updateIndexedBuffer(m_spriteBuffer, allVertexData, allIndices, 10);
    rebuildAllBatches();
}

void VulkanRenderer::setParticleBatches(const Vector<ParticleBatch>& batches) {
    // Wait for all in-flight frames to complete before modifying shared buffers
    VkFence fences[] = {m_inFlightFences[0], m_inFlightFences[1]};
    vkWaitForFences(m_device, 2, fences, VK_TRUE, UINT64_MAX);

    m_particleBatches.clear();

    Vector<float> allVertexData(*m_allocator, "VulkanRenderer::generateParticleBatches::allVertexData");
    Vector<uint16_t> allIndices(*m_allocator, "VulkanRenderer::generateParticleBatches::allIndices");
    uint32_t baseVertex = 0;

    for (const auto& batch : batches) {
        if (batch.vertices.empty() || batch.indices.empty()) {
            continue;
        }

        BatchDrawData drawData;
        drawData.textureId = batch.textureId;
        drawData.normalMapId = 0;
        drawData.descriptorId = batch.textureId;  // Use texture ID as descriptor ID
        drawData.pipelineId = batch.pipelineId;
        drawData.parallaxDepth = batch.parallaxDepth;
        drawData.firstIndex = static_cast<uint32_t>(allIndices.size());
        drawData.indexCount = static_cast<uint32_t>(batch.indices.size());
        drawData.isParticle = true;

        // Initialize animation parameters to defaults (no animation for particles)
        drawData.spinSpeed = 0.0f;
        drawData.centerX = 0.0f;
        drawData.centerY = 0.0f;
        drawData.blinkSecondsOn = 0.0f;
        drawData.blinkSecondsOff = 0.0f;
        drawData.blinkRiseTime = 0.0f;
        drawData.blinkFallTime = 0.0f;
        drawData.waveWavelength = 0.0f;
        drawData.waveSpeed = 0.0f;
        drawData.waveAngle = 0.0f;
        drawData.waveAmplitude = 0.0f;
        drawData.colorR = 1.0f;
        drawData.colorG = 1.0f;
        drawData.colorB = 1.0f;
        drawData.colorA = 1.0f;
        drawData.colorEndR = 1.0f;
        drawData.colorEndG = 1.0f;
        drawData.colorEndB = 1.0f;
        drawData.colorEndA = 1.0f;
        drawData.colorCycleTime = 0.0f;

        for (const auto& v : batch.vertices) {
            allVertexData.push_back(v.x);
            allVertexData.push_back(v.y);
            allVertexData.push_back(v.u);
            allVertexData.push_back(v.v);
            allVertexData.push_back(v.r);
            allVertexData.push_back(v.g);
            allVertexData.push_back(v.b);
            allVertexData.push_back(v.a);
            allVertexData.push_back(v.uvMinX);
            allVertexData.push_back(v.uvMinY);
            allVertexData.push_back(v.uvMaxX);
            allVertexData.push_back(v.uvMaxY);
        }

        for (uint16_t idx : batch.indices) {
            allIndices.push_back(idx + baseVertex);
        }

        baseVertex += static_cast<uint32_t>(batch.vertices.size());
        m_particleBatches.push_back(drawData);
    }

    m_bufferManager.updateIndexedBuffer(m_particleBuffer, allVertexData, allIndices, 8);
    rebuildAllBatches();
}

void VulkanRenderer::rebuildAllBatches() {
    m_allBatches.clear();
    m_allBatches.reserve(m_spriteBatches.size() + m_particleBatches.size());
    for (const auto& b : m_spriteBatches) {
        m_allBatches.push_back(b);
    }
    for (const auto& b : m_particleBatches) {
        m_allBatches.push_back(b);
    }
    // Sort by parallax depth (higher = background = drawn first)
    std::sort(m_allBatches.begin(), m_allBatches.end(), [](const BatchDrawData& a, const BatchDrawData& b) {
        return a.parallaxDepth > b.parallaxDepth;
    });
}

// Light management delegation

int VulkanRenderer::addLight(float x, float y, float z, float r, float g, float b, float intensity) {
    return m_lightManager.addLight(x, y, z, r, g, b, intensity);
}

void VulkanRenderer::updateLight(int lightId, float x, float y, float z, float r, float g, float b, float intensity) {
    m_lightManager.updateLight(lightId, x, y, z, r, g, b, intensity);
}

void VulkanRenderer::removeLight(int lightId) {
    m_lightManager.removeLight(lightId);
}

void VulkanRenderer::clearLights() {
    m_lightManager.clearLights();
}

void VulkanRenderer::setAmbientLight(float r, float g, float b) {
    m_lightManager.setAmbientLight(r, g, b);
}

// Command buffer recording

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float time) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    assert(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS);

    // Render reflection pass first (if enabled)
    if (m_reflectionEnabled) {
        recordReflectionPass(commandBuffer, time);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    VkClearValue clearColor = {{{m_clearColorR, m_clearColorG, m_clearColorB, m_clearColorA}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    float pushConstants[7] = {
        static_cast<float>(m_swapchainExtent.width),
        static_cast<float>(m_swapchainExtent.height),
        time,
        m_cameraOffsetX,
        m_cameraOffsetY,
        m_cameraZoom,
        0.0f
    };

    const auto& pipelinesToDraw = m_pipelineManager.getPipelinesToDraw();

    // Phase 1: Draw background shaders (non-textured pipelines like nebula)
    for (uint64_t pipelineId : pipelinesToDraw) {
        if (m_pipelineManager.isDebugPipeline(pipelineId)) {
            continue; // Skip debug, draw last
        }

        VkPipeline pipeline = m_pipelineManager.getPipeline(pipelineId);
        const PipelineInfo* info = m_pipelineManager.getPipelineInfo(pipelineId);

        // Non-textured pipeline (e.g., background shaders)
        if (pipeline != VK_NULL_HANDLE && info == nullptr) {
            float parallaxDepth = m_pipelineManager.getPipelineParallaxDepth(static_cast<int>(pipelineId));

            float pipelinePushConstants[7] = {
                static_cast<float>(m_swapchainExtent.width),
                static_cast<float>(m_swapchainExtent.height),
                time,
                m_cameraOffsetX,
                m_cameraOffsetY,
                m_cameraZoom,
                parallaxDepth
            };
            vkCmdPushConstants(commandBuffer, m_pipelineManager.getBasePipelineLayout(),
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pipelinePushConstants), pipelinePushConstants);

            VkBuffer vertexBuffers[] = {m_vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDraw(commandBuffer, 4, 1, 0, 0);
        }
    }

    // Phase 2: Draw all batches (sprites and particles) in parallax order
    // m_allBatches is pre-sorted by parallax depth (higher = background = drawn first)
    if (!m_allBatches.empty()) {
        int currentPipelineId = -1;
        bool currentIsParticle = false;
        bool spriteBound = false;
        bool particleBound = false;

        for (const auto& batch : m_allBatches) {
            VkPipeline pipeline = m_pipelineManager.getPipeline(batch.pipelineId);
            const PipelineInfo* info = m_pipelineManager.getPipelineInfo(batch.pipelineId);

            if (pipeline == VK_NULL_HANDLE || info == nullptr) {
                continue;
            }

            // Switch buffers if switching between sprite and particle batches
            if (batch.isParticle != currentIsParticle) {
                currentIsParticle = batch.isParticle;
                currentPipelineId = -1;  // Force pipeline rebind

                if (batch.isParticle) {
                    VkBuffer vertexBuffers[] = {m_particleBuffer.vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, m_particleBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    particleBound = true;
                    spriteBound = false;
                } else {
                    VkBuffer vertexBuffers[] = {m_spriteBuffer.vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, m_spriteBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    spriteBound = true;
                    particleBound = false;
                }
            }

            // Ensure buffers are bound on first batch
            if (!batch.isParticle && !spriteBound) {
                VkBuffer vertexBuffers[] = {m_spriteBuffer.vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, m_spriteBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                spriteBound = true;
            } else if (batch.isParticle && !particleBound) {
                VkBuffer vertexBuffers[] = {m_particleBuffer.vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, m_particleBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                particleBound = true;
            }

            // Switch pipeline if needed
            if (batch.pipelineId != currentPipelineId) {
                currentPipelineId = batch.pipelineId;
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            }

            // Set push constants for this batch
            if (info->isWaterPipeline) {
                // Water pipeline with ripple push constants (33 floats)
                const auto& params = m_pipelineManager.getShaderParams(batch.pipelineId);
                int rippleCount = 0;
                ShaderRippleData ripples[MAX_SHADER_RIPPLES];
                m_pipelineManager.getWaterRipples(batch.pipelineId, rippleCount, ripples);

                float waterPushConstants[33] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    m_cameraOffsetY,
                    m_cameraZoom,
                    params[0], params[1], params[2],
                    params[3], params[4], params[5], params[6],
                    // Ripple data (4 ripples x 3 values)
                    rippleCount > 0 ? ripples[0].x : 0.0f,
                    rippleCount > 0 ? ripples[0].time : -1.0f,
                    rippleCount > 0 ? ripples[0].amplitude : 0.0f,
                    rippleCount > 1 ? ripples[1].x : 0.0f,
                    rippleCount > 1 ? ripples[1].time : -1.0f,
                    rippleCount > 1 ? ripples[1].amplitude : 0.0f,
                    rippleCount > 2 ? ripples[2].x : 0.0f,
                    rippleCount > 2 ? ripples[2].time : -1.0f,
                    rippleCount > 2 ? ripples[2].amplitude : 0.0f,
                    rippleCount > 3 ? ripples[3].x : 0.0f,
                    rippleCount > 3 ? ripples[3].time : -1.0f,
                    rippleCount > 3 ? ripples[3].amplitude : 0.0f,
                    // Unused slots
                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
                };
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(waterPushConstants), waterPushConstants);
            } else if (info->usesAnimationPushConstants) {
                // Animation pipeline with extended push constants (33 floats)
                const auto& params = m_pipelineManager.getShaderParams(batch.pipelineId);

                float animPushConstants[33] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    m_cameraOffsetY,
                    m_cameraZoom,
                    params[0], params[1], params[2],
                    params[3], params[4], params[5], params[6],
                    // Animation parameters
                    batch.spinSpeed,
                    batch.centerX,
                    batch.centerY,
                    batch.blinkSecondsOn,
                    batch.blinkSecondsOff,
                    batch.blinkRiseTime,
                    batch.blinkFallTime,
                    batch.waveWavelength,
                    batch.waveSpeed,
                    batch.waveAngle,
                    batch.waveAmplitude,
                    batch.colorR,
                    batch.colorG,
                    batch.colorB,
                    batch.colorA,
                    batch.colorEndR,
                    batch.colorEndG,
                    batch.colorEndB,
                    batch.colorEndA,
                    batch.colorCycleTime
                };
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(animPushConstants), animPushConstants);
            } else if (info->usesExtendedPushConstants) {
                const auto& params = m_pipelineManager.getShaderParams(batch.pipelineId);

                float extPushConstants[13] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    m_cameraOffsetY,
                    m_cameraZoom,
                    params[0], params[1], params[2],
                    params[3], params[4], params[5], params[6]
                };
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(extPushConstants), extPushConstants);
            } else {
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), pushConstants);
            }

            if (batch.isParticle) {
                // Particle batch - use single texture descriptor set
                const auto& singleTexDescSets = m_descriptorManager.getSingleTextureDescriptorSets();
                if (!singleTexDescSets.empty()) {
                    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
                    auto it = singleTexDescSets.find(batch.textureId);
                    if (it != singleTexDescSets.end()) {
                        descriptorSet = it->second;
                    }
                    if (descriptorSet == VK_NULL_HANDLE) {
                        descriptorSet = singleTexDescSets.begin()->second;
                    }
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          info->layout, 0, 1, &descriptorSet, 0, nullptr);
                    vkCmdDrawIndexed(commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0);
                }
            } else {
                // Sprite batch
                VkDescriptorSet descriptorSet = m_descriptorManager.getOrCreateDescriptorSet(
                    batch.descriptorId,
                    batch.textureId,
                    batch.normalMapId,
                    info->usesDualTexture
                );

                if (descriptorSet != VK_NULL_HANDLE) {
                    if (info->usesDualTexture) {
                        VkDescriptorSet descriptorSets[] = {descriptorSet, m_descriptorManager.getLightDescriptorSet()};
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              info->layout, 0, 2, descriptorSets, 0, nullptr);
                    } else {
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              info->layout, 0, 1, &descriptorSet, 0, nullptr);
                    }
                    vkCmdDrawIndexed(commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0);
                }
            }
        }
    }

    // Phase 3: Draw debug shader (always last)
    for (uint64_t pipelineId : pipelinesToDraw) {
        if (!m_pipelineManager.isDebugPipeline(pipelineId)) {
            continue;
        }

        vkCmdPushConstants(commandBuffer, m_pipelineManager.getBasePipelineLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), pushConstants);

        // Draw triangles first
        if (m_debugTriangleBuffer.count > 0 && m_pipelineManager.getDebugTrianglePipeline() != VK_NULL_HANDLE) {
            VkBuffer debugBuffers[] = {m_debugTriangleBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, debugBuffers, offsets);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineManager.getDebugTrianglePipeline());
            vkCmdDraw(commandBuffer, m_debugTriangleBuffer.count, 1, 0, 0);
        }
        // Then draw lines
        if (m_debugLineBuffer.count > 0 && m_pipelineManager.getDebugLinePipeline() != VK_NULL_HANDLE) {
            VkBuffer debugBuffers[] = {m_debugLineBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, debugBuffers, offsets);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineManager.getDebugLinePipeline());
            vkCmdDraw(commandBuffer, m_debugLineBuffer.count, 1, 0, 0);
        }
    }

#ifdef DEBUG
    if (m_imguiRenderCallback) {
        m_imguiRenderCallback(commandBuffer);
    }
#endif

    vkCmdEndRenderPass(commandBuffer);
    assert(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS);
}

// Reflection/render-to-texture implementation

void VulkanRenderer::enableReflection(float surfaceY) {
    if (m_reflectionEnabled) {
        // Already enabled, just update surface Y
        m_reflectionSurfaceY = surfaceY;
        return;
    }

    m_reflectionSurfaceY = surfaceY;
    createReflectionResources();
    m_reflectionEnabled = true;

    std::cout << "Reflection enabled at surface Y=" << surfaceY << std::endl;
}

void VulkanRenderer::disableReflection() {
    if (!m_reflectionEnabled) {
        return;
    }

    destroyReflectionResources();
    m_reflectionEnabled = false;

    std::cout << "Reflection disabled" << std::endl;
}

void VulkanRenderer::createReflectionResources() {
    // Create render target texture for reflection
    m_reflectionTextureId = REFLECTION_TEXTURE_ID;
    m_textureManager.createRenderTargetTexture(m_reflectionTextureId,
                                                m_swapchainExtent.width,
                                                m_swapchainExtent.height,
                                                m_swapchainImageFormat);

    // Create a descriptor set for the reflection texture
    VulkanTexture::TextureData texData;
    if (m_textureManager.getTexture(m_reflectionTextureId, &texData)) {
        m_descriptorManager.createSingleTextureDescriptorSet(m_reflectionTextureId, texData.imageView, texData.sampler);
    }

    // Create render pass for reflection (no MSAA, single attachment)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_reflectionRenderPass);
    assert(result == VK_SUCCESS);

    // Create framebuffer for reflection
    if (m_textureManager.getTexture(m_reflectionTextureId, &texData)) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_reflectionRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &texData.imageView;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_reflectionFramebuffer);
        assert(result == VK_SUCCESS);
    }

    std::cout << "Created reflection resources: " << m_swapchainExtent.width << "x" << m_swapchainExtent.height << std::endl;
}

void VulkanRenderer::destroyReflectionResources() {
    if (m_reflectionFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_reflectionFramebuffer, nullptr);
        m_reflectionFramebuffer = VK_NULL_HANDLE;
    }

    if (m_reflectionRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_reflectionRenderPass, nullptr);
        m_reflectionRenderPass = VK_NULL_HANDLE;
    }

    if (m_reflectionTextureId != REFLECTION_TEXTURE_ID_INVALID) {
        m_textureManager.destroyTexture(m_reflectionTextureId);
        m_reflectionTextureId = REFLECTION_TEXTURE_ID_INVALID;
    }
}

void VulkanRenderer::recordReflectionPass(VkCommandBuffer commandBuffer, float time) {
    if (!m_reflectionEnabled || m_reflectionRenderPass == VK_NULL_HANDLE) {
        return;
    }

    // Begin reflection render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_reflectionRenderPass;
    renderPassInfo.framebuffer = m_reflectionFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    VkClearValue clearColor = {{{0.15f, 0.45f, 0.75f, 0.0f}}};  // Water surface color with zero alpha
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Render sprites/layers that are above the water surface (for reflection)
    // Flip the Y coordinate to create mirror effect
    float flippedCameraY = 2.0f * m_reflectionSurfaceY - m_cameraOffsetY;

    float pushConstants[7] = {
        static_cast<float>(m_swapchainExtent.width),
        static_cast<float>(m_swapchainExtent.height),
        time,
        m_cameraOffsetX,
        flippedCameraY,  // Flipped camera Y for reflection
        m_cameraZoom,
        0.0f
    };

    // Bind sprite vertex/index buffers
    if (m_spriteBuffer.vertexBuffer != VK_NULL_HANDLE && m_spriteBuffer.indexBuffer != VK_NULL_HANDLE) {
        VkBuffer vertexBuffers[] = {m_spriteBuffer.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_spriteBuffer.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    }

    // Draw only batches that are above the water surface (parallaxDepth check)
    for (const auto& batch : m_allBatches) {
        if (batch.isParticle) continue;  // Skip particles for reflection

        VkPipeline pipeline = m_pipelineManager.getPipeline(batch.pipelineId);
        const PipelineInfo* info = m_pipelineManager.getPipelineInfo(batch.pipelineId);

        if (pipeline != VK_NULL_HANDLE && info != nullptr) {
            // Skip water pipelines in reflection pass
            if (info->isWaterPipeline) continue;

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            // Set push constants with flipped Y
            if (info->usesExtendedPushConstants) {
                const auto& params = m_pipelineManager.getShaderParams(batch.pipelineId);
                float extPushConstants[13] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    flippedCameraY,
                    -m_cameraZoom,  // Negative zoom to flip Y
                    params[0], params[1], params[2],
                    params[3], params[4], params[5], params[6]
                };
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(extPushConstants), extPushConstants);
            } else {
                float reflPushConstants[7] = {
                    pushConstants[0], pushConstants[1], pushConstants[2],
                    pushConstants[3], pushConstants[4], -m_cameraZoom, pushConstants[6]
                };
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(reflPushConstants), reflPushConstants);
            }

            // Bind descriptor set and draw
            VkDescriptorSet descriptorSet = m_descriptorManager.getOrCreateDescriptorSet(
                batch.descriptorId, batch.textureId, batch.normalMapId, info->usesDualTexture);

            if (descriptorSet != VK_NULL_HANDLE) {
                if (info->usesDualTexture) {
                    VkDescriptorSet descriptorSets[] = {descriptorSet, m_descriptorManager.getLightDescriptorSet()};
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          info->layout, 0, 2, descriptorSets, 0, nullptr);
                } else {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          info->layout, 0, 1, &descriptorSet, 0, nullptr);
                }
                vkCmdDrawIndexed(commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0);
            }
        }
    }

    vkCmdEndRenderPass(commandBuffer);
}
