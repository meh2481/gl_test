#include "VulkanRenderer.h"
#include "ResourceTypes.h"
#include "SceneLayer.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <SDL3/SDL_vulkan.h>

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
    m_debugLinePipeline(VK_NULL_HANDLE),
    m_debugTrianglePipeline(VK_NULL_HANDLE),
    commandPool(VK_NULL_HANDLE),
    commandBuffers(nullptr),
    vertexBuffer(VK_NULL_HANDLE),
    vertexBufferMemory(VK_NULL_HANDLE),
    debugVertexBuffer(VK_NULL_HANDLE),
    debugVertexBufferMemory(VK_NULL_HANDLE),
    debugVertexBufferSize(0),
    debugVertexCount(0),
    debugTriangleVertexBuffer(VK_NULL_HANDLE),
    debugTriangleVertexBufferMemory(VK_NULL_HANDLE),
    debugTriangleVertexBufferSize(0),
    debugTriangleVertexCount(0),
    spriteVertexBuffer(VK_NULL_HANDLE),
    spriteVertexBufferMemory(VK_NULL_HANDLE),
    spriteVertexBufferSize(0),
    spriteVertexCount(0),
    spriteIndexBuffer(VK_NULL_HANDLE),
    spriteIndexBufferMemory(VK_NULL_HANDLE),
    spriteIndexBufferSize(0),
    spriteIndexCount(0),
    m_singleTextureDescriptorSetLayout(VK_NULL_HANDLE),
    m_singleTextureDescriptorPool(VK_NULL_HANDLE),
    m_singleTexturePipelineLayout(VK_NULL_HANDLE),
    m_dualTextureDescriptorSetLayout(VK_NULL_HANDLE),
    m_dualTextureDescriptorPool(VK_NULL_HANDLE),
    m_dualTexturePipelineLayout(VK_NULL_HANDLE),
    m_cameraOffsetX(0.0f),
    m_cameraOffsetY(0.0f),
    m_cameraZoom(1.0f),
    currentFrame(0),
    graphicsQueueFamilyIndex(0),
    swapchainFramebuffers(nullptr)
#ifdef DEBUG
    , imguiRenderCallback_(nullptr)
#endif
{
    // Initialize semaphores and fences to VK_NULL_HANDLE
    for (int i = 0; i < 2; ++i) {
        imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        renderFinishedSemaphores[i] = VK_NULL_HANDLE;
        inFlightFences[i] = VK_NULL_HANDLE;
    }
    
    // Shader parameters are now stored per-pipeline in m_pipelineShaderParams
    // They are set via setShaderParameters(pipelineId, ...) from Lua
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
    createSingleTextureDescriptorSetLayout();
    createSingleTexturePipelineLayout();
    createSingleTextureDescriptorPool();
    createDualTextureDescriptorSetLayout();
    createDualTexturePipelineLayout();
    createDualTextureDescriptorPool();
    createFramebuffers();
    createVertexBuffer();
    createDebugVertexBuffer();
    createDebugTriangleVertexBuffer();
    createSpriteVertexBuffer();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

void VulkanRenderer::setShaders(const ResourceData& vertShader, const ResourceData& fragShader) {
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, m_pipelines[0], nullptr);
    if (m_debugLinePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_debugLinePipeline, nullptr);
        m_debugLinePipeline = VK_NULL_HANDLE;
    }
    if (m_debugTrianglePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_debugTrianglePipeline, nullptr);
        m_debugTrianglePipeline = VK_NULL_HANDLE;
    }
    createPipeline(0, vertShader, fragShader);
    setCurrentPipeline(0);
    // Note: Command buffers may need to be re-recorded if pipeline changes, but for simplicity, assume it's ok
}

