#ifndef CONFIG_H
#define CONFIG_H

#include <SDL2/SDL.h>

struct Config {
    int display = 0;
    int fullscreenMode = SDL_WINDOW_FULLSCREEN_DESKTOP;
};

Config loadConfig();
void saveConfig(const Config& config);

#endif // CONFIG_H