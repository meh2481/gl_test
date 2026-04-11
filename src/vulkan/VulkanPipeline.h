#pragma once

#include <vulkan/vulkan.h>
#include "../resources/resource.h"
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "../core/HashSet.h"

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
    HashSet<Uint64> descriptorIds;  // Which descriptor sets this pipeline uses

    explicit PipelineInfo(MemoryAllocator& allocator)
        : layout(VK_NULL_HANDLE)
        , descriptorSetLayout(VK_NULL_HANDLE)
        , usesDualTexture(false)
        , usesExtendedPushConstants(false)
        , usesAnimationPushConstants(false)
        , isParticlePipeline(false)
        , isWaterPipeline(false)
        , descriptorIds(allocator, "PipelineInfo::descriptorIds")
    {
    }
};

// Helper class for managing Vulkan pipelines
class VulkanPipeline {
public:
    VulkanPipeline(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator);
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
    void createPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, bool isDebugPipeline = false);
    void createTexturedPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 1);
    void createTexturedPipelineAdditive(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 1);
    void createParticlePipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, int blendMode = 0);
    void createAnimTexturedPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 1);
    void createWaterPipeline(Uint64 id, const ResourceData& vertShader, const ResourceData& fragShader, Uint32 numTextures = 2);
    void createFadePipeline(const ResourceData& vertShader, const ResourceData& fragShader);
    void createVectorPipeline(const ResourceData& vertShader, const ResourceData& fragShader);

    // Pipeline access
    VkPipeline getPipeline(Uint64 id) const;
    VkPipeline getDebugLinePipeline() const { return m_debugLinePipeline; }
    VkPipeline getDebugTrianglePipeline() const { return m_debugTrianglePipeline; }
    VkPipeline getFadePipeline() const { return m_fadePipeline; }
    VkPipeline getVectorPipeline() const { return m_vectorPipeline; }
    VkPipelineLayout getVectorPipelineLayout() const { return m_vectorPipelineLayout; }
    bool hasPipeline(Uint64 id) const;
    bool isDebugPipeline(Uint64 id) const;

    // Pipeline info access
    const PipelineInfo* getPipelineInfo(Uint64 id) const;
    PipelineInfo* getPipelineInfoMutable(Uint64 id);

    // Associate descriptor with pipeline
    void associateDescriptorWithPipeline(Uint64 pipelineId, Uint64 descriptorId);

    // Shader parameters per pipeline
    void setShaderParameters(int pipelineId, int paramCount, const float* params);
    const Vector<float>* getShaderParams(int pipelineId) const;
    int getShaderParamCount(int pipelineId) const;

    // Water ripple data per pipeline
    void setWaterRipples(int pipelineId, int rippleCount, const ShaderRippleData* ripples);
    void getWaterRipples(int pipelineId, int& outRippleCount, ShaderRippleData* outRipples) const;

    // Parallax depth per pipeline
    void setPipelineParallaxDepth(int pipelineId, float depth);
    float getPipelineParallaxDepth(int pipelineId) const;

    // Set current pipeline
    void setCurrentPipeline(Uint64 id);
    VkPipeline getCurrentPipeline() const { return m_currentPipeline; }

    // Pipelines to draw
    void setPipelinesToDraw(const Vector<Uint64>& pipelineIds) { m_pipelinesToDraw = pipelineIds; }
    const Vector<Uint64>& getPipelinesToDraw() const { return m_pipelinesToDraw; }

    // Destroy specific pipeline
    void destroyPipeline(Uint64 id);

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
    HashTable<Uint64, VkPipeline> m_pipelines;
    HashTable<Uint64, bool> m_debugPipelines;
    VkPipeline m_debugLinePipeline;
    VkPipeline m_debugTrianglePipeline;
    VkPipeline m_fadePipeline;
    VkPipeline m_vectorPipeline;
    VkPipelineLayout m_vectorPipelineLayout;
    VkPipeline m_currentPipeline;
    Vector<Uint64> m_pipelinesToDraw;

    // Pipeline info
    HashTable<Uint64, PipelineInfo*> m_pipelineInfo;

    // Per-pipeline shader parameters
    HashTable<int, Vector<float>*> m_pipelineShaderParams;
    HashTable<int, int> m_pipelineShaderParamCount;
    HashTable<int, float> m_pipelineParallaxDepth;

    // Per-pipeline water ripple data
    HashTable<int, Vector<ShaderRippleData>*> m_pipelineWaterRipples;
    HashTable<int, int> m_pipelineWaterRippleCount;

    // Shader data storage
    Vector<char> m_vertShaderData;
    Vector<char> m_fragShaderData;

    // Memory allocator
    MemoryAllocator* m_allocator;
    ConsoleBuffer* m_consoleBuffer;
};