void VulkanRenderer::createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline) {
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
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};

    if (isDebugPipeline) {
        // Debug pipeline: position (vec2) + color (vec4) = 6 floats
        bindingDescription.stride = sizeof(float) * 6;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;  // position
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;  // color
        attributeDescriptions[1].offset = sizeof(float) * 2;
    } else {
        // Regular pipeline: position (vec2) + texcoord (vec2) = 4 floats
        bindingDescription.stride = sizeof(float) * 4;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;  // position
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;  // texcoord
        attributeDescriptions[1].offset = sizeof(float) * 2;
    }

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = isDebugPipeline ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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

    if (isDebugPipeline) {
        // Create line pipeline
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_debugLinePipeline) == VK_SUCCESS);

        // Create triangle pipeline
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_debugTrianglePipeline) == VK_SUCCESS);

        m_debugPipelines[id] = true;
    } else {
        VkPipeline pipeline;
        assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS);

        m_pipelines[id] = pipeline;
        m_debugPipelines[id] = false;
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanRenderer::setCurrentPipeline(uint64_t id) {
    auto it = m_pipelines.find(id);
    assert(it != m_pipelines.end());
    m_currentPipeline = it->second;
}

void VulkanRenderer::associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId) {
    auto it = m_pipelineInfo.find(pipelineId);
    if (it != m_pipelineInfo.end()) {
        it->second.descriptorIds.insert(descriptorId);
    }
}

void VulkanRenderer::setPipelinesToDraw(const std::vector<uint64_t>& pipelineIds) {
    m_pipelinesToDraw = pipelineIds;
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

bool VulkanRenderer::getTextureDimensions(uint64_t textureId, uint32_t* width, uint32_t* height) const {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        return false;
    }
    if (width) *width = it->second.width;
    if (height) *height = it->second.height;
    return true;
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

    if (debugVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, debugVertexBuffer, nullptr);
    }
    if (debugVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, debugVertexBufferMemory, nullptr);
    }

    if (debugTriangleVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, debugTriangleVertexBuffer, nullptr);
    }
    if (debugTriangleVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, debugTriangleVertexBufferMemory, nullptr);
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

    if (m_debugLinePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_debugLinePipeline, nullptr);
    }
    if (m_debugTrianglePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_debugTrianglePipeline, nullptr);
    }

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

    Uint32 count;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    assert(extensions != nullptr);
    createInfo.enabledExtensionCount = count;
    createInfo.ppEnabledExtensionNames = extensions;

    assert(vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS);
}

void VulkanRenderer::createSurface(SDL_Window* window) {
    assert(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
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

bool VulkanRenderer::isDeviceSuitable(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);

    // Required extensions
    if (!checkDeviceExtensionSupport(device))
        return false;

    // Swapchain support (very cheap check)
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (formatCount == 0 || presentModeCount == 0)
        return false;

    // You can add more checks here (queue families, etc.) if needed
    return true;
}

// Simple scoring: higher = better
int VulkanRenderer::rateDevice(VkPhysicalDevice device)
{
    if (!isDeviceSuitable(device))
        return -1;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    switch (props.deviceType)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:      return 10000;  // strongly prefer
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:    return 5000;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       return 1000;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:               return 500;
        default:                                        return 100;
    }
}

void VulkanRenderer::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    assert(deviceCount > 0 && "No Vulkan devices found!");

    VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
    assert(devices != nullptr);

    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int bestScore = -1;

    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        int score = rateDevice(devices[i]);
        if (score > bestScore)
        {
            bestScore = score;
            bestDevice = devices[i];
        }
    }

    delete[] devices;

    assert(bestDevice != VK_NULL_HANDLE && "No suitable Vulkan device found!");

    #ifdef DEBUG
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(bestDevice, &props);
        std::cout<< "Vulkan device selected: " << props.deviceName << std::endl;
    #endif

    physicalDevice = bestDevice;
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
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 6; // width, height, time, cameraX, cameraY, cameraZoom
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


