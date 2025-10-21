#include "config.h"
#include <SDL2/SDL.h>

#define PREF_PATH_PREFIX "RetSphinxEngine"
#define PREF_PATH_APPLICATION "ShaderTriangle"

Config loadConfig() {
    Config config;
    char* prefPath = SDL_GetPrefPath(PREF_PATH_PREFIX, PREF_PATH_APPLICATION);
    if (prefPath) {
        char configFile[1024];
        SDL_snprintf(configFile, sizeof(configFile), "%sconfig.txt", prefPath);
        SDL_RWops* file = SDL_RWFromFile(configFile, "r");
        if (file) {
            char line[256];
            size_t len = 0;
            char ch;
            while (SDL_RWread(file, &ch, 1, 1) == 1) {
                if (ch == '\n' || len >= sizeof(line) - 1) {
                    line[len] = '\0';
                    if (SDL_strncmp(line, "display=", 8) == 0) {
                        config.display = SDL_atoi(line + 8);
                    } else if (SDL_strncmp(line, "fullscreen=", 11) == 0) {
                        config.fullscreen = SDL_atoi(line + 11) != 0;
                    }
                    len = 0;
                } else {
                    line[len++] = ch;
                }
            }
            // Process last line if no trailing newline
            if (len > 0) {
                line[len] = '\0';
                if (SDL_strncmp(line, "display=", 8) == 0) {
                    config.display = SDL_atoi(line + 8);
                } else if (SDL_strncmp(line, "fullscreen=", 11) == 0) {
                    config.fullscreen = SDL_atoi(line + 11) != 0;
                }
            }
            SDL_RWclose(file);
        }
        SDL_free(prefPath);
    }
    return config;
}

void saveConfig(const Config& config) {
    char* prefPath = SDL_GetPrefPath(PREF_PATH_PREFIX, PREF_PATH_APPLICATION);
    if (prefPath) {
        char configFile[1024];
        SDL_snprintf(configFile, sizeof(configFile), "%sconfig.txt", prefPath);
        SDL_RWops* file = SDL_RWFromFile(configFile, "w");
        if (file) {
            char line[256];
            SDL_snprintf(line, sizeof(line), "display=%d\n", config.display);
            SDL_RWwrite(file, line, 1, SDL_strlen(line));
            SDL_snprintf(line, sizeof(line), "fullscreen=%d\n", config.fullscreen ? 1 : 0);
            SDL_RWwrite(file, line, 1, SDL_strlen(line));
            SDL_RWclose(file);
        }
        SDL_free(prefPath);
    }
}