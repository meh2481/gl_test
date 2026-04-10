#include "VulkanRenderer.h"
#include "../core/ResourceTypes.h"
#include "../scene/SceneLayer.h"
#include "../debug/ConsoleBuffer.h"
#include "../debug/ThreadProfiler.h"
#include <cassert>
#include <cstdint>
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

static const char* boolToString(bool value) {
    return value ? "true" : "false";
}

static const char* vkPresentModeToString(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
        case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
        default: return "VK_PRESENT_MODE_UNKNOWN";
    }
}

// Macro to check Vulkan result and log errors
#define VK_CHECK(result, msg) \
    do { \
        VkResult res = (result); \
        if (res != VK_SUCCESS) { \
            m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "Vulkan error in %s: %s", msg, vkResultToString(res)); \
            assert(res == VK_SUCCESS); \
        } \
    } while(0)

// Reserved texture ID for reflection render target
static const Uint64 REFLECTION_TEXTURE_ID = 0xFFFFFFFF00000001ULL;
static const Uint64 REFLECTION_TEXTURE_ID_INVALID = 0xFFFFFFFFFFFFFFFFULL;

inline Uint32 clamp(Uint32 value, Uint32 min, Uint32 max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

VulkanRenderer::VulkanRenderer(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator, ConsoleBuffer* consoleBuffer) :
    m_textureManager(smallAllocator, consoleBuffer),
    m_descriptorManager(smallAllocator),
    m_pipelineManager(smallAllocator, largeAllocator),
    m_lightManager(smallAllocator),
    m_waterPolygonManager(smallAllocator, consoleBuffer),
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
    m_fadeOverlayR(0.0f),
    m_fadeOverlayG(0.0f),
    m_fadeOverlayB(0.0f),
    m_fadeOverlayAlpha(0.0f),
    m_currentFrame(0),
    m_graphicsQueueFamilyIndex(0),
    m_swapchainFramebuffers(nullptr),
    m_msaaSamples(VK_SAMPLE_COUNT_1_BIT),
    m_msaaColorImage(VK_NULL_HANDLE),
    m_msaaColorImageMemory(VK_NULL_HANDLE),
    m_msaaColorImageView(VK_NULL_HANDLE),
    m_selectedGpuIndex(-1),
    m_preferredGpuIndex(-1),
    m_preferredPresentMode(VK_PRESENT_MODE_FIFO_KHR),
    m_activePresentMode(VK_PRESENT_MODE_FIFO_KHR),
    m_swapchainNeedsRecreation(false),
    m_reflectionRenderPass(VK_NULL_HANDLE),
    m_reflectionFramebuffer(VK_NULL_HANDLE),
    m_reflectionTextureId(REFLECTION_TEXTURE_ID_INVALID),
    m_reflectionMsaaImage(VK_NULL_HANDLE),
    m_reflectionMsaaImageMemory(VK_NULL_HANDLE),
    m_reflectionMsaaImageView(VK_NULL_HANDLE),
    m_reflectionEnabled(false),
    m_reflectionSurfaceY(0.0f),
    m_frameCount(0),
    m_deviceLost(false),
    m_diagnosticCheckpointsEnabled(false),
    m_vkCmdSetCheckpointNV(nullptr),
    m_vkGetQueueCheckpointDataNV(nullptr),
    m_spriteBatches(*smallAllocator, "VulkanRenderer::m_spriteBatches"),
    m_particleBatches(*smallAllocator, "VulkanRenderer::m_particleBatches"),
    m_allBatches(*smallAllocator, "VulkanRenderer::m_allBatches"),
    m_allocator(smallAllocator),
    m_consoleBuffer(consoleBuffer)
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
    for (int i = 0; i < 2; ++i) {
        m_spriteBuffers[i] = {VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
        m_particleBuffers[i] = {VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0};
    }
}

VulkanRenderer::~VulkanRenderer() {
}

void VulkanRenderer::initialize(SDL_Window* window, int preferredGpuIndex, VkPresentModeKHR preferredPresentMode) {
    m_preferredPresentMode = preferredPresentMode;
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
    m_bufferManager.init(m_device, m_physicalDevice, m_consoleBuffer);
    m_textureManager.init(m_device, m_physicalDevice, m_commandPool, m_graphicsQueue);
    m_descriptorManager.init(m_device, m_consoleBuffer);
    m_descriptorManager.setTextureManager(&m_textureManager);
    m_lightManager.init(m_device, m_physicalDevice);
    m_waterPolygonManager.init(m_device, m_physicalDevice);

    // Create descriptor layouts and pools
    m_descriptorManager.createSingleTextureDescriptorSetLayout();
    m_descriptorManager.createSingleTexturePipelineLayout();
    m_descriptorManager.createSingleTextureDescriptorPool();
    m_descriptorManager.createDualTextureDescriptorSetLayout();
    m_descriptorManager.createLightDescriptorSetLayout();
    m_descriptorManager.createWaterPolygonDescriptorSetLayout();
    m_descriptorManager.createWaterDescriptorSetLayout();  // NEW: Water descriptor set with 3 bindings
    m_descriptorManager.createDualTexturePipelineLayout();
    m_descriptorManager.createWaterPipelineLayout();  // Uses water descriptor set layout
    m_descriptorManager.createDualTextureDescriptorPool();
    m_descriptorManager.createLightDescriptorPool();
    m_descriptorManager.createWaterPolygonDescriptorPool();
    m_descriptorManager.createWaterDescriptorPool();  // NEW: Pool for water descriptor set
    m_descriptorManager.createAnimSingleTexturePipelineLayout();
    m_descriptorManager.createAnimDualTexturePipelineLayout();

    // Create light uniform buffer and descriptor set
    m_lightManager.createLightUniformBuffer();
    m_descriptorManager.createLightDescriptorSet(m_lightManager.getUniformBuffer(), m_lightManager.getBufferSize());

    // Create water polygon uniform buffer and descriptor set
    m_waterPolygonManager.createUniformBuffer();
    m_descriptorManager.createWaterPolygonDescriptorSet(m_waterPolygonManager.getUniformBuffer(), m_waterPolygonManager.getBufferSize());

    // NOTE: Water descriptor set (with textures + uniform buffer) will be created when water is set up
    // This happens in setupWaterVisuals() after textures are loaded

    // Initialize pipeline manager
    m_pipelineManager.init(m_device, m_renderPass, m_msaaSamples, m_swapchainExtent, m_consoleBuffer);
    m_pipelineManager.setDescriptorManager(&m_descriptorManager);
    m_pipelineManager.createBasePipelineLayout();

    createFramebuffers();
    createVertexBuffer();

    // Create dynamic buffers using buffer manager
    m_bufferManager.createDynamicVertexBuffer(m_debugLineBuffer, 65536);
    m_bufferManager.createDynamicVertexBuffer(m_debugTriangleBuffer, 65536);
    m_bufferManager.createDynamicVertexBuffer(m_fadeOverlayBuffer, 256); // Small buffer for fade overlay
    for (int i = 0; i < 2; ++i) {
        m_bufferManager.createIndexedBuffer(m_spriteBuffers[i], 4096, 2048);
        m_bufferManager.createIndexedBuffer(m_particleBuffers[i], 8192, 4096);
    }

    createCommandBuffers();
    createSyncObjects();
}

void VulkanRenderer::setShaders(const ResourceData& vertShader, const ResourceData& fragShader) {
    vkDeviceWaitIdle(m_device);
    m_pipelineManager.setShaders(vertShader, fragShader);
}

void VulkanRenderer::createPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline) {
    m_pipelineManager.createPipeline(id, vertShader, fragShader, isDebugPipeline);
}

void VulkanRenderer::createFadePipeline(const ResourceData& vertShader, const ResourceData& fragShader) {
    m_pipelineManager.createFadePipeline(vertShader, fragShader);
}

void VulkanRenderer::setCurrentPipeline(Uint64 id) {
    m_pipelineManager.setCurrentPipeline(id);
}

void VulkanRenderer::associateDescriptorWithPipeline(Uint64 pipelineId, Uint64 descriptorId) {
    m_pipelineManager.associateDescriptorWithPipeline(pipelineId, descriptorId);
}

void VulkanRenderer::setPipelinesToDraw(const Vector<Uint64>& pipelineIds) {
    m_pipelineManager.setPipelinesToDraw(pipelineIds);
}