void VulkanRenderer::createDebugVertexBuffer() {
    // Create a buffer large enough for debug drawing (allocate 64KB initially)
    debugVertexBufferSize = 65536;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = debugVertexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    assert(vkCreateBuffer(device, &bufferInfo, nullptr, &debugVertexBuffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, debugVertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkAllocateMemory(device, &allocInfo, nullptr, &debugVertexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, debugVertexBuffer, debugVertexBufferMemory, 0);
}

void VulkanRenderer::updateDebugVertexBuffer(const std::vector<float>& vertexData) {
    if (vertexData.empty()) {
        debugVertexCount = 0;
        return;
    }

    size_t dataSize = vertexData.size() * sizeof(float);

    // Reallocate if needed
    if (dataSize > debugVertexBufferSize) {
        if (debugVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, debugVertexBuffer, nullptr);
        }
        if (debugVertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, debugVertexBufferMemory, nullptr);
        }

        debugVertexBufferSize = dataSize * 2;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = debugVertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(device, &bufferInfo, nullptr, &debugVertexBuffer) == VK_SUCCESS);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, debugVertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(vkAllocateMemory(device, &allocInfo, nullptr, &debugVertexBufferMemory) == VK_SUCCESS);
        vkBindBufferMemory(device, debugVertexBuffer, debugVertexBufferMemory, 0);
    }

    // Copy data to buffer
    void* data;
    vkMapMemory(device, debugVertexBufferMemory, 0, dataSize, 0, &data);
    memcpy(data, vertexData.data(), dataSize);
    vkUnmapMemory(device, debugVertexBufferMemory);

    // Update vertex count (6 floats per vertex: x, y, r, g, b, a)
    debugVertexCount = vertexData.size() / 6;
}

void VulkanRenderer::createDebugTriangleVertexBuffer() {
    // Create a buffer large enough for debug drawing (allocate 64KB initially)
    debugTriangleVertexBufferSize = 65536;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = debugTriangleVertexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    assert(vkCreateBuffer(device, &bufferInfo, nullptr, &debugTriangleVertexBuffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, debugTriangleVertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkAllocateMemory(device, &allocInfo, nullptr, &debugTriangleVertexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, debugTriangleVertexBuffer, debugTriangleVertexBufferMemory, 0);
}

void VulkanRenderer::updateDebugTriangleVertexBuffer(const std::vector<float>& vertexData) {
    if (vertexData.empty()) {
        debugTriangleVertexCount = 0;
        return;
    }

    size_t dataSize = vertexData.size() * sizeof(float);

    // Reallocate if needed
    if (dataSize > debugTriangleVertexBufferSize) {
        if (debugTriangleVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, debugTriangleVertexBuffer, nullptr);
        }
        if (debugTriangleVertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, debugTriangleVertexBufferMemory, nullptr);
        }

        debugTriangleVertexBufferSize = dataSize * 2;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = debugTriangleVertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(device, &bufferInfo, nullptr, &debugTriangleVertexBuffer) == VK_SUCCESS);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, debugTriangleVertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(vkAllocateMemory(device, &allocInfo, nullptr, &debugTriangleVertexBufferMemory) == VK_SUCCESS);
        vkBindBufferMemory(device, debugTriangleVertexBuffer, debugTriangleVertexBufferMemory, 0);
    }

    // Copy data to buffer
    void* data;
    vkMapMemory(device, debugTriangleVertexBufferMemory, 0, dataSize, 0, &data);
    memcpy(data, vertexData.data(), dataSize);
    vkUnmapMemory(device, debugTriangleVertexBufferMemory);

    // Update vertex count (6 floats per vertex: x, y, r, g, b, a)
    debugTriangleVertexCount = vertexData.size() / 6;
}

void VulkanRenderer::setDebugTriangleDrawData(const std::vector<float>& vertexData) {
    updateDebugTriangleVertexBuffer(vertexData);
}

void VulkanRenderer::setDebugDrawData(const std::vector<float>& vertexData) {
    updateDebugVertexBuffer(vertexData);
}

void VulkanRenderer::setDebugLineDrawData(const std::vector<float>& vertexData) {
    updateDebugVertexBuffer(vertexData);
}

