#ifdef DEBUG

#include "ImGuiManager.h"
#include "ConsoleBuffer.h"
#include "resource.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>

// Check Vulkan result callback for ImGui
static void check_vk_result(VkResult err) {
    assert(err == VK_SUCCESS);
}

ImGuiManager::ImGuiManager() : initialized_(false), device_(VK_NULL_HANDLE), imguiPool_(VK_NULL_HANDLE) {
    // Initialize particle editor state
    memset(&editorState_, 0, sizeof(editorState_));
    editorState_.isActive = false;
    editorState_.previewSystemId = -1;
    editorState_.previewPipelineId = -1;
    editorState_.selectedVertexIndex = -1;
    editorState_.isDraggingVertex = false;
    editorState_.selectedTextureCount = 0;
    editorState_.previewZoom = 1.0f;
    editorState_.previewOffsetX = 0.0f;
    editorState_.previewOffsetY = 0.0f;
    editorState_.showExportPopup = false;

    // Initialize default particle config
    editorState_.config.maxParticles = 100;
    editorState_.config.emissionRate = 10.0f;
    editorState_.config.blendMode = PARTICLE_BLEND_ADDITIVE;
    editorState_.config.emissionVertexCount = 0;
    editorState_.config.textureCount = 0;
    editorState_.config.positionVariance = 0.0f;
    editorState_.config.velocityMinX = -0.5f;
    editorState_.config.velocityMaxX = 0.5f;
    editorState_.config.velocityMinY = 0.5f;
    editorState_.config.velocityMaxY = 1.5f;
    editorState_.config.accelerationMinX = 0.0f;
    editorState_.config.accelerationMaxX = 0.0f;
    editorState_.config.accelerationMinY = -1.0f;
    editorState_.config.accelerationMaxY = -0.5f;
    editorState_.config.radialAccelerationMin = 0.0f;
    editorState_.config.radialAccelerationMax = 0.0f;
    editorState_.config.radialVelocityMin = 0.0f;
    editorState_.config.radialVelocityMax = 0.0f;
    editorState_.config.startSizeMin = 0.05f;
    editorState_.config.startSizeMax = 0.1f;
    editorState_.config.endSizeMin = 0.02f;
    editorState_.config.endSizeMax = 0.05f;
    editorState_.config.colorMinR = 1.0f;
    editorState_.config.colorMaxR = 1.0f;
    editorState_.config.colorMinG = 0.8f;
    editorState_.config.colorMaxG = 1.0f;
    editorState_.config.colorMinB = 0.0f;
    editorState_.config.colorMaxB = 0.3f;
    editorState_.config.colorMinA = 1.0f;
    editorState_.config.colorMaxA = 1.0f;
    editorState_.config.endColorMinR = 1.0f;
    editorState_.config.endColorMaxR = 1.0f;
    editorState_.config.endColorMinG = 0.0f;
    editorState_.config.endColorMaxG = 0.2f;
    editorState_.config.endColorMinB = 0.0f;
    editorState_.config.endColorMaxB = 0.0f;
    editorState_.config.endColorMinA = 0.0f;
    editorState_.config.endColorMaxA = 0.0f;
    editorState_.config.lifetimeMin = 1.0f;
    editorState_.config.lifetimeMax = 2.0f;
    editorState_.config.rotationMinZ = 0.0f;
    editorState_.config.rotationMaxZ = 6.28318f;
    editorState_.config.rotVelocityMinZ = -1.0f;
    editorState_.config.rotVelocityMaxZ = 1.0f;

    // Expand sections by default
    editorState_.colorsExpanded = true;
    editorState_.velocityExpanded = true;
    editorState_.accelerationExpanded = false;
    editorState_.sizeExpanded = true;
    editorState_.rotationExpanded = false;
    editorState_.emissionExpanded = true;
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

    // Initialize SDL3 backend for ImGui
    ImGui_ImplSDL3_InitForVulkan(window);

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
    ImGui_ImplSDL3_Shutdown();
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

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
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

    // With SDL3 backend, we can process events
    ImGui_ImplSDL3_ProcessEvent(event);
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

void ImGuiManager::setParticleEditorActive(bool active) {
    editorState_.isActive = active;
}

bool ImGuiManager::isParticleEditorActive() const {
    return editorState_.isActive;
}

void ImGuiManager::showParticleEditorWindow(ParticleSystemManager* particleManager, PakResource* pakResource,
                                             int pipelineId, float deltaTime) {
    if (!initialized_ || !editorState_.isActive) {
        return;
    }

    // Create main particle editor window
    ImGui::SetNextWindowSize(ImVec2(450, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Particle System Editor", &editorState_.isActive)) {
        ImGui::End();
        return;
    }

    // Store pipeline ID for preview
    editorState_.previewPipelineId = pipelineId;

    // Update preview system with current config
    updatePreviewSystem(particleManager, pipelineId);

    // Tabs for different sections
    if (ImGui::BeginTabBar("ParticleEditorTabs")) {
        if (ImGui::BeginTabItem("Emission")) {
            showEmissionSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Motion")) {
            showVelocitySettings();
            ImGui::Separator();
            showAccelerationSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Appearance")) {
            showSizeSettings();
            ImGui::Separator();
            showColorSettings();
            ImGui::Separator();
            showRotationSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Textures")) {
            showTextureSelector(pakResource);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Polygon")) {
            showEmissionPolygonEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Export")) {
            showLuaExport();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Preview controls at the bottom
    ImGui::Separator();
    ImGui::Text("Preview Controls:");
    ImGui::SliderFloat("Zoom", &editorState_.previewZoom, 0.1f, 10.0f, "%.2f");
    ImGui::DragFloat2("Offset", &editorState_.previewOffsetX, 0.01f, -10.0f, 10.0f, "%.2f");

    if (ImGui::Button("Reset Preview")) {
        editorState_.previewZoom = 1.0f;
        editorState_.previewOffsetX = 0.0f;
        editorState_.previewOffsetY = 0.0f;
    }

    ImGui::End();
}

void ImGuiManager::showEmissionSettings() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Basic Emission Settings");

    ImGui::SliderInt("Max Particles", &cfg.maxParticles, 1, 10000);
    ImGui::SliderFloat("Emission Rate", &cfg.emissionRate, 0.0f, 1000.0f, "%.1f particles/sec");
    ImGui::SliderFloat("Position Variance", &cfg.positionVariance, 0.0f, 2.0f, "%.3f");

    const char* blendModes[] = { "Additive", "Alpha" };
    int blendMode = (int)cfg.blendMode;
    if (ImGui::Combo("Blend Mode", &blendMode, blendModes, 2)) {
        cfg.blendMode = (ParticleBlendMode)blendMode;
    }

    ImGui::Separator();
    ImGui::Text("Lifetime");
    ImGui::DragFloatRange2("Lifetime Range", &cfg.lifetimeMin, &cfg.lifetimeMax, 0.01f, 0.01f, 30.0f, "Min: %.2fs", "Max: %.2fs");
}

void ImGuiManager::showVelocitySettings() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Linear Velocity");
    ImGui::DragFloatRange2("Velocity X", &cfg.velocityMinX, &cfg.velocityMaxX, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Velocity Y", &cfg.velocityMinY, &cfg.velocityMaxY, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");

    ImGui::Separator();
    ImGui::Text("Radial Velocity (from emission center)");
    ImGui::DragFloatRange2("Radial Velocity", &cfg.radialVelocityMin, &cfg.radialVelocityMax, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
}

void ImGuiManager::showAccelerationSettings() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Linear Acceleration");
    ImGui::DragFloatRange2("Accel X", &cfg.accelerationMinX, &cfg.accelerationMaxX, 0.01f, -20.0f, 20.0f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Accel Y", &cfg.accelerationMinY, &cfg.accelerationMaxY, 0.01f, -20.0f, 20.0f, "Min: %.2f", "Max: %.2f");

    ImGui::Separator();
    ImGui::Text("Radial Acceleration (towards/away from center)");
    ImGui::DragFloatRange2("Radial Accel", &cfg.radialAccelerationMin, &cfg.radialAccelerationMax, 0.01f, -20.0f, 20.0f, "Min: %.2f", "Max: %.2f");
}

void ImGuiManager::showSizeSettings() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Particle Size");
    ImGui::DragFloatRange2("Start Size", &cfg.startSizeMin, &cfg.startSizeMax, 0.001f, 0.001f, 5.0f, "Min: %.3f", "Max: %.3f");
    ImGui::DragFloatRange2("End Size", &cfg.endSizeMin, &cfg.endSizeMax, 0.001f, 0.001f, 5.0f, "Min: %.3f", "Max: %.3f");
}

void ImGuiManager::showColorSettings() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Start Color Range");

    // Start color min
    float startColorMin[4] = { cfg.colorMinR, cfg.colorMinG, cfg.colorMinB, cfg.colorMinA };
    if (ImGui::ColorEdit4("Start Color Min", startColorMin)) {
        cfg.colorMinR = startColorMin[0];
        cfg.colorMinG = startColorMin[1];
        cfg.colorMinB = startColorMin[2];
        cfg.colorMinA = startColorMin[3];
    }

    // Start color max
    float startColorMax[4] = { cfg.colorMaxR, cfg.colorMaxG, cfg.colorMaxB, cfg.colorMaxA };
    if (ImGui::ColorEdit4("Start Color Max", startColorMax)) {
        cfg.colorMaxR = startColorMax[0];
        cfg.colorMaxG = startColorMax[1];
        cfg.colorMaxB = startColorMax[2];
        cfg.colorMaxA = startColorMax[3];
    }

    ImGui::Separator();
    ImGui::Text("End Color Range");

    // End color min
    float endColorMin[4] = { cfg.endColorMinR, cfg.endColorMinG, cfg.endColorMinB, cfg.endColorMinA };
    if (ImGui::ColorEdit4("End Color Min", endColorMin)) {
        cfg.endColorMinR = endColorMin[0];
        cfg.endColorMinG = endColorMin[1];
        cfg.endColorMinB = endColorMin[2];
        cfg.endColorMinA = endColorMin[3];
    }

    // End color max
    float endColorMax[4] = { cfg.endColorMaxR, cfg.endColorMaxG, cfg.endColorMaxB, cfg.endColorMaxA };
    if (ImGui::ColorEdit4("End Color Max", endColorMax)) {
        cfg.endColorMaxR = endColorMax[0];
        cfg.endColorMaxG = endColorMax[1];
        cfg.endColorMaxB = endColorMax[2];
        cfg.endColorMaxA = endColorMax[3];
    }
}

void ImGuiManager::showRotationSettings() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Initial Rotation (radians)");
    ImGui::DragFloatRange2("Rotation X", &cfg.rotationMinX, &cfg.rotationMaxX, 0.01f, -6.28f, 6.28f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Rotation Y", &cfg.rotationMinY, &cfg.rotationMaxY, 0.01f, -6.28f, 6.28f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Rotation Z", &cfg.rotationMinZ, &cfg.rotationMaxZ, 0.01f, -6.28f, 6.28f, "Min: %.2f", "Max: %.2f");

    ImGui::Separator();
    ImGui::Text("Rotational Velocity (rad/sec)");
    ImGui::DragFloatRange2("Rot Vel X", &cfg.rotVelocityMinX, &cfg.rotVelocityMaxX, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Rot Vel Y", &cfg.rotVelocityMinY, &cfg.rotVelocityMaxY, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Rot Vel Z", &cfg.rotVelocityMinZ, &cfg.rotVelocityMaxZ, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");

    ImGui::Separator();
    ImGui::Text("Rotational Acceleration (rad/sec^2)");
    ImGui::DragFloatRange2("Rot Accel X", &cfg.rotAccelerationMinX, &cfg.rotAccelerationMaxX, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Rot Accel Y", &cfg.rotAccelerationMinY, &cfg.rotAccelerationMaxY, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
    ImGui::DragFloatRange2("Rot Accel Z", &cfg.rotAccelerationMinZ, &cfg.rotAccelerationMaxZ, 0.01f, -10.0f, 10.0f, "Min: %.2f", "Max: %.2f");
}

void ImGuiManager::showEmissionPolygonEditor() {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Emission Area Polygon");
    ImGui::Text("Vertices define the emission area (0 = point emitter)");

    ImGui::SliderInt("Vertex Count", &cfg.emissionVertexCount, 0, EDITOR_MAX_VERTICES);

    if (cfg.emissionVertexCount > 0) {
        ImGui::Separator();
        ImGui::Text("Vertex Positions:");

        for (int i = 0; i < cfg.emissionVertexCount; ++i) {
            ImGui::PushID(i);
            char label[32];
            snprintf(label, sizeof(label), "Vertex %d", i);
            float vertex[2] = { cfg.emissionVertices[i * 2], cfg.emissionVertices[i * 2 + 1] };
            if (ImGui::DragFloat2(label, vertex, 0.01f, -5.0f, 5.0f, "%.3f")) {
                cfg.emissionVertices[i * 2] = vertex[0];
                cfg.emissionVertices[i * 2 + 1] = vertex[1];
            }
            ImGui::PopID();
        }

        ImGui::Separator();

        // Visual polygon editor
        ImGui::Text("Click and drag vertices in the preview below:");

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImVec2(300, 300);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Draw background
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                IM_COL32(40, 40, 40, 255));
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                          IM_COL32(100, 100, 100, 255));

        // Draw grid
        float gridStep = 30.0f;
        for (float x = gridStep; x < canvasSize.x; x += gridStep) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                              ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
                              IM_COL32(60, 60, 60, 255));
        }
        for (float y = gridStep; y < canvasSize.y; y += gridStep) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                              ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
                              IM_COL32(60, 60, 60, 255));
        }

        // Draw center crosshair
        float centerX = canvasPos.x + canvasSize.x * 0.5f;
        float centerY = canvasPos.y + canvasSize.y * 0.5f;
        drawList->AddLine(ImVec2(centerX - 10, centerY), ImVec2(centerX + 10, centerY), IM_COL32(100, 100, 100, 255));
        drawList->AddLine(ImVec2(centerX, centerY - 10), ImVec2(centerX, centerY + 10), IM_COL32(100, 100, 100, 255));

        // Scale factor for world coords to canvas coords
        float scale = 100.0f;

        // Draw polygon edges
        if (cfg.emissionVertexCount >= 2) {
            for (int i = 0; i < cfg.emissionVertexCount; ++i) {
                int next = (i + 1) % cfg.emissionVertexCount;
                float x1 = centerX + cfg.emissionVertices[i * 2] * scale;
                float y1 = centerY - cfg.emissionVertices[i * 2 + 1] * scale;
                float x2 = centerX + cfg.emissionVertices[next * 2] * scale;
                float y2 = centerY - cfg.emissionVertices[next * 2 + 1] * scale;
                drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 200, 100, 255), 2.0f);
            }
        }

        // Draw and handle vertex dragging
        ImGuiIO& io = ImGui::GetIO();
        ImGui::InvisibleButton("PolygonCanvas", canvasSize);
        bool isHovered = ImGui::IsItemHovered();

        for (int i = 0; i < cfg.emissionVertexCount; ++i) {
            float vx = centerX + cfg.emissionVertices[i * 2] * scale;
            float vy = centerY - cfg.emissionVertices[i * 2 + 1] * scale;

            bool vertexHovered = isHovered &&
                fabsf(io.MousePos.x - vx) < 10.0f &&
                fabsf(io.MousePos.y - vy) < 10.0f;

            // Highlight selected or hovered vertex
            ImU32 vertexColor = IM_COL32(255, 255, 255, 255);
            if (editorState_.selectedVertexIndex == i) {
                vertexColor = IM_COL32(255, 200, 0, 255);
            } else if (vertexHovered) {
                vertexColor = IM_COL32(200, 200, 255, 255);
            }

            drawList->AddCircleFilled(ImVec2(vx, vy), 8.0f, vertexColor);
            drawList->AddCircle(ImVec2(vx, vy), 8.0f, IM_COL32(0, 0, 0, 255), 0, 2.0f);

            // Handle clicking on vertex
            if (vertexHovered && ImGui::IsMouseClicked(0)) {
                editorState_.selectedVertexIndex = i;
                editorState_.isDraggingVertex = true;
            }
        }

        // Handle vertex dragging
        if (editorState_.isDraggingVertex && editorState_.selectedVertexIndex >= 0) {
            if (ImGui::IsMouseDown(0)) {
                float newX = (io.MousePos.x - centerX) / scale;
                float newY = (centerY - io.MousePos.y) / scale;
                cfg.emissionVertices[editorState_.selectedVertexIndex * 2] = newX;
                cfg.emissionVertices[editorState_.selectedVertexIndex * 2 + 1] = newY;
            } else {
                editorState_.isDraggingVertex = false;
            }
        }
    }

    // Preset polygons
    ImGui::Separator();
    ImGui::Text("Presets:");
    if (ImGui::Button("Circle (8 verts)")) {
        cfg.emissionVertexCount = 8;
        for (int i = 0; i < 8; ++i) {
            float angle = (float)i * 6.28318f / 8.0f;
            cfg.emissionVertices[i * 2] = cosf(angle) * 0.5f;
            cfg.emissionVertices[i * 2 + 1] = sinf(angle) * 0.5f;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Square")) {
        cfg.emissionVertexCount = 4;
        cfg.emissionVertices[0] = -0.5f; cfg.emissionVertices[1] = -0.5f;
        cfg.emissionVertices[2] = 0.5f; cfg.emissionVertices[3] = -0.5f;
        cfg.emissionVertices[4] = 0.5f; cfg.emissionVertices[5] = 0.5f;
        cfg.emissionVertices[6] = -0.5f; cfg.emissionVertices[7] = 0.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Line")) {
        cfg.emissionVertexCount = 2;
        cfg.emissionVertices[0] = -0.5f; cfg.emissionVertices[1] = 0.0f;
        cfg.emissionVertices[2] = 0.5f; cfg.emissionVertices[3] = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Point")) {
        cfg.emissionVertexCount = 0;
    }
}

