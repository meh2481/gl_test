#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include "VulkanRenderer.h"

#define VERT_SHADER_ID 17179088570012488797ULL
#define FRAG_SHADER_ID 1358186205122297171ULL

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

    SDL_DisplayMode displayMode;
    if (SDL_GetDesktopDisplayMode(0, &displayMode) != 0) {
        std::cerr << "SDL_GetDesktopDisplayMode Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Shader Triangle", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, displayMode.w, displayMode.h, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    PakResource pakResource;
    pakResource.load("res.pak");
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
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        float time = SDL_GetTicks() / 1000.0f;
        renderer.render(time);
    }

    renderer.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}