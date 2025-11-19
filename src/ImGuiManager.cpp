#ifdef DEBUG

#include "ImGuiManager.h"
#include "ConsoleBuffer.h"
#include <cassert>

// Check Vulkan result callback for ImGui
static void check_vk_result(VkResult err) {
    assert(err == VK_SUCCESS);
}

ImGuiManager::ImGuiManager() : initialized_(false), imguiPool_(VK_NULL_HANDLE) {
}

ImGuiManager::~ImGuiManager() {
    if (initialized_) {
        cleanup();
    }
}

void ImGuiManager::initialize(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                              VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
                              VkRenderPass renderPass, uint32_t imageCount) {
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

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool_;
    init_info.RenderPass = renderPass;
    init_info.Subpass = 0;
    init_info.MinImageCount = imageCount;
    init_info.ImageCount = imageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = check_vk_result;

    ImGui_ImplVulkan_Init(&init_info);

    // Upload fonts (required for ImGui to work)
    // Create temporary command pool and buffer
    VkCommandPool command_pool;
    VkCommandPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create_info.queueFamilyIndex = queueFamily;
    result = vkCreateCommandPool(device, &pool_create_info, nullptr, &command_pool);
    assert(result == VK_SUCCESS);

    VkCommandBuffer command_buffer;
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
    assert(result == VK_SUCCESS);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    assert(result == VK_SUCCESS);

    ImGui_ImplVulkan_CreateFontsTexture();

    result = vkEndCommandBuffer(command_buffer);
    assert(result == VK_SUCCESS);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    result = vkQueueSubmit(graphicsQueue, 1, &submit_info, VK_NULL_HANDLE);
    assert(result == VK_SUCCESS);

    result = vkQueueWaitIdle(graphicsQueue);
    assert(result == VK_SUCCESS);

    ImGui_ImplVulkan_DestroyFontsTexture();
    vkDestroyCommandPool(device, command_pool, nullptr);

    initialized_ = true;
}

void ImGuiManager::cleanup() {
    if (!initialized_) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    initialized_ = false;
}

void ImGuiManager::newFrame() {
    if (!initialized_) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
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

    ImGui_ImplSDL2_ProcessEvent(event);
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
