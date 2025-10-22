#include "VulkanRenderer.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <SDL2/SDL_vulkan.h>

inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

VulkanRenderer::VulkanRenderer() :
    instance(VK_NULL_HANDLE),
    surface(VK_NULL_HANDLE),
    physicalDevice(VK_NULL_HANDLE),
    device(VK_NULL_HANDLE),
    graphicsQueue(VK_NULL_HANDLE),
    swapchain(VK_NULL_HANDLE),
    swapchainImages(nullptr),
    swapchainImageCount(0),
    swapchainImageFormat(VK_FORMAT_UNDEFINED),
    swapchainExtent({0, 0}),
    swapchainImageViews(nullptr),
    renderPass(VK_NULL_HANDLE),
    pipelineLayout(VK_NULL_HANDLE),
    m_currentPipeline(VK_NULL_HANDLE),
    commandPool(VK_NULL_HANDLE),
    commandBuffers(nullptr),
    vertexBuffer(VK_NULL_HANDLE),
    vertexBufferMemory(VK_NULL_HANDLE),
    currentFrame(0),
    graphicsQueueFamilyIndex(0),
    swapchainFramebuffers(nullptr)
{
    // Initialize semaphores and fences to VK_NULL_HANDLE
    for (int i = 0; i < 2; ++i) {
        imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        renderFinishedSemaphores[i] = VK_NULL_HANDLE;
        inFlightFences[i] = VK_NULL_HANDLE;
    }
}

VulkanRenderer::~VulkanRenderer() {
}

void VulkanRenderer::initialize(SDL_Window* window) {
    createInstance(window);
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain(window);
    createImageViews();
    createRenderPass();
    createPipelineLayout();
    createFramebuffers();
    createVertexBuffer();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

void VulkanRenderer::setShaders(const ResourceData& vertShader, const ResourceData& fragShader) {
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, m_pipelines[0], nullptr);
    createPipeline(0, vertShader, fragShader);
    setCurrentPipeline(0);
    // Note: Command buffers may need to be re-recorded if pipeline changes, but for simplicity, assume it's ok
}

void VulkanRenderer::createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader) {
    // Copy shader data
    std::vector<char> vertData(vertShader.data, vertShader.data + vertShader.size);
    std::vector<char> fragData(fragShader.data, fragShader.data + fragShader.size);

    VkShaderModule vertShaderModule = createShaderModule(vertData);
    VkShaderModule fragShaderModule = createShaderModule(fragData);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 4;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2;

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS);

    m_pipelines[id] = pipeline;

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanRenderer::setCurrentPipeline(uint64_t id) {
    auto it = m_pipelines.find(id);
    assert(it != m_pipelines.end());
    m_currentPipeline = it->second;
}

void VulkanRenderer::render(float time) {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Recreate swapchain
        return;
    } else {
        assert(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex, time);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    assert(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) == VK_SUCCESS);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(graphicsQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % 2;
}

void VulkanRenderer::cleanup() {

    for (size_t i = 0; i < 2; i++) {
        if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        }
        if (imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        }
        if (inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }
    }

    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }

    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, nullptr);
    }

    if (swapchainFramebuffers != nullptr) {
        for (size_t i = 0; i < swapchainImageCount; i++) {
            if (swapchainFramebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, swapchainFramebuffers[i], nullptr);
            }
        }
        delete[] swapchainFramebuffers;
    }

    for (auto& pair : m_pipelines) {
        if (pair.second != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pair.second, nullptr);
        }
    }
    m_pipelines.clear();

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
    }

    if (swapchainImageViews != nullptr) {
        for (size_t i = 0; i < swapchainImageCount; i++) {
            if (swapchainImageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device, swapchainImageViews[i], nullptr);
            }
        }
        delete[] swapchainImageViews;
    }

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    if (swapchainImages != nullptr) {
        delete[] swapchainImages;
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    assert(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);
    return shaderModule;
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

    unsigned int count;
    assert(SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr));
    const char** extensions = new const char*[count];
    SDL_Vulkan_GetInstanceExtensions(window, &count, extensions);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(count);
    createInfo.ppEnabledExtensionNames = extensions;

    assert(vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS);
    delete[] extensions;
}

void VulkanRenderer::createSurface(SDL_Window* window) {
    assert(SDL_Vulkan_CreateSurface(window, instance, &surface));
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
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        // check swapchain support
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        swapChainAdequate = formatCount > 0 && presentModeCount > 0;
    }
    return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && extensionsSupported && swapChainAdequate;
}

void VulkanRenderer::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    assert(deviceCount != 0);
    VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
    for (uint32_t i = 0; i < deviceCount; i++) {
        if (isDeviceSuitable(devices[i])) {
            physicalDevice = devices[i];
            break;
        }
    }
    delete[] devices;
    assert(physicalDevice != VK_NULL_HANDLE);
}

void VulkanRenderer::createLogicalDevice() {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    VkQueueFamilyProperties* queueFamilies = new VkQueueFamilyProperties[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);
    int graphicsFamily = -1;
    int presentFamily = -1;
    for (int i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
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
    assert(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) == VK_SUCCESS);
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    graphicsQueueFamilyIndex = graphicsFamily;
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
        SDL_Vulkan_GetDrawableSize(window, &width, &height);
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
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[formatCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats);
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    VkPresentModeKHR* presentModes = new VkPresentModeKHR[presentModeCount];
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats, formatCount);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes, presentModeCount);
    VkExtent2D extent = chooseSwapExtent(capabilities, window);
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
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
    assert(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) == VK_SUCCESS);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImageCount = imageCount;
    swapchainImages = new VkImage[imageCount];
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages);
    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;
    delete[] formats;
    delete[] presentModes;
}

void VulkanRenderer::createImageViews() {
    swapchainImageViews = new VkImageView[swapchainImageCount];
    for (size_t i = 0; i < swapchainImageCount; i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        assert(vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) == VK_SUCCESS);
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    assert(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) == VK_SUCCESS);
}

void VulkanRenderer::createFramebuffers() {
    swapchainFramebuffers = new VkFramebuffer[swapchainImageCount];
    for (size_t i = 0; i < swapchainImageCount; i++) {
        VkImageView attachments[] = {swapchainImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;
        assert(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) == VK_SUCCESS);
    }
}

void VulkanRenderer::createPipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 3; // vec2 + float
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) == VK_SUCCESS);
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(false && "failed to find suitable memory type!");
    return 0; // This should never be reached
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
    assert(vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer) == VK_SUCCESS);
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);
    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferInfo.size, 0, &data);
    memcpy(data, vertices, (size_t)bufferInfo.size);
    vkUnmapMemory(device, vertexBufferMemory);
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    assert(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS);
}

void VulkanRenderer::createCommandBuffers() {
    commandBuffers = new VkCommandBuffer[swapchainImageCount];
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)swapchainImageCount;
    assert(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers) == VK_SUCCESS);
}

void VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < 2; i++) {
        assert(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) == VK_SUCCESS &&
               vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) == VK_SUCCESS &&
               vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) == VK_SUCCESS);
    }
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, float time) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    assert(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS);
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPipeline);
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    float pushConstants[3] = {static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), time};
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), pushConstants);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    assert(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS);
}