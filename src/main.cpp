#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <lua.hpp>
#include "VulkanRenderer.h"
#include "SceneManager.h"
#include "config.h"
#include "InputActions.h"
#include "VibrationManager.h"
#include "LuaInterface.h"

#ifdef DEBUG
#include "ImGuiManager.h"
#include "ConsoleBuffer.h"
#endif

#define LUA_SCRIPT_ID 14669932163325785351ULL
#define PAK_FILE "res.pak"

inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

// Convert screen coordinates to world coordinates
// World coordinates are -aspect to aspect in x, -1 to 1 in y (aspect = width/height)
// Accounts for camera offset and zoom
inline void screenToWorld(float screenX, float screenY, int windowWidth, int windowHeight,
                          float cameraOffsetX, float cameraOffsetY, float cameraZoom,
                          float* worldX, float* worldY) {
    float aspect = windowWidth / (float)windowHeight;
    // Convert to normalized device coordinates (-1 to 1)
    float ndcX = (screenX / (float)windowWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenY / (float)windowHeight) * 2.0f; // Flip Y
    // Apply inverse camera transform (aspect, zoom, then offset)
    *worldX = (ndcX * aspect / cameraZoom) + cameraOffsetX;
    *worldY = (ndcY / cameraZoom) + cameraOffsetY;
}

// Pan tracking state
static bool isPanning = false;
static float panStartCursorX = 0.0f;
static float panStartCursorY = 0.0f;
static float panStartCameraX = 0.0f;
static float panStartCameraY = 0.0f;

#ifdef DEBUG
// Structure to pass data to the hot-reload thread
struct HotReloadData {
    SDL_Mutex* mutex;
    SDL_AtomicInt reloadComplete;
    SDL_AtomicInt reloadSuccess;
    SDL_AtomicInt reloadRequested;
};

// Global ImGuiManager pointer for callback
static ImGuiManager* g_imguiManager = nullptr;

// Callback for rendering ImGui
static void renderImGuiCallback(VkCommandBuffer commandBuffer) {
    if (g_imguiManager) {
        g_imguiManager->render(commandBuffer);
    }
}

// Thread function for hot-reloading resources
// This allows F5 hot-reload to happen in the background without blocking the main thread
// The thread waits for reload requests and rebuilds shaders/resources asynchronously
static int hotReloadThread(void* data) {
    HotReloadData* reloadData = (HotReloadData*)data;

    while (true) {
        // Wait for reload request
        while (SDL_GetAtomicInt(&reloadData->reloadRequested) == 0) {
            SDL_Delay(100);
        }

        // Lock mutex to prevent concurrent reloads
        SDL_LockMutex(reloadData->mutex);

        std::cout << "Hot-reloading resources in background thread..." << std::endl;

        // Rebuild shaders and pak file using make
        int result = system("make shaders && make res_pak");

        // Store result
        SDL_SetAtomicInt(&reloadData->reloadSuccess, (result == 0) ? 1 : 0);
        SDL_SetAtomicInt(&reloadData->reloadComplete, 1);
        SDL_SetAtomicInt(&reloadData->reloadRequested, 0);

        SDL_UnlockMutex(reloadData->mutex);
    }

    return 0;
}
#endif