void VulkanRenderer::createSpriteVertexBuffer() {
    // Start with a reasonable size
    spriteVertexBufferSize = 4096;  // 4KB initial size
    spriteIndexBufferSize = 2048;   // 2KB initial size for indices

    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = spriteVertexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    assert(vkCreateBuffer(device, &bufferInfo, nullptr, &spriteVertexBuffer) == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, spriteVertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkAllocateMemory(device, &allocInfo, nullptr, &spriteVertexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, spriteVertexBuffer, spriteVertexBufferMemory, 0);

    // Create index buffer
    bufferInfo.size = spriteIndexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    assert(vkCreateBuffer(device, &bufferInfo, nullptr, &spriteIndexBuffer) == VK_SUCCESS);

    vkGetBufferMemoryRequirements(device, spriteIndexBuffer, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(vkAllocateMemory(device, &allocInfo, nullptr, &spriteIndexBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, spriteIndexBuffer, spriteIndexBufferMemory, 0);

    spriteVertexCount = 0;
    spriteIndexCount = 0;
}

void VulkanRenderer::updateSpriteVertexBuffer(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices) {
    if (vertexData.empty() || indices.empty()) {
        spriteVertexCount = 0;
        spriteIndexCount = 0;
        return;
    }

    size_t vertexDataSize = vertexData.size() * sizeof(float);
    size_t indexDataSize = indices.size() * sizeof(uint16_t);

    // Reallocate vertex buffer if needed
    if (vertexDataSize > spriteVertexBufferSize) {
        if (spriteVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, spriteVertexBuffer, nullptr);
        }
        if (spriteVertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, spriteVertexBufferMemory, nullptr);
        }

        spriteVertexBufferSize = vertexDataSize * 2;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = spriteVertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(device, &bufferInfo, nullptr, &spriteVertexBuffer) == VK_SUCCESS);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, spriteVertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(vkAllocateMemory(device, &allocInfo, nullptr, &spriteVertexBufferMemory) == VK_SUCCESS);
        vkBindBufferMemory(device, spriteVertexBuffer, spriteVertexBufferMemory, 0);
    }

    // Reallocate index buffer if needed
    if (indexDataSize > spriteIndexBufferSize) {
        if (spriteIndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, spriteIndexBuffer, nullptr);
        }
        if (spriteIndexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, spriteIndexBufferMemory, nullptr);
        }

        spriteIndexBufferSize = indexDataSize * 2;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = spriteIndexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        assert(vkCreateBuffer(device, &bufferInfo, nullptr, &spriteIndexBuffer) == VK_SUCCESS);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, spriteIndexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(vkAllocateMemory(device, &allocInfo, nullptr, &spriteIndexBufferMemory) == VK_SUCCESS);
        vkBindBufferMemory(device, spriteIndexBuffer, spriteIndexBufferMemory, 0);
    }

    // Copy vertex data
    void* data;
    vkMapMemory(device, spriteVertexBufferMemory, 0, vertexDataSize, 0, &data);
    memcpy(data, vertexData.data(), vertexDataSize);
    vkUnmapMemory(device, spriteVertexBufferMemory);

    // Copy index data
    vkMapMemory(device, spriteIndexBufferMemory, 0, indexDataSize, 0, &data);
    memcpy(data, indices.data(), indexDataSize);
    vkUnmapMemory(device, spriteIndexBufferMemory);

    // Update counts (4 floats per vertex: x, y, u, v)
    spriteVertexCount = vertexData.size() / 4;
    spriteIndexCount = indices.size();
}

