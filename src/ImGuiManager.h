#pragma once

#ifdef DEBUG

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

// Forward declaration
class VulkanRenderer;

class ImGuiManager {
public:
    ImGuiManager();
    ~ImGuiManager();

    // Initialize ImGui with Vulkan and SDL3
    void initialize(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                    VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
                    VkRenderPass renderPass, uint32_t imageCount);

    // Cleanup ImGui
    void cleanup();

    // Start a new frame
    void newFrame(int width, int height);

    // Render ImGui
    void render(VkCommandBuffer commandBuffer);

    // Process SDL event
    void processEvent(SDL_Event* event);

    // Show console window
    void showConsoleWindow();

private:
    bool initialized_;
    VkDevice device_;
    VkDescriptorPool imguiPool_;
};

#endif // DEBUG