int main() {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));

    Config config = loadConfig();

    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    if (config.display == 0) {
        config.display = primaryDisplay;
    }

    const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(config.display);
    if (displayMode == nullptr) {
        config.display = primaryDisplay;
        displayMode = SDL_GetDesktopDisplayMode(config.display);
        assert(displayMode != nullptr);
    }

    std::cout << "Launching on display: " << config.display << std::endl;

    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(config.display);
    int y = SDL_WINDOWPOS_CENTERED_DISPLAY(config.display);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Shader Triangle");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, displayMode->w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, displayMode->h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_VULKAN | config.fullscreenMode);

    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    std::cout << "error code: " << SDL_GetError() << std::endl;
    assert(window != nullptr);

    PakResource pakResource;
    pakResource.load(PAK_FILE);

    VulkanRenderer renderer;
    VibrationManager vibrationManager;
    SceneManager sceneManager(pakResource, renderer, &vibrationManager);
    renderer.initialize(window);

    // Initialize keybinding manager
    KeybindingManager keybindings;

    // Load keybindings from config if available
    if (config.keybindings[0] != '\0') {
        keybindings.deserializeBindings(config.keybindings);
    }

    // Open all available game controllers
    SDL_Gamepad* gameController = nullptr;
    int numJoysticks;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&numJoysticks);
    if (joysticks) {
        for (int i = 0; i < numJoysticks; ++i) {
            if (SDL_IsGamepad(joysticks[i])) {
                gameController = SDL_OpenGamepad(joysticks[i]);
                if (gameController) {
                    vibrationManager.setGameController(gameController);
                    std::cout << "Game Controller " << i << " connected: "
                             << SDL_GetGamepadName(gameController) << std::endl;
                    if (vibrationManager.hasRumbleSupport()) {
                        std::cout << "  Rumble support: Yes" << std::endl;
                    }
                    if (vibrationManager.hasTriggerRumbleSupport()) {
                        std::cout << "  Trigger rumble support: Yes (DualSense)" << std::endl;
                    }
                    break; // Use the first available controller
                }
            }
        }
        SDL_free(joysticks);
    }

    // Load initial scene
    sceneManager.pushScene(LUA_SCRIPT_ID);

#ifdef DEBUG
    // Setup console capture for std::cout
    std::streambuf* coutBuf = std::cout.rdbuf();
    ConsoleCapture consoleCapture(std::cout, coutBuf);
    std::cout.rdbuf(&consoleCapture);

    // Initialize ImGui
    ImGuiManager imguiManager;
    g_imguiManager = &imguiManager;
    imguiManager.initialize(window, renderer.getInstance(), renderer.getPhysicalDevice(),
                           renderer.getDevice(), renderer.getGraphicsQueueFamilyIndex(),
                           renderer.getGraphicsQueue(), renderer.getRenderPass(),
                           renderer.getSwapchainImageCount());

    // Set ImGui render callback in renderer
    renderer.setImGuiRenderCallback(renderImGuiCallback);

    // Initialize hot-reload thread
    HotReloadData reloadData;
    reloadData.mutex = SDL_CreateMutex();
    assert(reloadData.mutex != nullptr);
    SDL_SetAtomicInt(&reloadData.reloadComplete, 0);
    SDL_SetAtomicInt(&reloadData.reloadSuccess, 0);
    SDL_SetAtomicInt(&reloadData.reloadRequested, 0);

    SDL_Thread* reloadThread = SDL_CreateThread(hotReloadThread, "HotReload", &reloadData);
    assert(reloadThread != nullptr);