void VulkanRenderer::setSpriteDrawData(const std::vector<float>& vertexData, const std::vector<uint16_t>& indices) {
    updateSpriteVertexBuffer(vertexData, indices);
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

    float pushConstants[6] = {
        static_cast<float>(swapchainExtent.width),
        static_cast<float>(swapchainExtent.height),
        time,
        m_cameraOffsetX,
        m_cameraOffsetY,
        m_cameraZoom
    };

    // Draw all pipelines
    for (uint64_t pipelineId : m_pipelinesToDraw) {
        bool isDebug = m_debugPipelines.count(pipelineId) > 0 && m_debugPipelines[pipelineId];

        if (isDebug) {
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), pushConstants);
            
            // Draw triangles first
            if (debugTriangleVertexCount > 0 && m_debugTrianglePipeline != VK_NULL_HANDLE) {
                VkBuffer debugBuffers[] = {debugTriangleVertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, debugBuffers, offsets);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugTrianglePipeline);
                vkCmdDraw(commandBuffer, debugTriangleVertexCount, 1, 0, 0);
            }
            // Then draw lines
            if (debugVertexCount > 0 && m_debugLinePipeline != VK_NULL_HANDLE) {
                VkBuffer debugBuffers[] = {debugVertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, debugBuffers, offsets);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugLinePipeline);
                vkCmdDraw(commandBuffer, debugVertexCount, 1, 0, 0);
            }
        } else {
            // Check if this is a textured pipeline
            auto pipelineIt = m_pipelines.find(pipelineId);
            auto infoIt = m_pipelineInfo.find(pipelineId);
            
            if (pipelineIt != m_pipelines.end() && infoIt != m_pipelineInfo.end() && !m_spriteBatches.empty()) {
                // Textured pipeline rendering
                const PipelineInfo& info = infoIt->second;

                // Prepare push constants based on pipeline requirements
                if (info.usesExtendedPushConstants) {
                    // Extended push constants with shader parameters
                    // Get parameters for this pipeline (or use defaults if not set)
                    const auto& params = m_pipelineShaderParams.count(pipelineId)
                        ? m_pipelineShaderParams[pipelineId]
                        : std::array<float, 7>{0, 0, 0, 0, 0, 0, 0};

                    float extPushConstants[13] = {
                        static_cast<float>(swapchainExtent.width),
                        static_cast<float>(swapchainExtent.height),
                        time,
                        m_cameraOffsetX,
                        m_cameraOffsetY,
                        m_cameraZoom,
                        params[0], params[1], params[2],
                        params[3], params[4], params[5], params[6]
                    };
                    vkCmdPushConstants(commandBuffer, info.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(extPushConstants), extPushConstants);
                } else {
                    // Standard push constants
                    vkCmdPushConstants(commandBuffer, info.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), pushConstants);
                }
                
                VkBuffer vertexBuffers[] = {spriteVertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, spriteIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineIt->second);
                
                // Draw each batch that belongs to this pipeline
                for (const auto& batch : m_spriteBatches) {
                    // Only draw batches that explicitly use this pipeline
                    // If batch has pipelineId == -1, it can be drawn by any pipeline that has a descriptor for it
                    if (batch.pipelineId != -1 && batch.pipelineId != pipelineId) {
                        continue;
                    }
                    
                    // For batches with pipelineId == -1, check if this pipeline has the descriptor
                    if (batch.pipelineId == -1 && !info.descriptorIds.empty() && 
                        info.descriptorIds.find(batch.descriptorId) == info.descriptorIds.end()) {
                        continue;
                    }
                    
                    // Get or create descriptor set lazily
                    VkDescriptorSet descriptorSet = getOrCreateDescriptorSet(
                        batch.descriptorId, 
                        batch.textureId, 
                        batch.normalMapId, 
                        info.usesDualTexture
                    );
                    
                    if (descriptorSet != VK_NULL_HANDLE) {
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                              info.layout, 0, 1, &descriptorSet, 0, nullptr);
                        vkCmdDrawIndexed(commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0);
                    }
                }
            } else if (pipelineIt != m_pipelines.end()) {
                // Non-textured pipeline (e.g., background shaders)
                vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), pushConstants);
                
                // Bind regular vertex buffer
                VkBuffer vertexBuffers[] = {vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineIt->second);
                vkCmdDraw(commandBuffer, 4, 1, 0, 0);
            }
        }
    }

#ifdef DEBUG
    // Render ImGui on top of everything
    if (imguiRenderCallback_) {
        imguiRenderCallback_(commandBuffer);
    }
#endif

    vkCmdEndRenderPass(commandBuffer);
    assert(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS);
}
void VulkanRenderer::createSingleTextureDescriptorSetLayout() {
    // Descriptor set layout for texture sampling
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    assert(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_singleTextureDescriptorSetLayout) == VK_SUCCESS);
}

void VulkanRenderer::createSingleTexturePipelineLayout() {
    // Push constants for width, height, time, cameraX, cameraY, cameraZoom
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 6;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_singleTextureDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_singleTexturePipelineLayout) == VK_SUCCESS);
}

void VulkanRenderer::createSingleTextureDescriptorPool() {
    // Create descriptor pool for texture descriptors
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 100;  // Support up to 100 textures

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 100;

    assert(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_singleTextureDescriptorPool) == VK_SUCCESS);
}

