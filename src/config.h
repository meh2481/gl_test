#ifndef CONFIG_H
#define CONFIG_H

#include <SDL2/SDL.h>

#define MAX_KEYBINDING_STRING 2048

struct Config {
    int display = 0;
    int fullscreenMode = SDL_WINDOW_FULLSCREEN_DESKTOP;
    char keybindings[MAX_KEYBINDING_STRING] = {0};
};

Config loadConfig();
void saveConfig(const Config& config);

#endif // CONFIG_H