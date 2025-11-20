#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
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

#ifdef DEBUG
// Structure to pass data to the hot-reload thread
struct HotReloadData {
    SDL_mutex* mutex;
    SDL_atomic_t reloadComplete;
    SDL_atomic_t reloadSuccess;
    SDL_atomic_t reloadRequested;
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
        while (SDL_AtomicGet(&reloadData->reloadRequested) == 0) {
            SDL_Delay(100);
        }

        // Lock mutex to prevent concurrent reloads
        SDL_LockMutex(reloadData->mutex);

        std::cout << "Hot-reloading resources in background thread..." << std::endl;

        // Rebuild shaders and pak file using make
        int result = system("make shaders && make res_pak");

        // Store result
        SDL_AtomicSet(&reloadData->reloadSuccess, (result == 0) ? 1 : 0);
        SDL_AtomicSet(&reloadData->reloadComplete, 1);
        SDL_AtomicSet(&reloadData->reloadRequested, 0);

        SDL_UnlockMutex(reloadData->mutex);
    }

    return 0;
}
#endif

int main() {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);

    Config config = loadConfig();

    SDL_DisplayMode displayMode;
    assert(SDL_GetDesktopDisplayMode(config.display, &displayMode) == 0);

    SDL_Window* window = SDL_CreateWindow("Shader Triangle", SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), displayMode.w, displayMode.h, SDL_WINDOW_VULKAN | config.fullscreenMode);
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
    SDL_GameController* gameController = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            gameController = SDL_GameControllerOpen(i);
            if (gameController) {
                vibrationManager.setGameController(gameController);
                std::cout << "Game Controller " << i << " connected: " 
                         << SDL_GameControllerName(gameController) << std::endl;
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
    SDL_AtomicSet(&reloadData.reloadComplete, 0);
    SDL_AtomicSet(&reloadData.reloadSuccess, 0);
    SDL_AtomicSet(&reloadData.reloadRequested, 0);

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
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                // Handle actions bound to this key
                const ActionList& actionList = keybindings.getActionsForKey(event.key.keysym.sym);
                for (int i = 0; i < actionList.count; ++i) {
                    sceneManager.handleAction(actionList.actions[i]);
                }

                // Handle special case: ALT+ENTER for fullscreen toggle
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    if (flags & config.fullscreenMode) {
                        SDL_SetWindowFullscreen(window, 0);
                        config.display = SDL_GetWindowDisplayIndex(window);
                    } else {
                        SDL_SetWindowFullscreen(window, config.fullscreenMode);
                        config.display = SDL_GetWindowDisplayIndex(window);
                    }
                    saveConfig(config);
                }
#ifdef DEBUG
                // Handle special case: F5 for hot reload
                if (event.key.keysym.sym == SDLK_F5) {
                    // Check if reload thread is ready
                    if (SDL_AtomicGet(&reloadData.reloadRequested) == 0) {
                        std::cout << "Requesting hot-reload..." << std::endl;
                        SDL_AtomicSet(&reloadData.reloadComplete, 0);
                        SDL_AtomicSet(&reloadData.reloadRequested, 1);
                    }
                }
#endif
            }
            // Handle gamepad button press
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const ActionList& actionList = keybindings.getActionsForGamepadButton(event.cbutton.button);
                for (int i = 0; i < actionList.count; ++i) {
                    sceneManager.handleAction(actionList.actions[i]);
                }
            }
            // Handle gamepad connection
            if (event.type == SDL_CONTROLLERDEVICEADDED && !gameController) {
                gameController = SDL_GameControllerOpen(event.cdevice.which);
                if (gameController) {
                    vibrationManager.setGameController(gameController);
                    std::cout << "Game Controller connected: " 
                             << SDL_GameControllerName(gameController) << std::endl;
                    if (vibrationManager.hasRumbleSupport()) {
                        std::cout << "  Rumble support: Yes" << std::endl;
                    }
                    if (vibrationManager.hasTriggerRumbleSupport()) {
                        std::cout << "  Trigger rumble support: Yes (DualSense)" << std::endl;
                    }
                }
            }
            // Handle gamepad disconnection
            if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (gameController && event.cdevice.which == SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(gameController))) {
                    std::cout << "Game Controller disconnected" << std::endl;
                    vibrationManager.setGameController(nullptr);
                    SDL_GameControllerClose(gameController);
                    gameController = nullptr;
                }
            }
        }

        if(!sceneManager.updateActiveScene(deltaTime)) {
            running = false;
        }

#ifdef DEBUG
        // Check if hot-reload completed
        if (SDL_AtomicGet(&reloadData.reloadComplete) == 1) {
            if (SDL_AtomicGet(&reloadData.reloadSuccess) == 1) {
                std::cout << "Hot-reload complete, applying changes..." << std::endl;
                // Reload pak
                pakResource.reload(PAK_FILE);
                // Reload current scene with new resources
                sceneManager.reloadCurrentScene();
            } else {
                std::cout << "Hot-reload failed!" << std::endl;
            }
            SDL_AtomicSet(&reloadData.reloadComplete, 0);
        }
#endif

#ifdef DEBUG
        // Start ImGui frame
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        imguiManager.newFrame(width, height);

        // Show console window
        imguiManager.showConsoleWindow();
#endif

        renderer.render(currentTime);
    }

    // Save current display to config
    config.display = SDL_GetWindowDisplayIndex(window);

    // Save keybindings to config
    keybindings.serializeBindings(config.keybindings, MAX_KEYBINDING_STRING);

    saveConfig(config);

    // Close game controller if open
    if (gameController) {
        SDL_GameControllerClose(gameController);
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