void VulkanRenderer::loadTexture(uint64_t textureId, const ResourceData& imageData) {
    // If texture already exists, skip reloading (textures don't change during hot-reload)
    // This prevents descriptor pool exhaustion from repeated allocations
    if (m_textures.find(textureId) != m_textures.end()) {
        return;
    }

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
    if (format == IMAGE_FORMAT_BC1_DXT1) {
        vkFormat = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    } else if (format == IMAGE_FORMAT_BC3_DXT5) {
        vkFormat = VK_FORMAT_BC3_UNORM_BLOCK;
    } else {
        assert(false && "Unsupported image format");
        return;
    }
    
    createTextureImage(textureId, compressedData, width, height, vkFormat, compressedSize);
    createTextureSampler(textureId);
    createSingleTextureDescriptorSet(textureId);
}

void VulkanRenderer::createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_vertShaderData.assign(vertShader.data, vertShader.data + vertShader.size);
    m_fragShaderData.assign(fragShader.data, fragShader.data + fragShader.size);
    
    VkShaderModule vertShaderModule = createShaderModule(m_vertShaderData);
    VkShaderModule fragShaderModule = createShaderModule(m_fragShaderData);
    
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
    
    // Vertex input for sprites: position (vec2) + texCoord (vec2)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 4; // 2 floats for position + 2 for texcoord
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;  // position
    attributeDescriptions[0].offset = 0;
    
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;  // texcoord
    attributeDescriptions[1].offset = sizeof(float) * 2;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
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
    
    // Select appropriate pipeline layout based on texture count
    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;
    bool usesDualTexture = (numTextures == 2);
    
    if (usesDualTexture) {
        pipelineLayout = m_dualTexturePipelineLayout;
        descriptorSetLayout = m_dualTextureDescriptorSetLayout;
    } else {
        pipelineLayout = m_singleTexturePipelineLayout;
        descriptorSetLayout = m_singleTextureDescriptorSetLayout;
    }
    
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    VkPipeline pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS);
    
    m_pipelines[id] = pipeline;
    
    // Store pipeline info
    PipelineInfo info;
    info.layout = pipelineLayout;
    info.descriptorSetLayout = descriptorSetLayout;
    info.usesDualTexture = usesDualTexture;
    info.usesExtendedPushConstants = false;  // Will be set to true when setShaderParameters is called
    m_pipelineInfo[id] = info;
    
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanRenderer::createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_vertShaderData.assign(vertShader.data, vertShader.data + vertShader.size);
    m_fragShaderData.assign(fragShader.data, fragShader.data + fragShader.size);

    VkShaderModule vertShaderModule = createShaderModule(m_vertShaderData);
    VkShaderModule fragShaderModule = createShaderModule(m_fragShaderData);

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

    // Vertex input for sprites: position (vec2) + texCoord (vec2)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 4; // 2 floats for position + 2 for texcoord
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;  // position
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;  // texcoord
    attributeDescriptions[1].offset = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

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

    // Select appropriate pipeline layout based on texture count
    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;
    bool usesDualTexture = (numTextures == 2);

    if (usesDualTexture) {
        pipelineLayout = m_dualTexturePipelineLayout;
        descriptorSetLayout = m_dualTextureDescriptorSetLayout;
    } else {
        pipelineLayout = m_singleTexturePipelineLayout;
        descriptorSetLayout = m_singleTextureDescriptorSetLayout;
    }

    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS);

    m_pipelines[id] = pipeline;

    // Store pipeline info
    PipelineInfo info;
    info.layout = pipelineLayout;
    info.descriptorSetLayout = descriptorSetLayout;
    info.usesDualTexture = usesDualTexture;
    info.usesExtendedPushConstants = false;  // Will be set to true when setShaderParameters is called
    m_pipelineInfo[id] = info;

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanRenderer::createTextureImage(uint64_t textureId, const void* imageData, uint32_t width, uint32_t height, VkFormat format, size_t dataSize) {
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    assert(vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) == VK_SUCCESS);
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    assert(vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) == VK_SUCCESS);
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
    
    // Copy image data to staging buffer
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, dataSize, 0, &data);
    memcpy(data, imageData, dataSize);
    vkUnmapMemory(device, stagingBufferMemory);
    
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
    assert(vkCreateImage(device, &imageInfo, nullptr, &tex.image) == VK_SUCCESS);
    
    VkMemoryRequirements imgMemRequirements;
    vkGetImageMemoryRequirements(device, tex.image, &imgMemRequirements);
    
    VkMemoryAllocateInfo imgAllocInfo{};
    imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAllocInfo.allocationSize = imgMemRequirements.size;
    imgAllocInfo.memoryTypeIndex = findMemoryType(imgMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    assert(vkAllocateMemory(device, &imgAllocInfo, nullptr, &tex.memory) == VK_SUCCESS);
    vkBindImageMemory(device, tex.image, tex.memory, 0);
    
    // Transition image layout and copy from staging buffer
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;
    
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer);
    
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
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
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
    
    assert(vkCreateImageView(device, &viewInfo, nullptr, &tex.imageView) == VK_SUCCESS);

    m_textures[textureId] = tex;
}

