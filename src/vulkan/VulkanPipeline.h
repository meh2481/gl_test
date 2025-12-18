#pragma once

#include <vulkan/vulkan.h>
#include "../resources/resource.h"
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include <set>
#include <array>
#include <cstdint>

// Forward declarations
class VulkanDescriptor;
class MemoryAllocator;
class ConsoleBuffer;

// Maximum number of water ripples that can be passed to shader
static const int MAX_SHADER_RIPPLES = 4;

// Water ripple data for shader
struct ShaderRippleData {
    float x;         // X position of ripple
    float time;      // Time since ripple started
    float amplitude; // Ripple amplitude
};

// Pipeline metadata - stores which resources each pipeline uses
struct PipelineInfo {
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptorSetLayout;
    bool usesDualTexture;  // true = 2 textures, false = 1 texture
    bool usesExtendedPushConstants;  // true = uses extended push constants with shader parameters
    bool usesAnimationPushConstants;  // true = uses animation push constants (33 floats)
    bool isParticlePipeline;  // true = particle pipeline (uses vertex colors)
    bool isWaterPipeline;     // true = water pipeline (uses ripple push constants)
    std::set<uint64_t> descriptorIds;  // Which descriptor sets this pipeline uses
};

// Helper class for managing Vulkan pipelines
class VulkanPipeline {
public:
    VulkanPipeline(MemoryAllocator* allocator);
    ~VulkanPipeline();

    // Initialization - must be called before any other operations
    void init(VkDevice device, VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples, VkExtent2D swapchainExtent, ConsoleBuffer* consoleBuffer);
    void cleanup();

    // Set descriptor manager reference
    void setDescriptorManager(VulkanDescriptor* descriptorManager) { m_descriptorManager = descriptorManager; }

    // Basic pipeline layout
    void createBasePipelineLayout();
    VkPipelineLayout getBasePipelineLayout() const { return m_pipelineLayout; }

    // Pipeline creation
    void createPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void createTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createTexturedPipelineAdditive(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createParticlePipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode = 0);
    void createAnimTexturedPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 1);
    void createWaterPipeline(uint64_t id, const ResourceData& vertShader, const ResourceData& fragShader, uint32_t numTextures = 2);

    // Pipeline access
    VkPipeline getPipeline(uint64_t id) const;
    VkPipeline getDebugLinePipeline() const { return m_debugLinePipeline; }
    VkPipeline getDebugTrianglePipeline() const { return m_debugTrianglePipeline; }
    bool hasPipeline(uint64_t id) const;
    bool isDebugPipeline(uint64_t id) const;

    // Pipeline info access
    const PipelineInfo* getPipelineInfo(uint64_t id) const;
    PipelineInfo* getPipelineInfoMutable(uint64_t id);

    // Associate descriptor with pipeline
    void associateDescriptorWithPipeline(uint64_t pipelineId, uint64_t descriptorId);

    // Shader parameters per pipeline
    void setShaderParameters(int pipelineId, int paramCount, const float* params);
    const std::array<float, 7>& getShaderParams(int pipelineId) const;
    int getShaderParamCount(int pipelineId) const;

    // Water ripple data per pipeline
    void setWaterRipples(int pipelineId, int rippleCount, const ShaderRippleData* ripples);
    void getWaterRipples(int pipelineId, int& outRippleCount, ShaderRippleData* outRipples) const;

    // Parallax depth per pipeline
    void setPipelineParallaxDepth(int pipelineId, float depth);
    float getPipelineParallaxDepth(int pipelineId) const;

    // Set current pipeline
    void setCurrentPipeline(uint64_t id);
    VkPipeline getCurrentPipeline() const { return m_currentPipeline; }

    // Pipelines to draw
    void setPipelinesToDraw(const Vector<uint64_t>& pipelineIds) { m_pipelinesToDraw = pipelineIds; }
    const Vector<uint64_t>& getPipelinesToDraw() const { return m_pipelinesToDraw; }

    // Destroy specific pipeline
    void destroyPipeline(uint64_t id);

    // Set shaders (for legacy API)
    void setShaders(const ResourceData& vertShader, const ResourceData& fragShader);

private:
    VkShaderModule createShaderModule(const Vector<char>& code);

    VkDevice m_device;
    VkRenderPass m_renderPass;
    VkSampleCountFlagBits m_msaaSamples;
    VkExtent2D m_swapchainExtent;
    VulkanDescriptor* m_descriptorManager;
    bool m_initialized;

    // Base pipeline layout (for simple/debug pipelines)
    VkPipelineLayout m_pipelineLayout;

    // Pipelines
    HashTable<uint64_t, VkPipeline> m_pipelines;
    HashTable<uint64_t, bool> m_debugPipelines;
    VkPipeline m_debugLinePipeline;
    VkPipeline m_debugTrianglePipeline;
    VkPipeline m_currentPipeline;
    Vector<uint64_t> m_pipelinesToDraw;

    // Pipeline info
    HashTable<uint64_t, PipelineInfo*> m_pipelineInfo;

    // Per-pipeline shader parameters
    HashTable<int, std::array<float, 7>> m_pipelineShaderParams;
    HashTable<int, int> m_pipelineShaderParamCount;
    HashTable<int, float> m_pipelineParallaxDepth;

    // Per-pipeline water ripple data
    HashTable<int, std::array<ShaderRippleData, MAX_SHADER_RIPPLES>> m_pipelineWaterRipples;
    HashTable<int, int> m_pipelineWaterRippleCount;

    // Shader data storage
    Vector<char> m_vertShaderData;
    Vector<char> m_fragShaderData;

    // Memory allocator
    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;
};
