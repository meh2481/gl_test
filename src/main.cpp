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
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);

    Config config = loadConfig();

    SDL_DisplayMode displayMode;
    assert(SDL_GetDesktopDisplayMode(config.display, &displayMode) == 0);

    SDL_Window* window = SDL_CreateWindow("Shader Triangle", SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), displayMode.w, displayMode.h, SDL_WINDOW_VULKAN | config.fullscreenMode);
    assert(window != nullptr);

    PakResource pakResource;
    pakResource.load(PAK_FILE);

    VulkanRenderer renderer;
    SceneManager sceneManager(pakResource, renderer);
    renderer.initialize(window);

    // Initialize keybinding manager
    KeybindingManager keybindings;

    // Load keybindings from config if available
    if (config.keybindings[0] != '\0') {
        keybindings.deserializeBindings(config.keybindings);
    }

    // Load initial scene
    sceneManager.pushScene(LUA_SCRIPT_ID);

#ifdef DEBUG
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
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN) {
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

        renderer.render(currentTime);
    }

    // Save current display to config
    config.display = SDL_GetWindowDisplayIndex(window);

    // Save keybindings to config
    keybindings.serializeBindings(config.keybindings, MAX_KEYBINDING_STRING);

    saveConfig(config);

#ifdef DEBUG
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