void VulkanRenderer::logDeviceLostDiagnostics() {
    m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
        "=== VK_ERROR_DEVICE_LOST on frame %llu ===", (unsigned long long)m_frameCount);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
        "  reflectionEnabled=%s reflectionTextureId=%llu",
        m_reflectionEnabled ? "true" : "false",
        (unsigned long long)m_reflectionTextureId);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
        "  waterDescriptorSet=%s lightDescriptorSet=%s",
        m_descriptorManager.getWaterDescriptorSet() != VK_NULL_HANDLE ? "valid" : "NULL",
        m_descriptorManager.getLightDescriptorSet() != VK_NULL_HANDLE ? "valid" : "NULL");
    m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
        "  allBatches.size()=%zu pipelinesToDraw.size()=%zu",
        m_allBatches.size(),
        m_pipelineManager.getPipelinesToDraw().size());
    m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
        "  currentFrame=%llu commandBuffer=%p imageAvailableSemaphore=%p renderFinishedSemaphore=%p inFlightFence=%p",
        (unsigned long long)m_currentFrame,
        (m_commandBuffers != nullptr) ? (void*)(uintptr_t)(m_commandBuffers[m_currentFrame]) : nullptr,
        (void*)(uintptr_t)(m_imageAvailableSemaphores[m_currentFrame]),
        (void*)(uintptr_t)(m_renderFinishedSemaphores[m_currentFrame]),
        (void*)(uintptr_t)(m_inFlightFences[m_currentFrame]));

    if (m_diagnosticCheckpointsEnabled && m_vkGetQueueCheckpointDataNV != nullptr) {
        Uint32 count = 0;
        m_vkGetQueueCheckpointDataNV(m_graphicsQueue, &count, nullptr);
        if (count > 0) {
            VkCheckpointDataNV* data = new VkCheckpointDataNV[count];
            for (Uint32 i = 0; i < count; ++i) {
                data[i].sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
                data[i].pNext = nullptr;
            }
            m_vkGetQueueCheckpointDataNV(m_graphicsQueue, &count, data);
            m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
                "  Last GPU checkpoint(s) before fault (%u reported):", count);
            for (Uint32 i = 0; i < count; ++i) {
                const char* label = (data[i].pCheckpointMarker != nullptr)
                    ? static_cast<const char*>(data[i].pCheckpointMarker) : "(null)";
                m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
                    "    [%u] stage=0x%x marker=\"%s\"",
                    i, (unsigned)data[i].stage, label);
            }
            delete[] data;
        } else {
            m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
                "  NV checkpoints enabled but count=0 (no commands reached the GPU)");
        }
    } else {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
            "  NV diagnostic checkpoints not available on this device/driver; "
            "run with VK_LAYER_KHRONOS_validation for more detail");
    }

    m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
        "=== end device-lost report ===");
}

void VulkanRenderer::render(float time) {
    ThreadProfiler& profiler = ThreadProfiler::instance();

    // If the device was already declared lost, stop submitting.
    // logDeviceLostDiagnostics() already fired on the first lost frame.
    if (m_deviceLost) {
        return;
    }

    ++m_frameCount;

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] begin currentFrame=%llu device=%p queue=%p swapchain=%p reflection=%s batches=%zu",
        (unsigned long long)m_frameCount,
        (unsigned long long)m_currentFrame,
        (void*)(uintptr_t)(m_device),
        (void*)(uintptr_t)(m_graphicsQueue),
        (void*)(uintptr_t)(m_swapchain),
        boolToString(m_reflectionEnabled),
        m_allBatches.size());

    profiler.updateThreadState(THREAD_STATE_WAITING);
    VkResult waitResult = vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    profiler.updateThreadState(THREAD_STATE_BUSY);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkWaitForFences(frameFence=%p) -> %s",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_inFlightFences[m_currentFrame]),
        vkResultToString(waitResult));
    if (waitResult == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        logDeviceLostDiagnostics();
        return;
    }
    if (waitResult != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
            "vkWaitForFences failed on frame %llu: %s",
            (unsigned long long)m_frameCount,
            vkResultToString(waitResult));
        assert(false);
    }

    // Update light uniform buffer if dirty
    if (m_lightManager.isDirty()) {
        m_lightManager.updateLightUniformBuffer();
    }

    Uint32 imageIndex;
    profiler.updateThreadState(THREAD_STATE_WAITING);
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    profiler.updateThreadState(THREAD_STATE_BUSY);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkAcquireNextImageKHR(waitSem=%p) -> %s imageIndex=%u",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_imageAvailableSemaphores[m_currentFrame]),
        vkResultToString(result),
        imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_swapchainNeedsRecreation = true;
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] swapchain marked for recreation (acquire out-of-date)",
            (unsigned long long)m_frameCount);
        return;
    } else if (result == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        logDeviceLostDiagnostics();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        // VK_SUBOPTIMAL_KHR is a benign performance hint on Android (preTransform mismatch);
        // the compositor handles the rotation, so ignore it and do not recreate.
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkAcquireNextImageKHR failed: %s", vkResultToString(result));
        assert(false);
    }

    VkResult resetFenceResult = vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkResetFences(frameFence=%p) -> %s",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_inFlightFences[m_currentFrame]),
        vkResultToString(resetFenceResult));
    if (resetFenceResult == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        logDeviceLostDiagnostics();
        return;
    }
    if (resetFenceResult != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
            "vkResetFences failed on frame %llu: %s",
            (unsigned long long)m_frameCount,
            vkResultToString(resetFenceResult));
        assert(false);
    }

    VkResult resetCmdResult = vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkResetCommandBuffer(cmd=%p) -> %s",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_commandBuffers[m_currentFrame]),
        vkResultToString(resetCmdResult));
    if (resetCmdResult == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        logDeviceLostDiagnostics();
        return;
    }
    if (resetCmdResult != VK_SUCCESS) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR,
            "vkResetCommandBuffer failed on frame %llu: %s",
            (unsigned long long)m_frameCount,
            vkResultToString(resetCmdResult));
        assert(false);
    }

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] recordCommandBuffer begin cmd=%p imageIndex=%u",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_commandBuffers[m_currentFrame]),
        imageIndex);
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, time);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] recordCommandBuffer end cmd=%p",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_commandBuffers[m_currentFrame]));

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

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkQueueSubmit queue=%p cmd=%p waitSem=%p signalSem=%p fence=%p waitStageMask=0x%x",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_graphicsQueue),
        (void*)(uintptr_t)(m_commandBuffers[m_currentFrame]),
        (void*)(uintptr_t)(waitSemaphores[0]),
        (void*)(uintptr_t)(signalSemaphores[0]),
        (void*)(uintptr_t)(m_inFlightFences[m_currentFrame]),
        (unsigned)waitStages[0]);

    {
        VkResult submitResult = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] vkQueueSubmit -> %s",
            (unsigned long long)m_frameCount,
            vkResultToString(submitResult));
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            m_deviceLost = true;
            logDeviceLostDiagnostics();
        } else if (submitResult != VK_SUCCESS) {
            m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "vkQueueSubmit failed on frame %llu: %s",
                                 (unsigned long long)m_frameCount, vkResultToString(submitResult));
        }
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkQueuePresentKHR queue=%p swapchain=%p imageIndex=%u waitSem=%p",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(m_graphicsQueue),
        (void*)(uintptr_t)(m_swapchain),
        imageIndex,
        (void*)(uintptr_t)(signalSemaphores[0]));

    profiler.updateThreadState(THREAD_STATE_WAITING);
    VkResult presentResult = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
    profiler.updateThreadState(THREAD_STATE_BUSY);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] vkQueuePresentKHR -> %s",
        (unsigned long long)m_frameCount,
        vkResultToString(presentResult));
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        m_swapchainNeedsRecreation = true;
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] swapchain marked for recreation (present out-of-date)",
            (unsigned long long)m_frameCount);
    } else if (presentResult == VK_ERROR_DEVICE_LOST) {
        m_deviceLost = true;
        logDeviceLostDiagnostics();
    } else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
        // VK_SUBOPTIMAL_KHR from present is harmless (preTransform mismatch hint); ignore it.
        m_consoleBuffer->log(SDL_LOG_PRIORITY_WARN, "vkQueuePresentKHR returned: %s", vkResultToString(presentResult));
    }

    m_currentFrame = (m_currentFrame + 1) % 2;
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] end nextCurrentFrame=%llu",
        (unsigned long long)m_frameCount,
        (unsigned long long)m_currentFrame);
}

