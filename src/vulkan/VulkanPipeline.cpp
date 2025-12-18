#include "VulkanPipeline.h"
#include "VulkanDescriptor.h"
#include "../core/Vector.h"
#include <cassert>

// Helper function to convert VkResult to readable string for error logging
static const char* vkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
        default: return "VK_UNKNOWN_ERROR";
    }
}

VulkanPipeline::VulkanPipeline(MemoryAllocator* allocator) :
    m_device(VK_NULL_HANDLE),
    m_renderPass(VK_NULL_HANDLE),
    m_msaaSamples(VK_SAMPLE_COUNT_1_BIT),
    m_swapchainExtent({0, 0}),
    m_descriptorManager(nullptr),
    m_initialized(false),
    m_pipelineLayout(VK_NULL_HANDLE),
    m_pipelines(*allocator, "VulkanPipeline::m_pipelines"),
    m_debugPipelines(*allocator, "VulkanPipeline::m_debugPipelines"),
    m_debugLinePipeline(VK_NULL_HANDLE),
    m_debugTrianglePipeline(VK_NULL_HANDLE),
    m_currentPipeline(VK_NULL_HANDLE),
    m_pipelinesToDraw(*allocator, "VulkanPipeline::m_pipelinesToDraw"),
    m_pipelineInfo(*allocator, "VulkanPipeline::m_pipelineInfo"),
    m_pipelineShaderParams(*allocator, "VulkanPipeline::m_pipelineShaderParams"),
    m_pipelineShaderParamCount(*allocator, "VulkanPipeline::m_pipelineShaderParamCount"),
    m_pipelineParallaxDepth(*allocator, "VulkanPipeline::m_pipelineParallaxDepth"),
    m_pipelineWaterRipples(*allocator, "VulkanPipeline::m_pipelineWaterRipples"),
    m_pipelineWaterRippleCount(*allocator, "VulkanPipeline::m_pipelineWaterRippleCount"),
    m_vertShaderData(*allocator, "VulkanPipeline::m_vertShaderData"),
    m_fragShaderData(*allocator, "VulkanPipeline::m_fragShaderData"),
    m_allocator(allocator)
{
    assert(m_allocator != nullptr);
}

VulkanPipeline::~VulkanPipeline() {
}

void VulkanPipeline::init(VkDevice device, VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples, VkExtent2D swapchainExtent) {
    m_device = device;
    m_renderPass = renderPass;
    m_msaaSamples = msaaSamples;
    m_swapchainExtent = swapchainExtent;
    m_initialized = true;
}

void VulkanPipeline::cleanup() {
    for (auto it = m_pipelines.begin(); it != m_pipelines.end(); ++it) {
        if (it.value() != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, it.value(), nullptr);
        }
    }
    m_pipelines.clear();

    if (m_debugLinePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_debugLinePipeline, nullptr);
        m_debugLinePipeline = VK_NULL_HANDLE;
    }
    if (m_debugTrianglePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_debugTrianglePipeline, nullptr);
        m_debugTrianglePipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    // Delete all dynamically allocated PipelineInfo objects
    for (auto it = m_pipelineInfo.begin(); it != m_pipelineInfo.end(); ++it) {
        PipelineInfo* info = it.value();
        info->~PipelineInfo();
        m_allocator->free(info);
    }
    m_pipelineInfo.clear();
    m_debugPipelines.clear();
    m_pipelineShaderParams.clear();
    m_pipelineShaderParamCount.clear();
    m_pipelineParallaxDepth.clear();
    m_pipelinesToDraw.clear();
    m_vertShaderData.clear();
    m_fragShaderData.clear();
    m_currentPipeline = VK_NULL_HANDLE;
    m_initialized = false;
}

VkShaderModule VulkanPipeline::createShaderModule(const Vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateShaderModule failed: %s", vkResultToString(result));
        assert(false);
    }
    return shaderModule;
}

void VulkanPipeline::createBasePipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 7; // width, height, time, cameraX, cameraY, cameraZoom, parallaxDepth

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreatePipelineLayout failed: %s", vkResultToString(result));
        assert(false);
    }
}