void ImGuiManager::showTextureSelector(PakResource* pakResource) {
    ParticleEmitterConfig& cfg = editorState_.config;

    ImGui::Text("Particle Textures");
    ImGui::Text("Select textures for particles (random selection per particle)");

    // Show currently selected textures
    ImGui::Text("Selected Textures: %d / 8", editorState_.selectedTextureCount);

    for (int i = 0; i < editorState_.selectedTextureCount; ++i) {
        ImGui::PushID(i);
        ImGui::Text("%d: %s (ID: %llu)", i, editorState_.textureNames[i],
                    (unsigned long long)editorState_.selectedTextureIds[i]);
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            // Remove this texture
            for (int j = i; j < editorState_.selectedTextureCount - 1; ++j) {
                editorState_.selectedTextureIds[j] = editorState_.selectedTextureIds[j + 1];
                strncpy(editorState_.textureNames[j], editorState_.textureNames[j + 1], 63);
            }
            editorState_.selectedTextureCount--;

            // Update config
            cfg.textureCount = editorState_.selectedTextureCount;
            for (int j = 0; j < cfg.textureCount; ++j) {
                cfg.textureIds[j] = editorState_.selectedTextureIds[j];
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();

    // Show available textures from pak file
    ImGui::Text("Available Textures (PNG files in pak):");
    ImGui::Text("(Textures must be loaded first via loadTexture() in Lua)");

    // Common texture names to show
    const char* commonTextures[] = {
        "bloom.png",
        "rock.png",
        "chain.png",
        "lantern.png",
        "metalwall.png"
    };
    int numCommonTextures = 5;

    for (int i = 0; i < numCommonTextures; ++i) {
        if (editorState_.selectedTextureCount >= 8) break;

        ImGui::PushID(100 + i);
        if (ImGui::Button(commonTextures[i])) {
            // Add texture to selection
            uint64_t texId = std::hash<std::string>{}(commonTextures[i]);
            editorState_.selectedTextureIds[editorState_.selectedTextureCount] = texId;
            strncpy(editorState_.textureNames[editorState_.selectedTextureCount], commonTextures[i], 63);
            editorState_.selectedTextureCount++;

            // Update config
            cfg.textureCount = editorState_.selectedTextureCount;
            for (int j = 0; j < cfg.textureCount; ++j) {
                cfg.textureIds[j] = editorState_.selectedTextureIds[j];
            }
        }
        ImGui::PopID();
    }
}

void ImGuiManager::showLuaExport() {
    ImGui::Text("Export to Lua");
    ImGui::Text("Copy the Lua table below to use in your scene:");

    if (ImGui::Button("Generate Lua Code")) {
        generateLuaExport();
    }

    ImGui::SameLine();
    if (ImGui::Button("Copy to Clipboard") && editorState_.exportBuffer[0] != '\0') {
        ImGui::SetClipboardText(editorState_.exportBuffer);
    }

    ImGui::Separator();

    // Show export text in a read-only multiline text box
    ImGui::InputTextMultiline("##LuaExport", editorState_.exportBuffer, sizeof(editorState_.exportBuffer),
                               ImVec2(-1, 400), ImGuiInputTextFlags_ReadOnly);
}

void ImGuiManager::generateLuaExport() {
    ParticleEmitterConfig& cfg = editorState_.config;

    char* buf = editorState_.exportBuffer;
    int pos = 0;
    int bufSize = sizeof(editorState_.exportBuffer);

    pos += snprintf(buf + pos, bufSize - pos, "local particleConfig = {\n");
    pos += snprintf(buf + pos, bufSize - pos, "    maxParticles = %d,\n", cfg.maxParticles);
    pos += snprintf(buf + pos, bufSize - pos, "    emissionRate = %.1f,\n", cfg.emissionRate);
    pos += snprintf(buf + pos, bufSize - pos, "    blendMode = %d,  -- %s\n", cfg.blendMode,
                    cfg.blendMode == PARTICLE_BLEND_ADDITIVE ? "PARTICLE_BLEND_ADDITIVE" : "PARTICLE_BLEND_ALPHA");

    // Emission polygon
    if (cfg.emissionVertexCount > 0) {
        pos += snprintf(buf + pos, bufSize - pos, "\n    -- Emission polygon (%d vertices)\n", cfg.emissionVertexCount);
        pos += snprintf(buf + pos, bufSize - pos, "    emissionVertices = {");
        for (int i = 0; i < cfg.emissionVertexCount * 2; ++i) {
            if (i > 0) pos += snprintf(buf + pos, bufSize - pos, ", ");
            pos += snprintf(buf + pos, bufSize - pos, "%.3f", cfg.emissionVertices[i]);
        }
        pos += snprintf(buf + pos, bufSize - pos, "},\n");
    }

    // Textures
    if (editorState_.selectedTextureCount > 0) {
        pos += snprintf(buf + pos, bufSize - pos, "\n    -- Textures (load with loadTexture() first)\n");
        pos += snprintf(buf + pos, bufSize - pos, "    textureIds = {");
        for (int i = 0; i < editorState_.selectedTextureCount; ++i) {
            if (i > 0) pos += snprintf(buf + pos, bufSize - pos, ", ");
            pos += snprintf(buf + pos, bufSize - pos, "%sTexId", editorState_.textureNames[i]);
        }
        pos += snprintf(buf + pos, bufSize - pos, "},\n");
    }

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- Position variance\n");
    pos += snprintf(buf + pos, bufSize - pos, "    positionVariance = %.3f,\n", cfg.positionVariance);

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- Velocity\n");
    pos += snprintf(buf + pos, bufSize - pos, "    velocityMinX = %.2f,\n", cfg.velocityMinX);
    pos += snprintf(buf + pos, bufSize - pos, "    velocityMaxX = %.2f,\n", cfg.velocityMaxX);
    pos += snprintf(buf + pos, bufSize - pos, "    velocityMinY = %.2f,\n", cfg.velocityMinY);
    pos += snprintf(buf + pos, bufSize - pos, "    velocityMaxY = %.2f,\n", cfg.velocityMaxY);

    if (cfg.radialVelocityMin != 0.0f || cfg.radialVelocityMax != 0.0f) {
        pos += snprintf(buf + pos, bufSize - pos, "    radialVelocityMin = %.2f,\n", cfg.radialVelocityMin);
        pos += snprintf(buf + pos, bufSize - pos, "    radialVelocityMax = %.2f,\n", cfg.radialVelocityMax);
    }

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- Acceleration\n");
    pos += snprintf(buf + pos, bufSize - pos, "    accelerationMinX = %.2f,\n", cfg.accelerationMinX);
    pos += snprintf(buf + pos, bufSize - pos, "    accelerationMaxX = %.2f,\n", cfg.accelerationMaxX);
    pos += snprintf(buf + pos, bufSize - pos, "    accelerationMinY = %.2f,\n", cfg.accelerationMinY);
    pos += snprintf(buf + pos, bufSize - pos, "    accelerationMaxY = %.2f,\n", cfg.accelerationMaxY);

    if (cfg.radialAccelerationMin != 0.0f || cfg.radialAccelerationMax != 0.0f) {
        pos += snprintf(buf + pos, bufSize - pos, "    radialAccelerationMin = %.2f,\n", cfg.radialAccelerationMin);
        pos += snprintf(buf + pos, bufSize - pos, "    radialAccelerationMax = %.2f,\n", cfg.radialAccelerationMax);
    }

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- Size\n");
    pos += snprintf(buf + pos, bufSize - pos, "    startSizeMin = %.3f,\n", cfg.startSizeMin);
    pos += snprintf(buf + pos, bufSize - pos, "    startSizeMax = %.3f,\n", cfg.startSizeMax);
    pos += snprintf(buf + pos, bufSize - pos, "    endSizeMin = %.3f,\n", cfg.endSizeMin);
    pos += snprintf(buf + pos, bufSize - pos, "    endSizeMax = %.3f,\n", cfg.endSizeMax);

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- Start color\n");
    pos += snprintf(buf + pos, bufSize - pos, "    colorMinR = %.3f, colorMaxR = %.3f,\n", cfg.colorMinR, cfg.colorMaxR);
    pos += snprintf(buf + pos, bufSize - pos, "    colorMinG = %.3f, colorMaxG = %.3f,\n", cfg.colorMinG, cfg.colorMaxG);
    pos += snprintf(buf + pos, bufSize - pos, "    colorMinB = %.3f, colorMaxB = %.3f,\n", cfg.colorMinB, cfg.colorMaxB);
    pos += snprintf(buf + pos, bufSize - pos, "    colorMinA = %.3f, colorMaxA = %.3f,\n", cfg.colorMinA, cfg.colorMaxA);

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- End color\n");
    pos += snprintf(buf + pos, bufSize - pos, "    endColorMinR = %.3f, endColorMaxR = %.3f,\n", cfg.endColorMinR, cfg.endColorMaxR);
    pos += snprintf(buf + pos, bufSize - pos, "    endColorMinG = %.3f, endColorMaxG = %.3f,\n", cfg.endColorMinG, cfg.endColorMaxG);
    pos += snprintf(buf + pos, bufSize - pos, "    endColorMinB = %.3f, endColorMaxB = %.3f,\n", cfg.endColorMinB, cfg.endColorMaxB);
    pos += snprintf(buf + pos, bufSize - pos, "    endColorMinA = %.3f, endColorMaxA = %.3f,\n", cfg.endColorMinA, cfg.endColorMaxA);

    pos += snprintf(buf + pos, bufSize - pos, "\n    -- Lifetime\n");
    pos += snprintf(buf + pos, bufSize - pos, "    lifetimeMin = %.2f,\n", cfg.lifetimeMin);
    pos += snprintf(buf + pos, bufSize - pos, "    lifetimeMax = %.2f,\n", cfg.lifetimeMax);

    // Rotation (only if non-zero)
    bool hasRotation = cfg.rotationMinX != 0.0f || cfg.rotationMaxX != 0.0f ||
                       cfg.rotationMinY != 0.0f || cfg.rotationMaxY != 0.0f ||
                       cfg.rotationMinZ != 0.0f || cfg.rotationMaxZ != 0.0f;
    bool hasRotVel = cfg.rotVelocityMinX != 0.0f || cfg.rotVelocityMaxX != 0.0f ||
                     cfg.rotVelocityMinY != 0.0f || cfg.rotVelocityMaxY != 0.0f ||
                     cfg.rotVelocityMinZ != 0.0f || cfg.rotVelocityMaxZ != 0.0f;
    bool hasRotAccel = cfg.rotAccelerationMinX != 0.0f || cfg.rotAccelerationMaxX != 0.0f ||
                       cfg.rotAccelerationMinY != 0.0f || cfg.rotAccelerationMaxY != 0.0f ||
                       cfg.rotAccelerationMinZ != 0.0f || cfg.rotAccelerationMaxZ != 0.0f;

    if (hasRotation || hasRotVel || hasRotAccel) {
        pos += snprintf(buf + pos, bufSize - pos, "\n    -- Rotation\n");
        if (hasRotation) {
            pos += snprintf(buf + pos, bufSize - pos, "    rotationMinX = %.2f, rotationMaxX = %.2f,\n", cfg.rotationMinX, cfg.rotationMaxX);
            pos += snprintf(buf + pos, bufSize - pos, "    rotationMinY = %.2f, rotationMaxY = %.2f,\n", cfg.rotationMinY, cfg.rotationMaxY);
            pos += snprintf(buf + pos, bufSize - pos, "    rotationMinZ = %.2f, rotationMaxZ = %.2f,\n", cfg.rotationMinZ, cfg.rotationMaxZ);
        }
        if (hasRotVel) {
            pos += snprintf(buf + pos, bufSize - pos, "    rotVelocityMinX = %.2f, rotVelocityMaxX = %.2f,\n", cfg.rotVelocityMinX, cfg.rotVelocityMaxX);
            pos += snprintf(buf + pos, bufSize - pos, "    rotVelocityMinY = %.2f, rotVelocityMaxY = %.2f,\n", cfg.rotVelocityMinY, cfg.rotVelocityMaxY);
            pos += snprintf(buf + pos, bufSize - pos, "    rotVelocityMinZ = %.2f, rotVelocityMaxZ = %.2f,\n", cfg.rotVelocityMinZ, cfg.rotVelocityMaxZ);
        }
        if (hasRotAccel) {
            pos += snprintf(buf + pos, bufSize - pos, "    rotAccelerationMinX = %.2f, rotAccelerationMaxX = %.2f,\n", cfg.rotAccelerationMinX, cfg.rotAccelerationMaxX);
            pos += snprintf(buf + pos, bufSize - pos, "    rotAccelerationMinY = %.2f, rotAccelerationMaxY = %.2f,\n", cfg.rotAccelerationMinY, cfg.rotAccelerationMaxY);
            pos += snprintf(buf + pos, bufSize - pos, "    rotAccelerationMinZ = %.2f, rotAccelerationMaxZ = %.2f,\n", cfg.rotAccelerationMinZ, cfg.rotAccelerationMaxZ);
        }
    }

    pos += snprintf(buf + pos, bufSize - pos, "}\n\n");
    pos += snprintf(buf + pos, bufSize - pos, "-- Create the particle system:\n");
    pos += snprintf(buf + pos, bufSize - pos, "-- particlePipelineId = loadParticleShaders(\"particle_vertex.spv\", \"particle_fragment.spv\", 1, true)\n");
    pos += snprintf(buf + pos, bufSize - pos, "-- particleSystemId = createParticleSystem(particleConfig, particlePipelineId)\n");
    pos += snprintf(buf + pos, bufSize - pos, "-- setParticleSystemPosition(particleSystemId, x, y)\n");
}

void ImGuiManager::updatePreviewSystem(ParticleSystemManager* particleManager, int pipelineId) {
    if (!particleManager) return;

    ParticleEmitterConfig& cfg = editorState_.config;

    // Destroy existing preview system if config changed significantly
    // For now, we'll just update the emission rate dynamically
    if (editorState_.previewSystemId >= 0) {
        // Update the system position to be at the preview offset
        particleManager->setSystemPosition(editorState_.previewSystemId,
                                            editorState_.previewOffsetX,
                                            editorState_.previewOffsetY);
        particleManager->setSystemEmissionRate(editorState_.previewSystemId, cfg.emissionRate);

        // Get the system and update its config
        ParticleSystem* system = particleManager->getSystem(editorState_.previewSystemId);
        if (system) {
            system->config = cfg;
        }
    } else if (pipelineId >= 0) {
        // Create a new preview system
        editorState_.previewSystemId = particleManager->createSystem(cfg, pipelineId);
        particleManager->setSystemPosition(editorState_.previewSystemId,
                                            editorState_.previewOffsetX,
                                            editorState_.previewOffsetY);
    }
}

#endif // DEBUG
