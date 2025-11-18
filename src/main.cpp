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

    // Load initial scene
    sceneManager.pushScene(LUA_SCRIPT_ID);

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
                    std::cout << "Hot-reloading resources..." << std::endl;
                    // Rebuild shaders and pak file using make
                    int result = system("make shaders && make res_pak");
                    assert(result == 0);
                    // Reload pak
                    pakResource.load(PAK_FILE);
                    // Reload current scene with new resources
                    sceneManager.reloadCurrentScene();
                }
#endif
            }
        }

        if(!sceneManager.updateActiveScene(deltaTime)) {
            running = false;
        }
        renderer.render(currentTime);
    }

    // Save current display to config
    config.display = SDL_GetWindowDisplayIndex(window);
    saveConfig(config);

    renderer.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}