void VulkanPipeline::createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline) {
    Vector<char> vertData(*m_allocator, "VulkanPipeline::createPipeline::vertData");
    Vector<char> fragData(*m_allocator, "VulkanPipeline::createPipeline::fragData");
    for (size_t i = 0; i < vertShader.size; ++i) {
        vertData.push_back(vertShader.data[i]);
    }
    for (size_t i = 0; i < fragShader.size; ++i) {
        fragData.push_back(fragShader.data[i]);
    }

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
        bindingDescription.stride = sizeof(float) * 6;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;
    } else {
        bindingDescription.stride = sizeof(float) * 4;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
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
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_msaaSamples;

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
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkResult result;
    if (isDebugPipeline) {
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_debugLinePipeline);
        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines (debug line) failed: %s", vkResultToString(result));
            assert(false);
        }

        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_debugTrianglePipeline);
        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines (debug triangle) failed: %s", vkResultToString(result));
            assert(false);
        }

        m_debugPipelines.insert(id, true);
    } else {
        VkPipeline pipeline;
        result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines failed: %s", vkResultToString(result));
            assert(false);
        }

        m_pipelines.insert(id, pipeline);
        m_debugPipelines.insert(id, false);
    }

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

void VulkanPipeline::createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_vertShaderData.clear();
    m_fragShaderData.clear();
    for (size_t i = 0; i < vertShader.size; ++i) {
        m_vertShaderData.push_back(vertShader.data[i]);
    }
    for (size_t i = 0; i < fragShader.size; ++i) {
        m_fragShaderData.push_back(fragShader.data[i]);
    }

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

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[4]{};
    uint32_t numAttributes = 3;

    if (numTextures == 2) {
        bindingDescription.stride = sizeof(float) * 10;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 4;

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = sizeof(float) * 6;

        numAttributes = 4;
    } else {
        bindingDescription.stride = sizeof(float) * 10;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 6;

        numAttributes = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = numAttributes;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_msaaSamples;

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

    VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;

    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;
    bool usesDualTexture = (numTextures == 2);

    if (usesDualTexture) {
        pipelineLayout = m_descriptorManager->getDualTexturePipelineLayout();
        descriptorSetLayout = m_descriptorManager->getDualTextureLayout();
    } else {
        pipelineLayout = m_descriptorManager->getSingleTexturePipelineLayout();
        descriptorSetLayout = m_descriptorManager->getSingleTextureLayout();
    }

    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.renderPass = m_renderPass;
    pipelineCreateInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines (textured) failed: %s", vkResultToString(result));
        assert(false);
    }

    // Destroy existing pipeline if it exists to prevent memory leaks
    if (hasPipeline(id)) {
        destroyPipeline(id);
    }

    m_pipelines.insertNew(id, pipeline);

    void* infoMem = m_allocator->allocate(sizeof(PipelineInfo), "VulkanPipeline::createTexturedPipeline::PipelineInfo");
    PipelineInfo* info = new (infoMem) PipelineInfo();
    info->layout = pipelineLayout;
    info->descriptorSetLayout = descriptorSetLayout;
    info->usesDualTexture = usesDualTexture;
    info->usesExtendedPushConstants = false;
    info->usesAnimationPushConstants = false;
    info->isParticlePipeline = false;
    info->isWaterPipeline = false;
    m_pipelineInfo.insertNew(id, info);

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