void VulkanRenderer::recreateSwapchain(SDL_Window* window) {
    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Recreating Vulkan swapchain...");
    assert(m_device != VK_NULL_HANDLE);
    vkDeviceWaitIdle(m_device);

    // Destroy framebuffers
    if (m_swapchainFramebuffers != nullptr) {
        for (Uint64 i = 0; i < m_swapchainImageCount; i++) {
            if (m_swapchainFramebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device, m_swapchainFramebuffers[i], nullptr);
            }
        }
        delete[] m_swapchainFramebuffers;
        m_swapchainFramebuffers = nullptr;
    }

    // Destroy MSAA color resources
    if (m_msaaColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_msaaColorImageView, nullptr);
        m_msaaColorImageView = VK_NULL_HANDLE;
    }
    if (m_msaaColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_msaaColorImage, nullptr);
        m_msaaColorImage = VK_NULL_HANDLE;
    }
    if (m_msaaColorImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_msaaColorImageMemory, nullptr);
        m_msaaColorImageMemory = VK_NULL_HANDLE;
    }

    // Destroy swapchain image views
    if (m_swapchainImageViews != nullptr) {
        for (Uint64 i = 0; i < m_swapchainImageCount; i++) {
            if (m_swapchainImageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(m_device, m_swapchainImageViews[i], nullptr);
            }
        }
        delete[] m_swapchainImageViews;
        m_swapchainImageViews = nullptr;
    }

    // Destroy old swapchain
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    if (m_swapchainImages != nullptr) {
        delete[] m_swapchainImages;
        m_swapchainImages = nullptr;
    }
    m_swapchainImageCount = 0;

    // Recreate swapchain and all dependent resources
    createSwapchain(window);
    createImageViews();
    createMsaaColorResources();
    createFramebuffers();

    // Update the pipeline manager's stored extent so any new pipelines use the correct size
    m_pipelineManager.init(m_device, m_renderPass, m_msaaSamples, m_swapchainExtent, m_consoleBuffer);

    m_swapchainNeedsRecreation = false;
    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Swapchain recreated: %ux%u",
                         m_swapchainExtent.width, m_swapchainExtent.height);
}

bool VulkanRenderer::getTextureDimensions(Uint64 textureId, Uint32* width, Uint32* height) const {
    return m_textureManager.getTextureDimensions(textureId, width, height);
}

#ifdef DEBUG
bool VulkanRenderer::getTextureForImGui(Uint64 textureId, VkImageView* imageView, VkSampler* sampler) const {
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
    // Wait for all GPU work (including any in-flight MangoHud/layer frames) to complete
    // before destroying any Vulkan objects. Without this, Vulkan layers such as MangoHud
    // can still be executing on the GPU when vkDestroyDevice is called, causing random
    // segfaults on exit.
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    // Clean up reflection resources first
    destroyReflectionResources();

    for (Uint64 i = 0; i < 2; i++) {
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
    for (int i = 0; i < 2; ++i) {
        m_bufferManager.destroyIndexedBuffer(m_spriteBuffers[i]);
        m_bufferManager.destroyIndexedBuffer(m_particleBuffers[i]);
    }

    if (m_swapchainFramebuffers != nullptr) {
        for (Uint64 i = 0; i < m_swapchainImageCount; i++) {
            if (m_swapchainFramebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device, m_swapchainFramebuffers[i], nullptr);
            }
        }
        delete[] m_swapchainFramebuffers;
    }

    // Cleanup managers
    m_pipelineManager.cleanup();
    m_lightManager.cleanup();
    m_waterPolygonManager.cleanup();
    m_descriptorManager.cleanup();
    m_textureManager.cleanup();
    m_bufferManager.cleanup();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    }

    if (m_swapchainImageViews != nullptr) {
        for (Uint64 i = 0; i < m_swapchainImageCount; i++) {
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
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
        assert(false);
    }
    createInfo.enabledExtensionCount = count;
    createInfo.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance), "vkCreateInstance");
}

void VulkanRenderer::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_ERROR, "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        assert(false);
    }
}