#endif

    bool running = true;
    SDL_Event event;
    float lastTime = SDL_GetTicks() / 1000.0f;
    while (running) {
        float currentTime = SDL_GetTicks()  / 1000.0f;
        float deltaTime = (currentTime - lastTime);
        lastTime = currentTime;
        while (SDL_PollEvent(&event)) {
#ifdef DEBUG
            // Process event for ImGui first
            imguiManager.processEvent(&event);
#endif
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                // Handle actions bound to this key
                const ActionList& actionList = keybindings.getActionsForKey(event.key.key);
                for (int i = 0; i < actionList.count; ++i) {
                    sceneManager.handleAction(actionList.actions[i]);
                }

                // Handle special case: ALT+ENTER for fullscreen toggle
                if (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)) {
                    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
                    if (flags & SDL_WINDOW_FULLSCREEN) {
                        SDL_SetWindowFullscreen(window, false);
                        config.fullscreenMode = 0;
                        config.display = SDL_GetDisplayForWindow(window);
                        std::cout << "Toggled to windowed on display: " << config.display << std::endl;
                    } else {
                        SDL_SetWindowFullscreen(window, true);
                        config.fullscreenMode = SDL_WINDOW_FULLSCREEN;
                        config.display = SDL_GetDisplayForWindow(window);
                        std::cout << "Toggled to fullscreen on display: " << config.display << std::endl;
                    }
                    saveConfig(config);
                }
#ifdef DEBUG
                // Handle special case: F5 for hot reload
                if (event.key.key == SDLK_F5) {
                    // Check if reload thread is ready
                    if (SDL_GetAtomicInt(&reloadData.reloadRequested) == 0) {
                        std::cout << "Requesting hot-reload..." << std::endl;
                        SDL_SetAtomicInt(&reloadData.reloadComplete, 0);
                        SDL_SetAtomicInt(&reloadData.reloadRequested, 1);
                    }
                }
#endif
            }
            // Handle gamepad button press
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const ActionList& actionList = keybindings.getActionsForGamepadButton(event.gbutton.button);
                for (int i = 0; i < actionList.count; ++i) {
                    sceneManager.handleAction(actionList.actions[i]);
                }
            }
            // Handle gamepad connection
            if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gameController) {
                gameController = SDL_OpenGamepad(event.gdevice.which);
                if (gameController) {
                    vibrationManager.setGameController(gameController);
                    std::cout << "Game Controller connected: "
                             << SDL_GetGamepadName(gameController) << std::endl;
                    if (vibrationManager.hasRumbleSupport()) {
                        std::cout << "  Rumble support: Yes" << std::endl;
                    }
                    if (vibrationManager.hasTriggerRumbleSupport()) {
                        std::cout << "  Trigger rumble support: Yes (DualSense)" << std::endl;
                    }
                }
            }
            // Handle gamepad disconnection
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gameController && event.gdevice.which == SDL_GetGamepadID(gameController)) {
                    std::cout << "Game Controller disconnected" << std::endl;
                    vibrationManager.setGameController(nullptr);
                    SDL_CloseGamepad(gameController);
                    gameController = nullptr;
                }
            }
            // Handle mouse button press for drag actions
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                int windowWidth, windowHeight;
                float worldX, worldY;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                screenToWorld(event.button.x, event.button.y, windowWidth, windowHeight,
                              sceneManager.getCameraOffsetX(), sceneManager.getCameraOffsetY(),
                              sceneManager.getCameraZoom(), &worldX, &worldY);
                sceneManager.setCursorPosition(worldX, worldY);
                sceneManager.handleAction(ACTION_DRAG_START);
            }
            // Handle mouse button release for drag actions
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                sceneManager.handleAction(ACTION_DRAG_END);
            }
            // Handle middle mouse button press for pan
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_MIDDLE) {
                int windowWidth, windowHeight;
                float worldX, worldY;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                screenToWorld(event.button.x, event.button.y, windowWidth, windowHeight,
                              sceneManager.getCameraOffsetX(), sceneManager.getCameraOffsetY(),
                              sceneManager.getCameraZoom(), &worldX, &worldY);
                sceneManager.setCursorPosition(worldX, worldY);
                isPanning = true;
                panStartCursorX = worldX;
                panStartCursorY = worldY;
                panStartCameraX = sceneManager.getCameraOffsetX();
                panStartCameraY = sceneManager.getCameraOffsetY();
                sceneManager.handleAction(ACTION_PAN_START);
            }
            // Handle middle mouse button release for pan
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_MIDDLE) {
                isPanning = false;
                sceneManager.handleAction(ACTION_PAN_END);
            }
            // Handle mouse wheel for zoom
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
#ifdef DEBUG
                // Don't zoom if ImGui wants the mouse (e.g., hovering over ImGui window)
                if (!imguiManager.wantCaptureMouse()) {
                    sceneManager.applyScrollZoom(event.wheel.y);
                }
#else
                sceneManager.applyScrollZoom(event.wheel.y);
#endif
            }
            // Handle mouse motion for cursor tracking
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                int windowWidth, windowHeight;
                float worldX, worldY;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);

                // Update camera offset while panning (before calculating cursor position)
                if (isPanning) {
                    // Calculate cursor position with original camera offset for delta calculation
                    screenToWorld(event.motion.x, event.motion.y, windowWidth, windowHeight,
                                  panStartCameraX, panStartCameraY,
                                  sceneManager.getCameraZoom(), &worldX, &worldY);
                    float deltaX = worldX - panStartCursorX;
                    float deltaY = worldY - panStartCursorY;
                    sceneManager.setCameraOffset(panStartCameraX - deltaX, panStartCameraY - deltaY);
                }

                // Calculate final cursor position with current camera offset
                screenToWorld(event.motion.x, event.motion.y, windowWidth, windowHeight,
                              sceneManager.getCameraOffsetX(), sceneManager.getCameraOffsetY(),
                              sceneManager.getCameraZoom(), &worldX, &worldY);
                sceneManager.setCursorPosition(worldX, worldY);
            }
        }

