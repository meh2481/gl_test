#ifdef DEBUG

#include "ImGuiManager.h"
#include "ConsoleBuffer.h"
#include <cassert>

// Check Vulkan result callback for ImGui
static void check_vk_result(VkResult err) {
    assert(err == VK_SUCCESS);
}

ImGuiManager::ImGuiManager() : initialized_(false), device_(VK_NULL_HANDLE), imguiPool_(VK_NULL_HANDLE) {
}

ImGuiManager::~ImGuiManager() {
    if (initialized_) {
        cleanup();
    }
}

void ImGuiManager::initialize(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                              VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
                              VkRenderPass renderPass, uint32_t imageCount) {
    device_ = device;
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;
    VkResult result = vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool_);
    assert(result == VK_SUCCESS);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool_;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = imageCount;
    init_info.ImageCount = imageCount;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = check_vk_result;

    ImGui_ImplVulkan_Init(&init_info);

    initialized_ = true;
}

void ImGuiManager::cleanup() {
    if (!initialized_) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    if (imguiPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, imguiPool_, nullptr);
        imguiPool_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

void ImGuiManager::newFrame(int width, int height) {
    if (!initialized_) {
        return;
    }

    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
    if (!initialized_) {
        return;
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiManager::processEvent(SDL_Event* event) {
    if (!initialized_) {
        return;
    }

    // No platform backend, so no event processing
}

void ImGuiManager::showConsoleWindow() {
    if (!initialized_) {
        return;
    }

    // Create console window
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Console Output", nullptr);

    // Get console lines
    std::vector<std::string> lines;
    ConsoleBuffer::getInstance().getLines(lines);

    // Display lines in a scrollable region
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -30), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& line : lines) {
        ImGui::TextUnformatted(line.c_str());
    }

    // Auto-scroll to bottom if we're near the bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    // Add a separator and a clear button
    ImGui::Separator();
    if (ImGui::Button("Clear")) {
        ConsoleBuffer::getInstance().clear();
    }

    ImGui::End();
}

#endif // DEBUG
