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
#include "LuaInterface.h"
#include "config.h"

#define LUA_SCRIPT_ID 14669932163325785351ULL
#define PAK_FILE "res.pak"

inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    Config config = loadConfig();

    SDL_DisplayMode displayMode;
    if (SDL_GetDesktopDisplayMode(config.display, &displayMode) != 0) {
        std::cerr << "SDL_GetDesktopDisplayMode Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Shader Triangle", SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), displayMode.w, displayMode.h, SDL_WINDOW_VULKAN | config.fullscreenMode);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    PakResource pakResource;
    pakResource.load(PAK_FILE);

    VulkanRenderer renderer;
    LuaInterface luaInterface(pakResource, renderer);
    renderer.initialize(window);

    // Load scene from Lua
    ResourceData luaScript = pakResource.getResource(LUA_SCRIPT_ID);
    assert(luaInterface.executeScript(luaScript));

    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
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
                if (event.key.keysym.sym == SDLK_F5) {
                    std::cout << "Hot-reloading resources..." << std::endl;
                    // Rebuild shaders and pak file using make
                    int result = system("make shaders && make res_pak");
                    if (result == 0) {
                        // Reload pak
                        pakResource.load(PAK_FILE);
                        // Reload scene from Lua
                        ResourceData luaScript = pakResource.getResource(LUA_SCRIPT_ID);
                        if (luaInterface.executeScript(luaScript)) {
                            std::cout << "Resources reloaded successfully." << std::endl;
                        } else {
                            std::cerr << "Failed to execute Lua script during hot-reload." << std::endl;
                        }
                    } else {
                        std::cerr << "Failed to rebuild resources." << std::endl;
                    }
                }
#endif
            }
        }

        float time = SDL_GetTicks() / 1000.0f;
        renderer.render(time);
    }

    // Save current display to config
    config.display = SDL_GetWindowDisplayIndex(window);
    saveConfig(config);

    renderer.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}