#ifdef DEBUG
        // Sync particle editor preview controls with camera
        if (sceneManager.isParticleEditorActive()) {
            ParticleEditorState& editorState = imguiManager.getEditorState();

            // If preview controls were changed by user, update the camera
            if (editorState.previewCameraChanged) {
                sceneManager.getLuaInterface()->setCameraOffset(editorState.previewOffsetX, editorState.previewOffsetY);
                sceneManager.getLuaInterface()->setCameraZoom(editorState.previewZoom);
                editorState.previewCameraChanged = false;
            }

            // If reset was requested, also reset the camera
            if (editorState.previewResetRequested) {
                sceneManager.getLuaInterface()->setCameraOffset(0.0f, 0.0f);
                sceneManager.getLuaInterface()->setCameraZoom(1.0f);
                editorState.previewResetRequested = false;
            }

            // Otherwise, sync preview controls FROM the camera (mouse zoom/pan updates)
            imguiManager.syncPreviewWithCamera(
                sceneManager.getCameraOffsetX(),
                sceneManager.getCameraOffsetY(),
                sceneManager.getCameraZoom()
            );
        }
#endif

        // Update renderer with current camera transform
        renderer.setCameraTransform(sceneManager.getCameraOffsetX(),
                                    sceneManager.getCameraOffsetY(),
                                    sceneManager.getCameraZoom());

        if(!sceneManager.updateActiveScene(deltaTime)) {
            running = false;
        }

#ifdef DEBUG
        // Check if hot-reload completed
        if (SDL_GetAtomicInt(&reloadData.reloadComplete) == 1) {
            if (SDL_GetAtomicInt(&reloadData.reloadSuccess) == 1) {
                std::cout << "Hot-reload complete, applying changes..." << std::endl;
                // Reload pak
                pakResource.reload(PAK_FILE);
                // Reload current scene with new resources
                sceneManager.reloadCurrentScene();
            } else {
                std::cout << "Hot-reload failed!" << std::endl;
            }
            SDL_SetAtomicInt(&reloadData.reloadComplete, 0);
        }
#endif

#ifdef DEBUG
        // Start ImGui frame
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        imguiManager.newFrame(width, height);

        // Show console window
        imguiManager.showConsoleWindow();

        // Show particle editor if scene wants it active
        bool sceneWantsEditor = sceneManager.isParticleEditorActive();
        bool editorWasActive = imguiManager.isParticleEditorActive();
        LuaInterface* luaInterface = sceneManager.getLuaInterface();

        if (sceneWantsEditor && !editorWasActive) {
            // Transitioning from inactive to active - activate the editor
            imguiManager.setParticleEditorActive(true);
        } else if (!sceneWantsEditor && editorWasActive) {
            // Transitioning from active to inactive - deactivate and destroy preview
            if (luaInterface) {
                imguiManager.destroyPreviewSystem(&luaInterface->getParticleSystemManager());
            }
            imguiManager.setParticleEditorActive(false);
        }

        if (sceneWantsEditor && luaInterface) {
            imguiManager.showParticleEditorWindow(
                &luaInterface->getParticleSystemManager(),
                &sceneManager.getPakResource(),
                sceneManager.getParticleEditorPipelineId(),
                deltaTime
            );
        }
#endif

        renderer.render(currentTime);
    }

    // Save current fullscreen state and display to config
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN) {
        config.fullscreenMode = SDL_WINDOW_FULLSCREEN;
    } else {
        config.fullscreenMode = 0;
    }
    config.display = SDL_GetDisplayForWindow(window);
    std::cout << "Saving display: " << config.display << std::endl;

    // Save keybindings to config
    keybindings.serializeBindings(config.keybindings, MAX_KEYBINDING_STRING);

    saveConfig(config);

    // Close game controller if open
    if (gameController) {
        SDL_CloseGamepad(gameController);
    }

#ifdef DEBUG
    // Cleanup ImGui
    g_imguiManager = nullptr;
    imguiManager.cleanup();

    // Cleanup hot-reload thread
    // Note: We can't cleanly stop the thread since it's in an infinite loop
    // In a production app, we'd use a flag to signal thread exit
    // For debug builds this is acceptable as the process is exiting anyway
    SDL_DetachThread(reloadThread);
    SDL_DestroyMutex(reloadData.mutex);
#endif

    renderer.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}