bool VulkanRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    Uint32 extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    VkExtensionProperties* availableExtensions = new VkExtensionProperties[extensionCount];
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions);
    const char* requiredExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    bool found = false;
    for (Uint32 i = 0; i < extensionCount; i++) {
        if (SDL_strcmp(availableExtensions[i].extensionName, requiredExtensions[0]) == 0) {
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

    Uint32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    Uint32 presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (formatCount == 0 || presentModeCount == 0)
        return false;

    return true;
}

VkDeviceSize VulkanRenderer::getDeviceLocalMemory(VkPhysicalDevice device) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);

    VkDeviceSize maxDeviceLocalMemory = 0;
    for (Uint32 i = 0; i < memProps.memoryHeapCount; ++i) {
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

    Uint32 deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    assert(deviceCount > 0 && "No Vulkan devices found!");

    VkPhysicalDevice* devices = new VkPhysicalDevice[deviceCount];
    assert(devices != nullptr);

    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices);

    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Available Vulkan devices:");
    for (Uint32 i = 0; i < deviceCount; ++i) {
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
        m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "  [%d] %s (%s) - %llu MB - Score: %d",
                             i, props.deviceName, deviceTypeStr,
                             (unsigned long long)(maxDeviceLocalMemory / (1024 * 1024)), score);
    }

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int bestScore = -1;
    int selectedIndex = -1;

    if (preferredGpuIndex >= 0 && preferredGpuIndex < static_cast<int>(deviceCount)) {
        if (isDeviceSuitable(devices[preferredGpuIndex])) {
            bestDevice = devices[preferredGpuIndex];
            bestScore = rateDevice(devices[preferredGpuIndex]);
            selectedIndex = preferredGpuIndex;
            m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Using user-specified GPU at index %d", preferredGpuIndex);
        } else {
            m_consoleBuffer->log(SDL_LOG_PRIORITY_WARN, "Warning: User-specified GPU at index %d is not suitable, falling back to auto-selection", preferredGpuIndex);
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        for (Uint32 i = 0; i < deviceCount; ++i) {
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
    m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Selected Vulkan device: %s (index %d)", props.deviceName, m_selectedGpuIndex);

    m_physicalDevice = bestDevice;
}

void VulkanRenderer::createLogicalDevice() {
    Uint32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    assert(queueFamilyCount > 0);

    VkQueueFamilyProperties* queueFamilies = new VkQueueFamilyProperties[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies);

    int graphicsFamily = -1;
    int presentFamily = -1;
    for (Uint32 i = 0; i < queueFamilyCount; i++) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "QueueFamily[%u]: flags=0x%x queueCount=%u",
            i,
            (unsigned)queueFamilies[i].queueFlags,
            queueFamilies[i].queueCount);
        if (graphicsFamily < 0 && (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "QueueFamily[%u]: presentSupport=%s",
            i,
            boolToString(presentSupport == VK_TRUE));
        if (presentFamily < 0 && presentSupport) {
            presentFamily = i;
        }
    }

    assert(graphicsFamily >= 0);
    assert(presentFamily >= 0);

    VkDeviceQueueCreateInfo queueCreateInfos[2];
    Uint32 uniqueQueueFamilies[2];
    int numUnique = 0;
    if (graphicsFamily >= 0) uniqueQueueFamilies[numUnique++] = static_cast<Uint32>(graphicsFamily);
    if (presentFamily >= 0 && presentFamily != graphicsFamily) uniqueQueueFamilies[numUnique++] = static_cast<Uint32>(presentFamily);
    float queuePriority = 1.0f;
    for (int i = 0; i < numUnique; i++) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = uniqueQueueFamilies[i];
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos[i] = queueCreateInfo;
    }
    VkPhysicalDeviceFeatures availableFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &availableFeatures);

    VkPhysicalDeviceFeatures deviceFeatures{};
    if (availableFeatures.textureCompressionBC) {
        deviceFeatures.textureCompressionBC = VK_TRUE;
        m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "textureCompressionBC: supported and enabled");
    } else {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_WARN, "textureCompressionBC: NOT supported by this device - BC1/BC3 textures will fail");
    }
    if (availableFeatures.textureCompressionASTC_LDR) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "textureCompressionASTC_LDR: supported");
    }
    if (availableFeatures.textureCompressionETC2) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "textureCompressionETC2: supported");
    }
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "Selected queue families: graphics=%d present=%d",
        graphicsFamily,
        presentFamily);
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<Uint32>(numUnique);
    createInfo.pQueueCreateInfos = queueCreateInfos;
    createInfo.pEnabledFeatures = &deviceFeatures;
    // Check whether VK_NV_device_diagnostic_checkpoints is available.
    // This extension lets us insert GPU-side breadcrumbs so we can see exactly
    // which draw call was in flight when the device was lost.
    bool checkpointsSupported = false;
    {
        Uint32 extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
        VkExtensionProperties* exts = new VkExtensionProperties[extCount];
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, exts);
        for (Uint32 i = 0; i < extCount; ++i) {
            if (SDL_strcmp(exts[i].extensionName, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) == 0) {
                checkpointsSupported = true;
            }
        }
        delete[] exts;
    }

    const char* deviceExtensionNames[2];
    Uint32 deviceExtensionCount = 0;
    deviceExtensionNames[deviceExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (checkpointsSupported) {
        deviceExtensionNames[deviceExtensionCount++] = VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME;
        m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "VK_NV_device_diagnostic_checkpoints enabled for GPU fault diagnosis");
    } else {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "VK_NV_device_diagnostic_checkpoints not available on this device");
    }

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "Creating logical device with %u extension(s): %s%s%s",
        deviceExtensionCount,
        deviceExtensionNames[0],
        (deviceExtensionCount > 1) ? ", " : "",
        (deviceExtensionCount > 1) ? deviceExtensionNames[1] : "");
    createInfo.enabledExtensionCount = deviceExtensionCount;
    createInfo.ppEnabledExtensionNames = deviceExtensionNames;

    VkResult deviceResult = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "vkCreateDevice -> %s device=%p",
        vkResultToString(deviceResult),
        (void*)(uintptr_t)(m_device));
    assert(deviceResult == VK_SUCCESS);

    // Load NV checkpoint function pointers if the extension was enabled.
    if (checkpointsSupported) {
        m_vkCmdSetCheckpointNV = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(
            vkGetDeviceProcAddr(m_device, "vkCmdSetCheckpointNV"));
        m_vkGetQueueCheckpointDataNV = reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>(
            vkGetDeviceProcAddr(m_device, "vkGetQueueCheckpointDataNV"));
        m_diagnosticCheckpointsEnabled = (m_vkCmdSetCheckpointNV != nullptr && m_vkGetQueueCheckpointDataNV != nullptr);
        if (!m_diagnosticCheckpointsEnabled) {
            m_consoleBuffer->log(SDL_LOG_PRIORITY_WARN, "Failed to load vkCmdSetCheckpointNV / vkGetQueueCheckpointDataNV proc addresses");
        }
    }

    vkGetDeviceQueue(m_device, static_cast<Uint32>(graphicsFamily), 0, &m_graphicsQueue);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "vkGetDeviceQueue(graphicsFamily=%u, index=0) -> queue=%p",
        static_cast<Uint32>(graphicsFamily),
        (void*)(uintptr_t)(m_graphicsQueue));
    m_graphicsQueueFamilyIndex = graphicsFamily;
    delete[] queueFamilies;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* availableFormats, Uint32 formatCount) {
    for (Uint32 i = 0; i < formatCount; i++) {
        if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM && availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::chooseSwapPresentMode(const VkPresentModeKHR* availablePresentModes, Uint32 presentModeCount) {
    // Use the mode from the config file if the GPU supports it
    if (m_preferredPresentMode != VK_PRESENT_MODE_FIFO_KHR) {
        for (Uint32 i = 0; i < presentModeCount; i++) {
            if (availablePresentModes[i] == m_preferredPresentMode) {
                m_activePresentMode = availablePresentModes[i];
                return m_activePresentMode;
            }
        }
        // Requested mode not available – fall through to FIFO
    }
    // VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available (Vulkan spec 29.6)
    m_activePresentMode = VK_PRESENT_MODE_FIFO_KHR;
    return m_activePresentMode;
}

VkPresentModeKHR VulkanRenderer::getActivePresentMode() const {
    return m_activePresentMode;
}

VkExtent2D VulkanRenderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        VkExtent2D actualExtent = {
            static_cast<Uint32>(width),
            static_cast<Uint32>(height)
        };
        actualExtent.width = clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

void VulkanRenderer::createSwapchain(SDL_Window* window) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
    Uint32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    VkSurfaceFormatKHR* formats = new VkSurfaceFormatKHR[formatCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats);
    Uint32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    VkPresentModeKHR* presentModes = new VkPresentModeKHR[presentModeCount];
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats, formatCount);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes, presentModeCount);
    VkExtent2D extent = chooseSwapExtent(capabilities, window);

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "Surface capabilities: minImageCount=%u maxImageCount=%u currentExtent=%ux%u minExtent=%ux%u maxExtent=%ux%u currentTransform=0x%x supportedTransforms=0x%x",
        capabilities.minImageCount,
        capabilities.maxImageCount,
        capabilities.currentExtent.width,
        capabilities.currentExtent.height,
        capabilities.minImageExtent.width,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.width,
        capabilities.maxImageExtent.height,
        (unsigned)capabilities.currentTransform,
        (unsigned)capabilities.supportedTransforms);
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "Swapchain choices: format=%d colorSpace=%d presentMode=%s(%d) extent=%ux%u",
        (int)surfaceFormat.format,
        (int)surfaceFormat.colorSpace,
        vkPresentModeToString(presentMode),
        (int)presentMode,
        extent.width,
        extent.height);

    Uint32 imageCount = capabilities.minImageCount + 1;
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
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    // Using IDENTITY instead of capabilities.currentTransform means we do not pre-rotate
    // content in shaders; the Android compositor handles physical screen rotation.
    // The driver may return VK_SUBOPTIMAL_KHR as a performance hint (encouraging
    // pre-rotation), but that is benign and intentionally ignored in render().
    // Select a supported composite alpha mode; prefer opaque for desktop,
    // fall back to inherit which Android always supports.
    if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else {
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    {
        VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "vkCreateSwapchainKHR(imageCount=%u, usage=0x%x, preTransform=0x%x, compositeAlpha=0x%x) -> %s swapchain=%p",
            imageCount,
            (unsigned)createInfo.imageUsage,
            (unsigned)createInfo.preTransform,
            (unsigned)createInfo.compositeAlpha,
            vkResultToString(result),
            (void*)(uintptr_t)(m_swapchain));
        assert(result == VK_SUCCESS);
    }
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImageCount = imageCount;
    m_swapchainImages = new VkImage[imageCount];
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages);
    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "Swapchain created: imageCount=%u format=%d extent=%ux%u",
        imageCount,
        (int)m_swapchainImageFormat,
        m_swapchainExtent.width,
        m_swapchainExtent.height);

    for (Uint32 i = 0; i < imageCount; ++i) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "  swapchainImage[%u]=%p",
            i,
            (void*)(uintptr_t)(m_swapchainImages[i]));
    }

    delete[] formats;
    delete[] presentModes;
}

void VulkanRenderer::createImageViews() {
    m_swapchainImageViews = new VkImageView[m_swapchainImageCount];
    for (Uint64 i = 0; i < m_swapchainImageCount; i++) {
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
        {
            VkResult result = vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]);
            assert(result == VK_SUCCESS);
        }
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = m_msaaSamples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // When MSAA is enabled the MSAA image is resolved into the resolve attachment;
    // the MSAA data does not need to be written to memory (DONT_CARE is correct and
    // avoids expensive tile-memory flushes on tile-based GPUs such as Adreno).
    colorAttachment.storeOp = (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) ?
        VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
    Uint32 attachmentCount = (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) ? 1 : 2;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    {
        VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
        assert(result == VK_SUCCESS);
    }
}

void VulkanRenderer::createFramebuffers() {
    m_swapchainFramebuffers = new VkFramebuffer[m_swapchainImageCount];
    for (Uint64 i = 0; i < m_swapchainImageCount; i++) {
        VkImageView attachments[2];
        Uint32 attachmentCount;

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
        {
            VkResult result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]);
            assert(result == VK_SUCCESS);
        }
    }
}

Uint32 VulkanRenderer::findMemoryType(Uint32 typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (Uint32 i = 0; i < memProperties.memoryTypeCount; i++) {
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
    {
        VkResult result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_vertexBuffer);
        assert(result == VK_SUCCESS);
    }
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    {
        VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexBufferMemory);
        assert(result == VK_SUCCESS);
    }
    vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexBufferMemory, 0);
    void* data;
    vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferInfo.size, 0, &data);
    SDL_memcpy(data, vertices, (Uint64)bufferInfo.size);
    vkUnmapMemory(m_device, m_vertexBufferMemory);
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    {
        VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
        assert(result == VK_SUCCESS);
    }
}

void VulkanRenderer::createCommandBuffers() {
    m_commandBuffers = new VkCommandBuffer[m_swapchainImageCount];
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (Uint32)m_swapchainImageCount;
    {
        VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers);
        assert(result == VK_SUCCESS);
    }
}

void VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (Uint64 i = 0; i < 2; i++) {
        {
            VkResult result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
            assert(result == VK_SUCCESS);
        }
        {
            VkResult result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
            assert(result == VK_SUCCESS);
        }
        {
            VkResult result = vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]);
            assert(result == VK_SUCCESS);
        }
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

    {
        VkResult result = vkCreateImage(m_device, &imageInfo, nullptr, &m_msaaColorImage);
        assert(result == VK_SUCCESS);
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, m_msaaColorImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Prefer lazily-allocated memory for transient attachments: on tile-based mobile
    // GPUs (Adreno, Mali) this keeps MSAA data entirely in on-chip tile memory and
    // never touches system RAM.  More importantly, Adreno drivers may only list
    // LAZILY_ALLOCATED types in memoryTypeBits for TRANSIENT images, so searching
    // for plain DEVICE_LOCAL would fail the assert in findMemoryType and crash.
    {
        bool foundLazy = false;
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
        for (Uint32 i = 0; i < memProps.memoryTypeCount; i++) {
            constexpr VkMemoryPropertyFlags kLazy =
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
            if ((memRequirements.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & kLazy) == kLazy) {
                allocInfo.memoryTypeIndex = i;
                foundLazy = true;
                m_consoleBuffer->log(SDL_LOG_PRIORITY_INFO,
                    "MSAA color image: using lazily-allocated (on-chip) memory (type %u)", i);
                break;
            }
        }
        if (!foundLazy) {
            allocInfo.memoryTypeIndex = findMemoryType(
                memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }
    }

    {
        VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_msaaColorImageMemory);
        assert(result == VK_SUCCESS);
    }
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

    {
        VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &m_msaaColorImageView);
        assert(result == VK_SUCCESS);
    }
}

// Texture and pipeline delegation methods

void VulkanRenderer::loadTexture(Uint64 textureId, const ResourceData& imageData) {
    m_textureManager.loadTexture(textureId, imageData);
    // Create descriptor set for the texture
    VulkanTexture::TextureData texData;
    if (m_textureManager.getTexture(textureId, &texData)) {
        m_descriptorManager.createSingleTextureDescriptorSet(textureId, texData.imageView, texData.sampler);
    }
}

void VulkanRenderer::loadAtlasTexture(Uint64 atlasId, const ResourceData& atlasData) {
    m_textureManager.loadAtlasTexture(atlasId, atlasData);
    // Create descriptor set for the atlas
    VulkanTexture::TextureData texData;
    if (m_textureManager.getTexture(atlasId, &texData)) {
        m_descriptorManager.createSingleTextureDescriptorSet(atlasId, texData.imageView, texData.sampler);
    }
}