void VulkanPipeline::createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_vertShaderData.clear();
    m_fragShaderData.clear();
    for (size_t i = 0; i < vertShader.size; ++i) {
        m_vertShaderData.push_back(vertShader.data[i]);
    }
    for (size_t i = 0; i < fragShader.size; ++i) {
        m_fragShaderData.push_back(fragShader.data[i]);
    }

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

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[4]{};
    uint32_t numAttributes = 3;

    if (numTextures == 2) {
        bindingDescription.stride = sizeof(float) * 10;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 4;

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = sizeof(float) * 6;

        numAttributes = 4;
    } else {
        bindingDescription.stride = sizeof(float) * 10;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 6;

        numAttributes = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = numAttributes;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_msaaSamples;

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

    VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;

    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;
    bool usesDualTexture = (numTextures == 2);

    if (usesDualTexture) {
        pipelineLayout = m_descriptorManager->getDualTexturePipelineLayout();
        descriptorSetLayout = m_descriptorManager->getDualTextureLayout();
    } else {
        pipelineLayout = m_descriptorManager->getSingleTexturePipelineLayout();
        descriptorSetLayout = m_descriptorManager->getSingleTextureLayout();
    }

    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.renderPass = m_renderPass;
    pipelineCreateInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines (textured additive) failed: %s", vkResultToString(result));
        assert(false);
    }

    // Destroy existing pipeline if it exists to prevent memory leaks
    if (hasPipeline(id)) {
        destroyPipeline(id);
    }

    m_pipelines.insertNew(id, pipeline);

    void* infoMem = m_allocator->allocate(sizeof(PipelineInfo), "VulkanPipeline::createTexturedPipelineAdditive::PipelineInfo");
    PipelineInfo* info = new (infoMem) PipelineInfo();
    info->layout = pipelineLayout;
    info->descriptorSetLayout = descriptorSetLayout;
    info->usesDualTexture = usesDualTexture;
    info->usesExtendedPushConstants = false;
    info->usesAnimationPushConstants = false;
    info->isParticlePipeline = false;
    info->isWaterPipeline = false;
    m_pipelineInfo.insertNew(id, info);

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

