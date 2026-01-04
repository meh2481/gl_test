#pragma once

#ifdef DEBUG

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include "../effects/ParticleSystem.h"
#include "../core/String.h"
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"
#include "../memory/SmallMemoryAllocator.h"
#include "../memory/LargeMemoryAllocator.h"
#include <cstdint>

// Forward declarations
class VulkanRenderer;
class ParticleSystemManager;
class PakResource;
class SceneManager;
class LuaInterface;
class ConsoleBuffer;
class TrigLookup;

// Maximum emission polygon vertices / textures
static const int EDITOR_MAX_VERTICES = 8;
static const int EDITOR_MAX_TEXTURES = 8;
static const int EDITOR_MAX_TEXTURE_NAME_LEN = 64;
static const int EDITOR_MAX_TEXTURE_FILES = 64;

// Maximum filename length for save/load
static const int EDITOR_MAX_FILENAME_LEN = 128;

// Maximum number of FX files to list
static const int EDITOR_MAX_FX_FILES = 64;

// Structure to hold particle editor state
struct ParticleEditorState {
    bool isActive;
    ParticleEmitterConfig config;

    // Preview particle system
    int previewSystemId;
    int previewPipelineId;
    bool needsReset;  // Flag to signal that preview system needs to be destroyed and reset

    // Emission polygon editing
    int selectedVertexIndex;
    bool isDraggingVertex;

    // Texture selection
    uint64_t selectedTextureIds[EDITOR_MAX_TEXTURES];
    int selectedTextureCount;
    char textureNames[EDITOR_MAX_TEXTURES][EDITOR_MAX_TEXTURE_NAME_LEN];

    // Preview camera
    float previewZoom;
    float previewOffsetX;
    float previewOffsetY;
    bool previewCameraChanged;  // Flag to indicate preview controls were changed by user
    bool previewResetRequested; // Flag to indicate user wants to reset camera

    // Preview background color (not saved, just for editor visualization)
    float previewBackgroundR;
    float previewBackgroundG;
    float previewBackgroundB;

    // Export state
    bool showExportPopup;
    char exportBuffer[8192];

    // Save/Load state
    char saveFilename[EDITOR_MAX_FILENAME_LEN];
    char loadFilename[EDITOR_MAX_FILENAME_LEN];
    char statusMessage[256];  // Status message for save/load operations

    // FX file list (dynamically enumerated from res/fx/)
    char fxFileList[EDITOR_MAX_FX_FILES][EDITOR_MAX_FILENAME_LEN];
    int fxFileCount;
    int selectedFxFileIndex;  // Index of currently selected file (-1 if none)

    // Texture file list (dynamically enumerated from res/fx/)
    char textureFileList[EDITOR_MAX_TEXTURE_FILES][EDITOR_MAX_FILENAME_LEN];
    int textureFileCount;

    // UI state
    bool colorsExpanded;
    bool velocityExpanded;
    bool accelerationExpanded;
    bool sizeExpanded;
    bool rotationExpanded;
    bool emissionExpanded;

    // System recreation tracking
    int lastMaxParticles;  // Track changes that require system recreation
    float lastSystemLifetime;  // Track changes that require system recreation
    ParticleBlendMode lastBlendMode;  // Track blend mode changes for pipeline selection
};

class ImGuiManager {
public:
    ImGuiManager(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer, TrigLookup* trigLookup);
    ~ImGuiManager();

    // Initialize ImGui with Vulkan and SDL3
    void initialize(SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                    VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
                    VkRenderPass renderPass, uint32_t imageCount, VkSampleCountFlagBits msaaSamples);

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

    // Show memory allocator window
    void showMemoryAllocatorWindow(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator, float currentTime);

    // Check if ImGui wants to capture mouse input
    bool wantCaptureMouse() const;

    // Particle editor functions
    void setParticleEditorActive(bool active);
    bool isParticleEditorActive() const;
    void showParticleEditorWindow(ParticleSystemManager* particleManager, PakResource* pakResource,
                                   VulkanRenderer* renderer, class LuaInterface* luaInterface, float deltaTime, class SceneManager* sceneManager = nullptr);
    ParticleEditorState& getEditorState() { return editorState_; }
    void destroyPreviewSystem(ParticleSystemManager* particleManager);
    int getPreviewSystemId() const { return editorState_.previewSystemId; }
    void onSceneReload();  // Called when scene is reloaded (F5)

    // Sync preview controls with camera
    void syncPreviewWithCamera(float cameraOffsetX, float cameraOffsetY, float cameraZoom);
    void getPreviewCameraSettings(float* offsetX, float* offsetY, float* zoom) const;

private:
    bool initialized_;
    VkDevice device_;
    VkDescriptorPool imguiPool_;

    // Particle editor state
    ParticleEditorState editorState_;

    // Memory allocator for string operations
    MemoryAllocator* stringAllocator_;

    // Console buffer for displaying console output
    ConsoleBuffer* consoleBuffer_;

    // Trig lookup table for fast sin/cos calculations
    TrigLookup* trigLookup_;

    // Initialize particle editor state with defaults
    void initializeParticleEditorDefaults();

    // Helper functions for particle editor
    void showEmissionSettings();
    void showVelocitySettings();
    void showAccelerationSettings();
    void showSizeSettings();
    void showColorSettings();
    void showRotationSettings();
    void showEmissionPolygonEditor();
    void showTextureSelector(PakResource* pakResource, VulkanRenderer* renderer);
    void showLuaExport();
    void showSaveLoadSection();
    void updatePreviewSystem(ParticleSystemManager* particleManager, int pipelineId);
    void generateLuaExport();
    void generateSaveableExport(char* buffer, int bufferSize);
    bool saveParticleConfig(const char* filename);
    void refreshFxFileList();
    void refreshTextureFileList();
    bool loadParticleConfigFromFile(const char* filename);

    // Helper function to truncate texture names for display
    String truncateTextureName(const char* fullName, float maxWidth);

    // ImGui texture cache for preview images
    HashTable<uint64_t, VkDescriptorSet> imguiTextureCache_;
};

#endif // DEBUG