void VulkanRenderer::createTexturedPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures) {
    m_pipelineManager.createTexturedPipeline(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createTexturedPipelineAdditive(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures) {
    m_pipelineManager.createTexturedPipelineAdditive(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createAnimTexturedPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures) {
    m_pipelineManager.createAnimTexturedPipeline(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createWaterPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures) {
    m_pipelineManager.createWaterPipeline(id, vertShader, fragShader, numTextures);
}

void VulkanRenderer::createParticlePipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode) {
    m_pipelineManager.createParticlePipeline(id, vertShader, fragShader, blendMode);
}

void VulkanRenderer::destroyPipeline(Uint64 id) {
    m_pipelineManager.destroyPipeline(id);
}

void VulkanRenderer::createDescriptorSetForTextures(Uint64 descriptorId, const Vector<Uint64>& textureIds) {
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

void VulkanRenderer::updateWaterPolygonVertices(const float* vertices, int vertexCount) {
    m_waterPolygonManager.updateUniformBuffer(vertices, vertexCount);
}

void VulkanRenderer::createWaterDescriptorSet(Uint64 primaryTextureId, Uint64 reflectionTextureId) {
    m_consoleBuffer->log(SDL_LOG_PRIORITY_TRACE, "Creating water descriptor set with primary texture %llu and reflection texture %llu",
                        (unsigned long long)primaryTextureId, (unsigned long long)reflectionTextureId);
    m_descriptorManager.createWaterDescriptorSet(primaryTextureId, reflectionTextureId,
                                                 m_waterPolygonManager.getUniformBuffer(),
                                                 m_waterPolygonManager.getBufferSize());
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

void VulkanRenderer::setFadeOverlay(float r, float g, float b, float alpha) {
    m_fadeOverlayR = r;
    m_fadeOverlayG = g;
    m_fadeOverlayB = b;
    m_fadeOverlayAlpha = alpha;
}

// Buffer update methods

void VulkanRenderer::setDebugDrawData(const Vector<float>& vertexData) {
    vkWaitForFences(m_device, 2, m_inFlightFences, VK_TRUE, UINT64_MAX);
    m_bufferManager.updateDynamicVertexBuffer(m_debugLineBuffer, vertexData, 6);
}

void VulkanRenderer::setDebugLineDrawData(const Vector<float>& vertexData) {
    vkWaitForFences(m_device, 2, m_inFlightFences, VK_TRUE, UINT64_MAX);
    m_bufferManager.updateDynamicVertexBuffer(m_debugLineBuffer, vertexData, 6);
}

void VulkanRenderer::setDebugTriangleDrawData(const Vector<float>& vertexData) {
    vkWaitForFences(m_device, 2, m_inFlightFences, VK_TRUE, UINT64_MAX);
    m_bufferManager.updateDynamicVertexBuffer(m_debugTriangleBuffer, vertexData, 6);
}

void VulkanRenderer::setSpriteDrawData(const Vector<float>& vertexData, const Vector<Uint16>& indices) {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    m_bufferManager.updateIndexedBuffer(m_spriteBuffers[m_currentFrame], vertexData, indices, 6);
}

void VulkanRenderer::setParticleDrawData(const Vector<float>& vertexData, const Vector<Uint16>& indices, Uint64 textureId) {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    m_bufferManager.updateIndexedBuffer(m_particleBuffers[m_currentFrame], vertexData, indices, 18);
    m_particleTextureId = textureId;
}

void VulkanRenderer::setSpriteBatches(const Vector<SpriteBatch>& batches) {
    // Wait only for the frame buffer we'll write this frame.
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    m_spriteBatches.clear();

    Vector<float> allVertexData(*m_allocator, "VulkanRenderer::generateSpriteBatches::allVertexData");
    Vector<Uint16> allIndices(*m_allocator, "VulkanRenderer::generateSpriteBatches::allIndices");
    Uint32 baseVertex = 0;
    Uint32 orderIndex = 0;

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
        drawData.firstIndex = static_cast<Uint32>(allIndices.size());
        drawData.indexCount = static_cast<Uint32>(batch.indices.size());
        drawData.instanceCount = 1;
        drawData.firstInstance = 0;
        drawData.orderIndex = orderIndex++;
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

        for (Uint16 idx : batch.indices) {
            allIndices.push_back(idx + baseVertex);
        }

        baseVertex += static_cast<Uint32>(batch.vertices.size());
        m_spriteBatches.push_back(drawData);
    }

    m_bufferManager.updateIndexedBuffer(m_spriteBuffers[m_currentFrame], allVertexData, allIndices, 10);
    rebuildAllBatches();
}

void VulkanRenderer::setParticleBatches(const Vector<ParticleBatch>& batches) {
    // Wait only for the frame buffer we'll write this frame.
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    m_particleBatches.clear();

    Vector<float> allVertexData(*m_allocator, "VulkanRenderer::generateParticleBatches::allVertexData");
    Vector<Uint16> allIndices(*m_allocator, "VulkanRenderer::generateParticleBatches::allIndices");
    Uint32 baseInstance = 0;
    // Start order index after sprite batches to preserve creation order
    Uint32 orderIndex = static_cast<Uint32>(m_spriteBatches.size());

    for (const auto& batch : batches) {
        if (batch.instances.empty()) {
            continue;
        }

        BatchDrawData drawData;
        drawData.textureId = batch.textureId;
        drawData.normalMapId = 0;
        drawData.descriptorId = batch.textureId;  // Use texture ID as descriptor ID
        drawData.pipelineId = batch.pipelineId;
        drawData.parallaxDepth = batch.parallaxDepth;
        drawData.firstIndex = 0;
        drawData.indexCount = 6;
        drawData.instanceCount = static_cast<Uint32>(batch.instances.size());
        drawData.firstInstance = baseInstance;
        drawData.orderIndex = orderIndex++;
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

        for (const auto& instance : batch.instances) {
            allVertexData.push_back(instance.x);
            allVertexData.push_back(instance.y);
            allVertexData.push_back(instance.halfSize);
            allVertexData.push_back(instance.rotZ);
            allVertexData.push_back(instance.lifeRatio);
            allVertexData.push_back(0.0f);
            allVertexData.push_back(instance.startR);
            allVertexData.push_back(instance.startG);
            allVertexData.push_back(instance.startB);
            allVertexData.push_back(instance.startA);
            allVertexData.push_back(instance.endR);
            allVertexData.push_back(instance.endG);
            allVertexData.push_back(instance.endB);
            allVertexData.push_back(instance.endA);
            allVertexData.push_back(instance.uvMinX);
            allVertexData.push_back(instance.uvMinY);
            allVertexData.push_back(instance.uvMaxX);
            allVertexData.push_back(instance.uvMaxY);
        }

        baseInstance += drawData.instanceCount;
        m_particleBatches.push_back(drawData);
    }

    allIndices.push_back(0);
    allIndices.push_back(1);
    allIndices.push_back(2);
    allIndices.push_back(2);
    allIndices.push_back(3);
    allIndices.push_back(0);

    m_bufferManager.updateIndexedBuffer(m_particleBuffers[m_currentFrame], allVertexData, allIndices, 18);
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
    // Then by order index to preserve creation order (lower = created earlier = drawn first)
    m_allBatches.sort([](const BatchDrawData& a, const BatchDrawData& b) {
        if (a.parallaxDepth != b.parallaxDepth) {
            return a.parallaxDepth > b.parallaxDepth;
        }
        return a.orderIndex < b.orderIndex;
    });
}

// Light management delegation

int VulkanRenderer::addLight(float x, float y, float z, float r, float g, float b, float intensity) {
    return m_lightManager.addLight(x, y, z, r, g, b, intensity);
}

void VulkanRenderer::updateLightPosition(int lightId, float x, float y, float z) {
    m_lightManager.updateLightPosition(lightId, x, y, z);
}

void VulkanRenderer::updateLightColor(int lightId, float r, float g, float b) {
    m_lightManager.updateLightColor(lightId, r, g, b);
}

void VulkanRenderer::updateLightIntensity(int lightId, float intensity) {
    m_lightManager.updateLightIntensity(lightId, intensity);
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

// Helper: insert a GPU-side diagnostic checkpoint if the NV extension is loaded.
// The marker is a plain string literal whose address is stored as a breadcrumb on
// the GPU.  After a device-lost the last marker that completed is queryable via
// vkGetQueueCheckpointDataNV so we know exactly which phase triggered the TDR.
#define INSERT_CHECKPOINT(cmdBuf, label) \
    do { if (m_diagnosticCheckpointsEnabled) m_vkCmdSetCheckpointNV((cmdBuf), (label)); } while(0)

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, Uint32 imageIndex, float time) {
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] recordCommandBuffer(cmd=%p, imageIndex=%u, time=%.3f) pipelines=%zu batches=%zu",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(commandBuffer),
        imageIndex,
        time,
        m_pipelineManager.getPipelinesToDraw().size(),
        m_allBatches.size());

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    {
        VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] vkBeginCommandBuffer(cmd=%p) -> %s",
            (unsigned long long)m_frameCount,
            (void*)(uintptr_t)(commandBuffer),
            vkResultToString(result));
        assert(result == VK_SUCCESS);
    }

    INSERT_CHECKPOINT(commandBuffer, "cmd_begin");

    // Render reflection pass first (if enabled)
    if (m_reflectionEnabled) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] reflection pass enabled (surfaceY=%.3f)",
            (unsigned long long)m_frameCount,
            m_reflectionSurfaceY);
        INSERT_CHECKPOINT(commandBuffer, "reflection_pass_start");
        recordReflectionPass(commandBuffer, time);
        INSERT_CHECKPOINT(commandBuffer, "reflection_pass_end");
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

    INSERT_CHECKPOINT(commandBuffer, "main_pass_start");

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
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] pipeline pass start: pipelinesToDraw=%zu",
        (unsigned long long)m_frameCount,
        pipelinesToDraw.size());

    // Phase 1: Draw background shaders (non-textured pipelines like nebula)
    INSERT_CHECKPOINT(commandBuffer, "phase1_background");
    for (Uint64 pipelineId : pipelinesToDraw) {
        if (m_pipelineManager.isDebugPipeline(pipelineId)) {
            continue; // Skip debug, draw last
        }

        VkPipeline pipeline = m_pipelineManager.getPipeline(pipelineId);
        const PipelineInfo* info = m_pipelineManager.getPipelineInfo(pipelineId);

        // Non-textured pipeline (e.g., background shaders)
        if (pipeline != VK_NULL_HANDLE && info == nullptr) {
            m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                "[Vulkan][Frame %llu] draw background pipelineId=%llu pipeline=%p",
                (unsigned long long)m_frameCount,
                (unsigned long long)pipelineId,
                (void*)(uintptr_t)(pipeline));
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
    INSERT_CHECKPOINT(commandBuffer, "phase2_batches");
    if (!m_allBatches.empty()) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] drawing %zu total batches",
            (unsigned long long)m_frameCount,
            m_allBatches.size());
        int currentPipelineId = -1;
        bool currentIsParticle = false;
        bool spriteBound = false;
        bool particleBound = false;

        for (const auto& batch : m_allBatches) {
            VkPipeline pipeline = m_pipelineManager.getPipeline(batch.pipelineId);
            const PipelineInfo* info = m_pipelineManager.getPipelineInfo(batch.pipelineId);

            if (pipeline == VK_NULL_HANDLE || info == nullptr) {
                m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                    "[Vulkan][Frame %llu] skip batch pipelineId=%d (pipeline=%p info=%p)",
                    (unsigned long long)m_frameCount,
                    batch.pipelineId,
                    (void*)(uintptr_t)(pipeline),
                    static_cast<const void*>(info));
                continue;
            }

            m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                "[Vulkan][Frame %llu] batch pipelineId=%d isParticle=%s descriptorId=%llu tex=%llu normal=%llu idxCount=%u firstIdx=%u instCount=%u firstInst=%u depth=%.3f",
                (unsigned long long)m_frameCount,
                batch.pipelineId,
                boolToString(batch.isParticle),
                (unsigned long long)batch.descriptorId,
                (unsigned long long)batch.textureId,
                (unsigned long long)batch.normalMapId,
                batch.indexCount,
                batch.firstIndex,
                batch.instanceCount,
                batch.firstInstance,
                batch.parallaxDepth);

            // Switch buffers if switching between sprite and particle batches
            if (batch.isParticle != currentIsParticle) {
                currentIsParticle = batch.isParticle;
                currentPipelineId = -1;  // Force pipeline rebind

                if (batch.isParticle) {
                    VkBuffer vertexBuffers[] = {m_particleBuffers[m_currentFrame].vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, m_particleBuffers[m_currentFrame].indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    particleBound = true;
                    spriteBound = false;
                } else {
                    VkBuffer vertexBuffers[] = {m_spriteBuffers[m_currentFrame].vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, m_spriteBuffers[m_currentFrame].indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    spriteBound = true;
                    particleBound = false;
                }
            }

            // Ensure buffers are bound on first batch
            if (!batch.isParticle && !spriteBound) {
                VkBuffer vertexBuffers[] = {m_spriteBuffers[m_currentFrame].vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, m_spriteBuffers[m_currentFrame].indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                spriteBound = true;
            } else if (batch.isParticle && !particleBound) {
                VkBuffer vertexBuffers[] = {m_particleBuffers[m_currentFrame].vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, m_particleBuffers[m_currentFrame].indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                particleBound = true;
            }

            // Switch pipeline if needed
            if (batch.pipelineId != currentPipelineId) {
                currentPipelineId = batch.pipelineId;
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                    "[Vulkan][Frame %llu] vkCmdBindPipeline pipelineId=%d pipeline=%p",
                    (unsigned long long)m_frameCount,
                    batch.pipelineId,
                    (void*)(uintptr_t)(pipeline));
            }

            // Set push constants for this batch
            if (info->isWaterPipeline) {
                // Water pipeline with ripple push constants (25 floats = 100 bytes)
                const Vector<float>* params = m_pipelineManager.getShaderParams(batch.pipelineId);
                int rippleCount = 0;
                ShaderRippleData ripples[MAX_SHADER_RIPPLES];
                m_pipelineManager.getWaterRipples(batch.pipelineId, rippleCount, ripples);

                // Build water push constants WITHOUT polygon vertices (now in uniform buffer)
                // Layout: system(6) + params(7) + ripples(12) = 25 floats (100 bytes)
                float waterPushConstants[25] = {
                    static_cast<float>(m_swapchainExtent.width),          // 0
                    static_cast<float>(m_swapchainExtent.height),         // 1
                    time,                                                  // 2
                    m_cameraOffsetX,                                       // 3
                    m_cameraOffsetY,                                       // 4
                    m_cameraZoom,                                          // 5
                    // Standard water params (indices 6-12)
                    params ? (*params)[0] : 0.0f,                          // 6: alpha
                    params ? (*params)[1] : 0.0f,                          // 7: rippleAmplitude
                    params ? (*params)[2] : 0.0f,                          // 8: rippleSpeed
                    params ? (*params)[3] : 0.0f,                          // 9: surfaceY
                    params ? (*params)[4] : 0.0f,                          // 10: minX
                    params ? (*params)[5] : 0.0f,                          // 11: minY
                    params ? (*params)[6] : 0.0f,                          // 12: maxX
                    // Ripple data (indices 13-24, 4 ripples x 3 values)
                    rippleCount > 0 ? ripples[0].x : 0.0f,                 // 13
                    rippleCount > 0 ? ripples[0].time : -1.0f,             // 14
                    rippleCount > 0 ? ripples[0].amplitude : 0.0f,         // 15
                    rippleCount > 1 ? ripples[1].x : 0.0f,                 // 16
                    rippleCount > 1 ? ripples[1].time : -1.0f,             // 17
                    rippleCount > 1 ? ripples[1].amplitude : 0.0f,         // 18
                    rippleCount > 2 ? ripples[2].x : 0.0f,                 // 19
                    rippleCount > 2 ? ripples[2].time : -1.0f,             // 20
                    rippleCount > 2 ? ripples[2].amplitude : 0.0f,         // 21
                    rippleCount > 3 ? ripples[3].x : 0.0f,                 // 22
                    rippleCount > 3 ? ripples[3].time : -1.0f,             // 23
                    rippleCount > 3 ? ripples[3].amplitude : 0.0f,         // 24
                };
                vkCmdPushConstants(commandBuffer, info->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(waterPushConstants), waterPushConstants);
            } else if (info->usesAnimationPushConstants) {
                // Animation pipeline with push constants (30 floats = 120 bytes)
                // Layout: system(6) + params(4: param0-param3) + animation(20) = 30 floats
                // param4/5/6 removed - unused in all animation shaders, freed 12 bytes to
                // stay within the 128-byte Vulkan push constant minimum.
                const Vector<float>* params = m_pipelineManager.getShaderParams(batch.pipelineId);

                float animPushConstants[30] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    m_cameraOffsetY,
                    m_cameraZoom,
                    params ? (*params)[0] : 0.0f, params ? (*params)[1] : 0.0f, params ? (*params)[2] : 0.0f,
                    params ? (*params)[3] : 0.0f,
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
                const Vector<float>* params = m_pipelineManager.getShaderParams(batch.pipelineId);

                float extPushConstants[13] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    m_cameraOffsetY,
                    m_cameraZoom,
                    (params && params->size() > 0) ? (*params)[0] : 0.0f,
                    (params && params->size() > 1) ? (*params)[1] : 0.0f,
                    (params && params->size() > 2) ? (*params)[2] : 0.0f,
                    (params && params->size() > 3) ? (*params)[3] : 0.0f,
                    (params && params->size() > 4) ? (*params)[4] : 0.0f,
                    (params && params->size() > 5) ? (*params)[5] : 0.0f,
                    (params && params->size() > 6) ? (*params)[6] : 0.0f
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
                    const VkDescriptorSet* descSetPtr = singleTexDescSets.find(batch.textureId);
                    if (descSetPtr != nullptr) {
                        descriptorSet = *descSetPtr;
                    }
                    if (descriptorSet == VK_NULL_HANDLE) {
                        auto it = singleTexDescSets.begin();
                        if (it != singleTexDescSets.end()) {
                            descriptorSet = it.value();
                        }
                    }
                    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                        "[Vulkan][Frame %llu] particle descriptor bind textureId=%llu descriptorSet=%p",
                        (unsigned long long)m_frameCount,
                        (unsigned long long)batch.textureId,
                        (void*)(uintptr_t)(descriptorSet));
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          info->layout, 0, 1, &descriptorSet, 0, nullptr);
                    vkCmdDrawIndexed(commandBuffer, batch.indexCount, batch.instanceCount, batch.firstIndex, 0, batch.firstInstance);
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
                    if (info->isWaterPipeline) {
                        // Water pipeline uses 2 descriptor sets: water (with 3 bindings) + light
                        INSERT_CHECKPOINT(commandBuffer, "water_draw");
                        VkDescriptorSet waterSet = m_descriptorManager.getWaterDescriptorSet();
                        VkDescriptorSet lightSet = m_descriptorManager.getLightDescriptorSet();

                        // Validate descriptor sets before binding
                        assert(waterSet != VK_NULL_HANDLE && "Water descriptor set is null");
                        assert(lightSet != VK_NULL_HANDLE && "Light descriptor set is null");

                        VkDescriptorSet descriptorSets[] = {
                            waterSet,
                            lightSet
                        };
                        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                            "[Vulkan][Frame %llu] water descriptor bind waterSet=%p lightSet=%p",
                            (unsigned long long)m_frameCount,
                            (void*)(uintptr_t)(waterSet),
                            (void*)(uintptr_t)(lightSet));
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              info->layout, 0, 2, descriptorSets, 0, nullptr);
                    } else if (info->usesDualTexture) {
                        VkDescriptorSet descriptorSets[] = {descriptorSet, m_descriptorManager.getLightDescriptorSet()};
                        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                            "[Vulkan][Frame %llu] dual-texture descriptor bind materialSet=%p lightSet=%p",
                            (unsigned long long)m_frameCount,
                            (void*)(uintptr_t)(descriptorSet),
                            (void*)(uintptr_t)(m_descriptorManager.getLightDescriptorSet()));
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              info->layout, 0, 2, descriptorSets, 0, nullptr);
                    } else {
                        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                            "[Vulkan][Frame %llu] single descriptor bind set=%p",
                            (unsigned long long)m_frameCount,
                            (void*)(uintptr_t)(descriptorSet));
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              info->layout, 0, 1, &descriptorSet, 0, nullptr);
                    }
                    vkCmdDrawIndexed(commandBuffer, batch.indexCount, batch.instanceCount, batch.firstIndex, 0, batch.firstInstance);
                    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                        "[Vulkan][Frame %llu] vkCmdDrawIndexed(indexCount=%u, instanceCount=%u, firstIndex=%u, firstInstance=%u)",
                        (unsigned long long)m_frameCount,
                        batch.indexCount,
                        batch.instanceCount,
                        batch.firstIndex,
                        batch.firstInstance);
                } else {
                    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                        "[Vulkan][Frame %llu] skipped sprite draw because descriptorSet is NULL (descriptorId=%llu)",
                        (unsigned long long)m_frameCount,
                        (unsigned long long)batch.descriptorId);
                }
            }
        }
    }

    // Phase 3: Draw debug shader (always last)
    INSERT_CHECKPOINT(commandBuffer, "phase3_debug");
    for (Uint64 pipelineId : pipelinesToDraw) {
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

    // Render fade overlay if alpha > 0 (for scene transitions)
    if (m_fadeOverlayAlpha > 0.0f && m_pipelineManager.getFadePipeline() != VK_NULL_HANDLE) {
        // Create a fullscreen quad with the fade color
        // Two triangles covering the entire screen in NDC coordinates
        const int FADE_VERTEX_COUNT = 6; // 2 triangles = 6 vertices
        const int FADE_FLOATS_PER_VERTEX = 6; // x, y, r, g, b, a
        const int FADE_TOTAL_FLOATS = FADE_VERTEX_COUNT * FADE_FLOATS_PER_VERTEX;

        float fadeVertices[FADE_TOTAL_FLOATS] = {
            // Triangle 1: bottom-left, bottom-right, top-right
            -1.0f, -1.0f, m_fadeOverlayR, m_fadeOverlayG, m_fadeOverlayB, m_fadeOverlayAlpha,
             1.0f, -1.0f, m_fadeOverlayR, m_fadeOverlayG, m_fadeOverlayB, m_fadeOverlayAlpha,
             1.0f,  1.0f, m_fadeOverlayR, m_fadeOverlayG, m_fadeOverlayB, m_fadeOverlayAlpha,
            // Triangle 2: top-right, top-left, bottom-left
             1.0f,  1.0f, m_fadeOverlayR, m_fadeOverlayG, m_fadeOverlayB, m_fadeOverlayAlpha,
            -1.0f,  1.0f, m_fadeOverlayR, m_fadeOverlayG, m_fadeOverlayB, m_fadeOverlayAlpha,
            -1.0f, -1.0f, m_fadeOverlayR, m_fadeOverlayG, m_fadeOverlayB, m_fadeOverlayAlpha,
        };

        // Create a Vector wrapper for the fade vertices
        Vector<float> fadeVertexVector(*m_allocator, "VulkanRenderer::fadeOverlay");
        fadeVertexVector.reserve(FADE_TOTAL_FLOATS);
        for (int i = 0; i < FADE_TOTAL_FLOATS; ++i) {
            fadeVertexVector.push_back(fadeVertices[i]);
        }

        // Update the fade overlay buffer
        m_bufferManager.updateDynamicVertexBuffer(m_fadeOverlayBuffer, fadeVertexVector, FADE_FLOATS_PER_VERTEX);

        if (m_fadeOverlayBuffer.buffer != VK_NULL_HANDLE && m_fadeOverlayBuffer.count > 0) {
            VkBuffer buffers[] = {m_fadeOverlayBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineManager.getFadePipeline());
            vkCmdDraw(commandBuffer, m_fadeOverlayBuffer.count, 1, 0, 0);
        }
    }

#ifdef DEBUG
    if (m_imguiRenderCallback) {
        m_imguiRenderCallback(commandBuffer);
    }
#endif

    INSERT_CHECKPOINT(commandBuffer, "main_pass_end");
    vkCmdEndRenderPass(commandBuffer);
    {
        VkResult result = vkEndCommandBuffer(commandBuffer);
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] vkEndCommandBuffer(cmd=%p) -> %s",
            (unsigned long long)m_frameCount,
            (void*)(uintptr_t)(commandBuffer),
            vkResultToString(result));
        assert(result == VK_SUCCESS);
    }
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

    m_consoleBuffer->log(SDL_LOG_PRIORITY_TRACE, "Reflection enabled at surface Y=%f", surfaceY);
}