void VulkanRenderer::createTextureSampler(uint64_t textureId) {
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
    
    assert(vkCreateSampler(device, &samplerInfo, nullptr, &it->second.sampler) == VK_SUCCESS);
}

void VulkanRenderer::createSingleTextureDescriptorSet(uint64_t textureId) {
    // If descriptor set already exists for this texture, skip allocation
    if (m_singleTextureDescriptorSets.find(textureId) != m_singleTextureDescriptorSets.end()) {
        return;
    }

    auto it = m_textures.find(textureId);
    assert(it != m_textures.end());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_singleTextureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_singleTextureDescriptorSetLayout;
    
    VkDescriptorSet descriptorSet;
    assert(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) == VK_SUCCESS);
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = it->second.imageView;
    imageInfo.sampler = it->second.sampler;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    
    m_singleTextureDescriptorSets[textureId] = descriptorSet;
}

void VulkanRenderer::setSpriteBatches(const std::vector<SpriteBatch>& batches) {
    m_spriteBatches.clear();
    
    // Combine all vertices and indices from all batches
    std::vector<float> allVertexData;
    std::vector<uint16_t> allIndices;
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
        drawData.firstIndex = static_cast<uint32_t>(allIndices.size());
        drawData.indexCount = static_cast<uint32_t>(batch.indices.size());
        
        // Add vertex data
        for (const auto& v : batch.vertices) {
            allVertexData.push_back(v.x);
            allVertexData.push_back(v.y);
            allVertexData.push_back(v.u);
            allVertexData.push_back(v.v);
        }
        
        // Add indices with offset
        for (uint16_t idx : batch.indices) {
            allIndices.push_back(idx + baseVertex);
        }
        
        baseVertex += static_cast<uint32_t>(batch.vertices.size());
        m_spriteBatches.push_back(drawData);
    }
    
    // Upload to GPU
    updateSpriteVertexBuffer(allVertexData, allIndices);
}

void VulkanRenderer::createDualTextureDescriptorSetLayout() {
    // Descriptor set layout with two textures (e.g., diffuse and normal map)
    VkDescriptorSetLayoutBinding bindings[2];
    
    // Binding 0: First texture (e.g., diffuse/albedo)
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 1: Second texture (e.g., normal map)
    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    
    assert(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_dualTextureDescriptorSetLayout) == VK_SUCCESS);
}

void VulkanRenderer::createDualTexturePipelineLayout() {
    // Push constants: width, height, time, cameraX, cameraY, cameraZoom, plus 7 shader-specific parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 13;  // width, height, time, cameraX, cameraY, cameraZoom, param0-6

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_dualTextureDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_dualTexturePipelineLayout) == VK_SUCCESS);
}

void VulkanRenderer::createDualTextureDescriptorPool() {
    // Create descriptor pool for multi-texture descriptors (2 textures per set)
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 200;  // Support up to 100 sets (2 textures each)
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 100;
    
    assert(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_dualTextureDescriptorPool) == VK_SUCCESS);
}