void VulkanPipeline::createAnimTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures) {
    m_vertShaderData.clear();
    m_fragShaderData.clear();
    for (size_t i = 0; i < vertShader.size; ++i) {
        m_vertShaderData.push_back(vertShader.data[i]);
    }
    for (size_t i = 0; i < fragShader.size; ++i) {
        m_fragShaderData.push_back(fragShader.data[i]);
    }

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

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[4]{};
    uint32_t numAttributes = 3;

    if (numTextures == 2) {
        bindingDescription.stride = sizeof(float) * 10;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 4;

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = sizeof(float) * 6;

        numAttributes = 4;
    } else {
        bindingDescription.stride = sizeof(float) * 10;

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 6;

        numAttributes = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = numAttributes;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_msaaSamples;

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

    VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;

    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;
    bool usesDualTexture = (numTextures == 2);

    // Use animation pipeline layouts with extended push constants
    if (usesDualTexture) {
        pipelineLayout = m_descriptorManager->getAnimDualTexturePipelineLayout();
        descriptorSetLayout = m_descriptorManager->getDualTextureLayout();
    } else {
        pipelineLayout = m_descriptorManager->getAnimSingleTexturePipelineLayout();
        descriptorSetLayout = m_descriptorManager->getSingleTextureLayout();
    }

    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.renderPass = m_renderPass;
    pipelineCreateInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines (anim textured) failed: %s", vkResultToString(result));
        assert(false);
    }

    // Destroy existing pipeline if it exists to prevent memory leaks
    if (hasPipeline(id)) {
        destroyPipeline(id);
    }

    m_pipelines.insertNew(id, pipeline);

    void* infoMem = m_allocator->allocate(sizeof(PipelineInfo), "VulkanPipeline::createAnimTexturedPipeline::PipelineInfo");
    PipelineInfo* info = new (infoMem) PipelineInfo();
    info->layout = pipelineLayout;
    info->descriptorSetLayout = descriptorSetLayout;
    info->usesDualTexture = usesDualTexture;
    info->usesExtendedPushConstants = true;
    info->usesAnimationPushConstants = true;
    info->isParticlePipeline = false;
    info->isWaterPipeline = false;
    m_pipelineInfo.insertNew(id, info);

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

void VulkanPipeline::createParticlePipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode) {
    m_vertShaderData.clear();
    m_fragShaderData.clear();
    for (size_t i = 0; i < vertShader.size; ++i) {
        m_vertShaderData.push_back(vertShader.data[i]);
    }
    for (size_t i = 0; i < fragShader.size; ++i) {
        m_fragShaderData.push_back(fragShader.data[i]);
    }

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

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 12;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[4]{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2;

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[2].offset = sizeof(float) * 4;

    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[3].offset = sizeof(float) * 8;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainExtent;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_msaaSamples;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;

    // blendMode: 0 = additive, 1 = alpha, 2 = subtractive
    if (blendMode == 0) {
        // Additive blending
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (blendMode == 1) {
        // Alpha blending
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        // Subtractive blending
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.layout = m_descriptorManager->getSingleTexturePipelineLayout();
    pipelineCreateInfo.renderPass = m_renderPass;
    pipelineCreateInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCreateGraphicsPipelines (particle) failed: %s", vkResultToString(result));
        assert(false);
    }

    // Destroy existing pipeline if it exists to prevent memory leaks
    if (hasPipeline(id)) {
        destroyPipeline(id);
    }

    m_pipelines.insertNew(id, pipeline);

    void* infoMem = m_allocator->allocate(sizeof(PipelineInfo), "VulkanPipeline::createParticlePipeline::PipelineInfo");
    PipelineInfo* info = new (infoMem) PipelineInfo();
    info->layout = m_descriptorManager->getSingleTexturePipelineLayout();
    info->descriptorSetLayout = m_descriptorManager->getSingleTextureLayout();
    info->usesDualTexture = false;
    info->usesExtendedPushConstants = false;
    info->usesAnimationPushConstants = false;
    info->isParticlePipeline = true;
    info->isWaterPipeline = false;
    m_pipelineInfo.insertNew(id, info);

    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

VkPipeline VulkanPipeline::getPipeline(uint64_t id) const {
    const VkPipeline* pipelinePtr = m_pipelines.find(id);
    if (pipelinePtr != nullptr) {
        return *pipelinePtr;
    }
    return VK_NULL_HANDLE;
}

bool VulkanPipeline::hasPipeline(uint64_t id) const {
    return m_pipelines.find(id) != nullptr;
}

bool VulkanPipeline::isDebugPipeline(uint64_t id) const {
    const bool* isDebugPtr = m_debugPipelines.find(id);
    return isDebugPtr != nullptr && *isDebugPtr;
}

const PipelineInfo* VulkanPipeline::getPipelineInfo(uint64_t id) const {
    PipelineInfo* const* infoPtr = m_pipelineInfo.find(id);
    return infoPtr ? *infoPtr : nullptr;
}

PipelineInfo* VulkanPipeline::getPipelineInfoMutable(uint64_t id) {
    PipelineInfo** infoPtr = m_pipelineInfo.find(id);
    return infoPtr ? *infoPtr : nullptr;
}

void VulkanPipeline::associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId) {
    PipelineInfo** infoPtrPtr = m_pipelineInfo.find(pipelineId);
    if (infoPtrPtr != nullptr) {
        (*infoPtrPtr)->descriptorIds.insert(descriptorId);
    }
}

void VulkanPipeline::setShaderParameters(int pipelineId, int paramCount, const float* params) {
    m_pipelineShaderParamCount.insert(pipelineId, paramCount);

    std::array<float, 7> paramsArray;
    for (int i = 0; i < paramCount && i < 7; ++i) {
        paramsArray[i] = params[i];
    }
    for (int i = paramCount; i < 7; ++i) {
        paramsArray[i] = 0.0f;
    }
    m_pipelineShaderParams.insert(pipelineId, paramsArray);

    PipelineInfo** infoPtrPtr = m_pipelineInfo.find(pipelineId);
    if (infoPtrPtr != nullptr) {
        (*infoPtrPtr)->usesExtendedPushConstants = true;
    }
}

const std::array<float, 7>& VulkanPipeline::getShaderParams(int pipelineId) const {
    static const std::array<float, 7> defaultParams = {0, 0, 0, 0, 0, 0, 0};
    const std::array<float, 7>* paramsPtr = m_pipelineShaderParams.find(pipelineId);
    if (paramsPtr != nullptr) {
        return *paramsPtr;
    }
    return defaultParams;
}

int VulkanPipeline::getShaderParamCount(int pipelineId) const {
    const int* countPtr = m_pipelineShaderParamCount.find(pipelineId);
    if (countPtr != nullptr) {
        return *countPtr;
    }
    return 0;
}

void VulkanPipeline::setWaterRipples(int pipelineId, int rippleCount, const ShaderRippleData* ripples) {
    if (rippleCount > MAX_SHADER_RIPPLES) {
        rippleCount = MAX_SHADER_RIPPLES;
    }
    m_pipelineWaterRippleCount.insert(pipelineId, rippleCount);

    std::array<ShaderRippleData, MAX_SHADER_RIPPLES> ripplesArray;
    for (int i = 0; i < rippleCount; ++i) {
        ripplesArray[i] = ripples[i];
    }
    for (int i = rippleCount; i < MAX_SHADER_RIPPLES; ++i) {
        ripplesArray[i] = {0.0f, 0.0f, 0.0f};
    }
    m_pipelineWaterRipples.insert(pipelineId, ripplesArray);

    PipelineInfo** infoPtrPtr = m_pipelineInfo.find(pipelineId);
    if (infoPtrPtr != nullptr) {
        (*infoPtrPtr)->isWaterPipeline = true;
    }
}

void VulkanPipeline::getWaterRipples(int pipelineId, int& outRippleCount, ShaderRippleData* outRipples) const {
    const int* countPtr = m_pipelineWaterRippleCount.find(pipelineId);
    if (countPtr == nullptr) {
        outRippleCount = 0;
        return;
    }
    outRippleCount = *countPtr;
    const std::array<ShaderRippleData, MAX_SHADER_RIPPLES>* ripplesPtr = m_pipelineWaterRipples.find(pipelineId);
    if (ripplesPtr != nullptr) {
        for (int i = 0; i < outRippleCount; ++i) {
            outRipples[i] = (*ripplesPtr)[i];
        }
    }
}

void VulkanPipeline::setPipelineParallaxDepth(int pipelineId, float depth) {
    m_pipelineParallaxDepth.insert(pipelineId, depth);
}

float VulkanPipeline::getPipelineParallaxDepth(int pipelineId) const {
    const float* depthPtr = m_pipelineParallaxDepth.find(pipelineId);
    if (depthPtr != nullptr) {
        return *depthPtr;
    }
    return 0.0f;
}

void VulkanPipeline::setCurrentPipeline(uint64_t id) {
    const VkPipeline* pipelinePtr = m_pipelines.find(id);
    assert(pipelinePtr != nullptr);
    m_currentPipeline = *pipelinePtr;
}

void VulkanPipeline::destroyPipeline(uint64_t id) {
    VkPipeline* pipelinePtr = m_pipelines.find(id);
    if (pipelinePtr != nullptr) {
        vkDestroyPipeline(m_device, *pipelinePtr, nullptr);
        m_pipelines.remove(id);
    }
    m_debugPipelines.remove(id);
    
    // Delete the dynamically allocated PipelineInfo
    PipelineInfo** infoPtrPtr = m_pipelineInfo.find(id);
    if (infoPtrPtr != nullptr) {
        PipelineInfo* info = *infoPtrPtr;
        info->~PipelineInfo();
        m_allocator->free(info);
        m_pipelineInfo.remove(id);
    }
}

void VulkanPipeline::setShaders(const ResourceData& vertShader, const ResourceData& fragShader) {
    vkDeviceWaitIdle(m_device);
    destroyPipeline(0);
    if (m_debugLinePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_debugLinePipeline, nullptr);
        m_debugLinePipeline = VK_NULL_HANDLE;
    }
    if (m_debugTrianglePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_debugTrianglePipeline, nullptr);
        m_debugTrianglePipeline = VK_NULL_HANDLE;
    }
    createPipeline(0, vertShader, fragShader);
    setCurrentPipeline(0);
}