void VulkanRenderer::disableReflection() {
    if (!m_reflectionEnabled) {
        return;
    }

    destroyReflectionResources();
    m_reflectionEnabled = false;

    m_consoleBuffer->log(SDL_LOG_PRIORITY_TRACE, "Reflection disabled");
}

void VulkanRenderer::createReflectionResources() {
    // Create render target texture for reflection (always 1-sample; used as the
    // resolve attachment when MSAA is active, or the direct color attachment otherwise).
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

    // If MSAA is active we need a matching multi-sample color image for the reflection
    // render pass.  Sprites and phong pipelines are compiled against the main render pass
    // sample count (m_msaaSamples).  Using them inside a 1-sample render pass violates
    // VUID-vkCmdBindPipeline and causes VK_ERROR_DEVICE_LOST on strict drivers (NVIDIA).
    const bool useMsaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);
    if (useMsaa) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {m_swapchainExtent.width, m_swapchainExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_swapchainImageFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = m_msaaSamples;
        VkResult r = vkCreateImage(m_device, &imageInfo, nullptr, &m_reflectionMsaaImage);
        assert(r == VK_SUCCESS);

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_device, m_reflectionMsaaImage, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        r = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_reflectionMsaaImageMemory);
        assert(r == VK_SUCCESS);
        vkBindImageMemory(m_device, m_reflectionMsaaImage, m_reflectionMsaaImageMemory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_reflectionMsaaImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainImageFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        r = vkCreateImageView(m_device, &viewInfo, nullptr, &m_reflectionMsaaImageView);
        assert(r == VK_SUCCESS);
    }

    // Build the reflection render pass.  The sample count and attachment structure
    // must exactly mirror the main render pass so that the same pipelines can be used.
    //
    //  Non-MSAA: one attachment (the reflection texture, STORE)
    //  MSAA:     attachment[0] = MSAA image (DONT_CARE store, intermediate)
    //            attachment[1] = reflection texture (resolve target, STORE)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = m_msaaSamples;  // match the pipelines
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = useMsaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription resolveAttachment{};
    resolveAttachment.format = m_swapchainImageFormat;
    resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolveAttachmentRef{};
    resolveAttachmentRef.attachment = 1;
    resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pResolveAttachments = useMsaa ? &resolveAttachmentRef : nullptr;

    // Entry dependency: wait for any previous sampling/attachment use before writing
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Exit dependency: ensure color writes (and the implicit MSAA resolve) are visible
    // to the main pass fragment shader before it samples the reflection texture.
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkAttachmentDescription attachments[] = {colorAttachment, resolveAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = useMsaa ? 2u : 1u;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_reflectionRenderPass);
    assert(result == VK_SUCCESS);

    // Create framebuffer for reflection
    if (m_textureManager.getTexture(m_reflectionTextureId, &texData)) {
        VkImageView fbAttachments[2];
        Uint32 fbAttachmentCount;
        if (useMsaa) {
            fbAttachments[0] = m_reflectionMsaaImageView;  // slot 0: MSAA draw target
            fbAttachments[1] = texData.imageView;          // slot 1: resolve target
            fbAttachmentCount = 2;
        } else {
            fbAttachments[0] = texData.imageView;          // slot 0: direct color target
            fbAttachmentCount = 1;
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_reflectionRenderPass;
        framebufferInfo.attachmentCount = fbAttachmentCount;
        framebufferInfo.pAttachments = fbAttachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_reflectionFramebuffer);
        assert(result == VK_SUCCESS);
    }

    m_consoleBuffer->log(SDL_LOG_PRIORITY_DEBUG,
        "Created reflection resources: %dx%d msaa=%d",
        m_swapchainExtent.width, m_swapchainExtent.height, (int)m_msaaSamples);
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

    if (m_reflectionMsaaImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_reflectionMsaaImageView, nullptr);
        m_reflectionMsaaImageView = VK_NULL_HANDLE;
    }
    if (m_reflectionMsaaImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_reflectionMsaaImage, nullptr);
        m_reflectionMsaaImage = VK_NULL_HANDLE;
    }
    if (m_reflectionMsaaImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_reflectionMsaaImageMemory, nullptr);
        m_reflectionMsaaImageMemory = VK_NULL_HANDLE;
    }

    if (m_reflectionTextureId != REFLECTION_TEXTURE_ID_INVALID) {
        m_textureManager.destroyTexture(m_reflectionTextureId);
        m_reflectionTextureId = REFLECTION_TEXTURE_ID_INVALID;
    }
}

