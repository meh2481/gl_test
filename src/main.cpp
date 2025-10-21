#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "VulkanRenderer.h"
#include "config.h"

#define VERT_SHADER_ID 17179088570012488797ULL
#define FRAG_SHADER_ID 1358186205122297171ULL
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

    SDL_Window* window = SDL_CreateWindow("Shader Triangle", SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), SDL_WINDOWPOS_CENTERED_DISPLAY(config.display), displayMode.w, displayMode.h, SDL_WINDOW_VULKAN | (config.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    PakResource pakResource;
    pakResource.load(PAK_FILE);
    ResourceData vertShader = pakResource.getResource(VERT_SHADER_ID);
    ResourceData fragShader = pakResource.getResource(FRAG_SHADER_ID);

    VulkanRenderer renderer;
    try {
        renderer.initialize(window);
        renderer.createPipeline(0, vertShader, fragShader);
        renderer.setCurrentPipeline(0);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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
                    config.fullscreen = !config.fullscreen;
                    SDL_SetWindowFullscreen(window, config.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
                    config.display = SDL_GetWindowDisplayIndex(window);
                    saveConfig(config);
                }
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