void VulkanRenderer::createDualTextureDescriptorSet(uint64_t descriptorId, uint64_t texture1Id, uint64_t texture2Id) {
    // If descriptor set already exists for this ID, skip allocation
    if (m_dualTextureDescriptorSets.find(descriptorId) != m_dualTextureDescriptorSets.end()) {
        return;
    }

    auto tex1It = m_textures.find(texture1Id);
    auto tex2It = m_textures.find(texture2Id);
    assert(tex1It != m_textures.end());
    assert(tex2It != m_textures.end());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_dualTextureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_dualTextureDescriptorSetLayout;
    
    VkDescriptorSet descriptorSet;
    assert(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) == VK_SUCCESS);
    
    VkDescriptorImageInfo imageInfos[2];
    
    // First texture
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = tex1It->second.imageView;
    imageInfos[0].sampler = tex1It->second.sampler;
    
    // Second texture
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = tex2It->second.imageView;
    imageInfos[1].sampler = tex2It->second.sampler;
    
    VkWriteDescriptorSet descriptorWrites[2];
    
    // Write first texture
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].pNext = nullptr;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfos[0];
    descriptorWrites[0].pBufferInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;
    
    // Write second texture
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].pNext = nullptr;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfos[1];
    descriptorWrites[1].pBufferInfo = nullptr;
    descriptorWrites[1].pTexelBufferView = nullptr;
    
    vkUpdateDescriptorSets(device, 2, descriptorWrites, 0, nullptr);
    
    m_dualTextureDescriptorSets[descriptorId] = descriptorSet;
}

void VulkanRenderer::createDescriptorSetForTextures(uint64_t descriptorId, const std::vector<uint64_t>& textureIds) {
    if (textureIds.size() == 1) {
        // Single texture - already created by loadTexture
        // Just copy the descriptor set reference
        auto it = m_singleTextureDescriptorSets.find(textureIds[0]);
        if (it != m_singleTextureDescriptorSets.end()) {
            m_singleTextureDescriptorSets[descriptorId] = it->second;
        }
    } else if (textureIds.size() == 2) {
        createDualTextureDescriptorSet(descriptorId, textureIds[0], textureIds[1]);
    }
}

void VulkanRenderer::setShaderParameters(int pipelineId, int paramCount, const float* params) {
    m_pipelineShaderParamCount[pipelineId] = paramCount;
    for (int i = 0; i < paramCount && i < 7; ++i) {
        m_pipelineShaderParams[pipelineId][i] = params[i];
    }
    // Zero out any unused parameters
    for (int i = paramCount; i < 7; ++i) {
        m_pipelineShaderParams[pipelineId][i] = 0.0f;
    }

    // Mark this pipeline as using extended push constants
    auto infoIt = m_pipelineInfo.find(pipelineId);
    if (infoIt != m_pipelineInfo.end()) {
        infoIt->second.usesExtendedPushConstants = true;
    }
}

void VulkanRenderer::setCameraTransform(float offsetX, float offsetY, float zoom) {
    m_cameraOffsetX = offsetX;
    m_cameraOffsetY = offsetY;
    m_cameraZoom = zoom;
}

VkDescriptorSet VulkanRenderer::getOrCreateDescriptorSet(uint64_t descriptorId, uint64_t textureId, uint64_t normalMapId, bool usesDualTexture) {
    if (usesDualTexture) {
        // Check if dual-texture descriptor set already exists
        auto it = m_dualTextureDescriptorSets.find(descriptorId);
        if (it != m_dualTextureDescriptorSets.end()) {
            return it->second;
        }
        
        // Create new dual-texture descriptor set
        if (normalMapId != 0) {
            createDualTextureDescriptorSet(descriptorId, textureId, normalMapId);
            return m_dualTextureDescriptorSets[descriptorId];
        }
    } else {
        // Check if single-texture descriptor set already exists
        auto it = m_singleTextureDescriptorSets.find(descriptorId);
        if (it != m_singleTextureDescriptorSets.end()) {
            return it->second;
        }
        
        // For single texture, descriptor ID should equal texture ID
        // Check if the texture has a descriptor set
        auto texIt = m_singleTextureDescriptorSets.find(textureId);
        if (texIt != m_singleTextureDescriptorSets.end()) {
            // Reuse existing descriptor set
            m_singleTextureDescriptorSets[descriptorId] = texIt->second;
            return texIt->second;
        }
    }
    
    return VK_NULL_HANDLE;
}