void VulkanRenderer::recordReflectionPass(VkCommandBuffer commandBuffer, float time) {
    if (!m_reflectionEnabled || m_reflectionRenderPass == VK_NULL_HANDLE) {
        m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
            "[Vulkan][Frame %llu] reflection pass skipped enabled=%s renderPass=%p",
            (unsigned long long)m_frameCount,
            boolToString(m_reflectionEnabled),
            (void*)(uintptr_t)(m_reflectionRenderPass));
        return;
    }

    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] reflection pass begin cmd=%p framebuffer=%p",
        (unsigned long long)m_frameCount,
        (void*)(uintptr_t)(commandBuffer),
        (void*)(uintptr_t)(m_reflectionFramebuffer));

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
    if (m_spriteBuffers[m_currentFrame].vertexBuffer != VK_NULL_HANDLE && m_spriteBuffers[m_currentFrame].indexBuffer != VK_NULL_HANDLE) {
        VkBuffer vertexBuffers[] = {m_spriteBuffers[m_currentFrame].vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_spriteBuffers[m_currentFrame].indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    }

    // Draw only batches that are above the water surface (parallaxDepth check)
    for (const auto& batch : m_allBatches) {
        if (batch.isParticle) continue;  // Skip particles for reflection

        VkPipeline pipeline = m_pipelineManager.getPipeline(batch.pipelineId);
        const PipelineInfo* info = m_pipelineManager.getPipelineInfo(batch.pipelineId);

        if (pipeline != VK_NULL_HANDLE && info != nullptr) {
            // Skip water pipelines in reflection pass
            if (info->isWaterPipeline) continue;

            m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
                "[Vulkan][Frame %llu] reflection draw pipelineId=%d descriptorId=%llu tex=%llu indexCount=%u firstIndex=%u",
                (unsigned long long)m_frameCount,
                batch.pipelineId,
                (unsigned long long)batch.descriptorId,
                (unsigned long long)batch.textureId,
                batch.indexCount,
                batch.firstIndex);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            // Set push constants with flipped Y
            if (info->usesExtendedPushConstants) {
                const Vector<float>* params = m_pipelineManager.getShaderParams(batch.pipelineId);
                float extPushConstants[13] = {
                    static_cast<float>(m_swapchainExtent.width),
                    static_cast<float>(m_swapchainExtent.height),
                    time,
                    m_cameraOffsetX,
                    flippedCameraY,
                    -m_cameraZoom,  // Negative zoom to flip Y
                    (params && params->size() > 0) ? (*params)[0] : 0.0f,
                    (params && params->size() > 1) ? (*params)[1] : 0.0f,
                    (params && params->size() > 2) ? (*params)[2] : 0.0f,
                    (params && params->size() > 3) ? (*params)[3] : 0.0f,
                    (params && params->size() > 4) ? (*params)[4] : 0.0f,
                    (params && params->size() > 5) ? (*params)[5] : 0.0f,
                    (params && params->size() > 6) ? (*params)[6] : 0.0f
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
    m_consoleBuffer->log(SDL_LOG_PRIORITY_VERBOSE,
        "[Vulkan][Frame %llu] reflection pass end",
        (unsigned long long)m_frameCount);
}
