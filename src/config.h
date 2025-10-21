#ifndef CONFIG_H
#define CONFIG_H

#include <SDL2/SDL.h>

struct Config {
    int display = 0;
    bool fullscreen = true;
};

Config loadConfig();
void saveConfig(const Config& config);

#endif // CONFIG_H