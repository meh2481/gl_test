#include "config.h"
#include <SDL3/SDL.h>
#include <cassert>

#define PREF_PATH_PREFIX "RetSphinxEngine"
#define PREF_PATH_APPLICATION "ShaderTriangle"

// ConfigManager implementation

ConfigManager::ConfigManager() : entryCount(0) {
    configFilePath[0] = '\0';
    for (int i = 0; i < MAX_CONFIG_ENTRIES; i++) {
        entries[i].section[0] = '\0';
        entries[i].key[0] = '\0';
        entries[i].value[0] = '\0';
    }
}

void ConfigManager::trimWhitespace(char* str) {
    if (!str || str[0] == '\0') return;

    // Trim leading whitespace
    char* start = str;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }

    // Trim trailing whitespace
    char* end = start + SDL_strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        end--;
    }
    *(end + 1) = '\0';

    // Move trimmed string to beginning if needed
    if (start != str) {
        SDL_memmove(str, start, SDL_strlen(start) + 1);
    }
}

int ConfigManager::findEntry(const char* section, const char* key) {
    for (int i = 0; i < entryCount; i++) {
        if (SDL_strcmp(entries[i].section, section) == 0 &&
            SDL_strcmp(entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

bool ConfigManager::load(const char* filename) {
    SDL_strlcpy(configFilePath, filename, sizeof(configFilePath));
    entryCount = 0;

    SDL_IOStream* file = SDL_IOFromFile(filename, "r");
    if (!file) {
        return false;
    }

    char line[MAX_CONFIG_LINE];
    char currentSection[MAX_CONFIG_KEY] = "";
    size_t len = 0;
    char ch;

    while (SDL_ReadIO(file, &ch, 1) == 1) {
        if (ch == '\n' || ch == '\r' || len >= sizeof(line) - 1) {
            if (len > 0) {
                line[len] = '\0';
                trimWhitespace(line);

                // Skip empty lines and comments
                if (line[0] != '\0' && line[0] != '#' && line[0] != ';') {
                    // Check for section header [Section]
                    if (line[0] == '[') {
                        char* endBracket = SDL_strchr(line, ']');
                        if (endBracket) {
                            *endBracket = '\0';
                            SDL_strlcpy(currentSection, line + 1, sizeof(currentSection));
                            trimWhitespace(currentSection);
                        }
                    } else {
                        // Parse key=value
                        char* equals = SDL_strchr(line, '=');
                        if (equals && entryCount < MAX_CONFIG_ENTRIES) {
                            *equals = '\0';
                            char* keyStr = line;
                            char* valueStr = equals + 1;

                            trimWhitespace(keyStr);
                            trimWhitespace(valueStr);

                            if (keyStr[0] != '\0') {
                                SDL_strlcpy(entries[entryCount].section, currentSection, MAX_CONFIG_KEY);
                                SDL_strlcpy(entries[entryCount].key, keyStr, MAX_CONFIG_KEY);
                                SDL_strlcpy(entries[entryCount].value, valueStr, MAX_CONFIG_VALUE);
                                entryCount++;
                            }
                        }
                    }
                }
                len = 0;
            }
        } else {
            line[len++] = ch;
        }
    }

    // Process last line if no trailing newline
    if (len > 0) {
        line[len] = '\0';
        trimWhitespace(line);

        if (line[0] != '\0' && line[0] != '#' && line[0] != ';') {
            if (line[0] == '[') {
                char* endBracket = SDL_strchr(line, ']');
                if (endBracket) {
                    *endBracket = '\0';
                    SDL_strlcpy(currentSection, line + 1, sizeof(currentSection));
                    trimWhitespace(currentSection);
                }
            } else {
                char* equals = SDL_strchr(line, '=');
                if (equals && entryCount < MAX_CONFIG_ENTRIES) {
                    *equals = '\0';
                    char* keyStr = line;
                    char* valueStr = equals + 1;

                    trimWhitespace(keyStr);
                    trimWhitespace(valueStr);

                    if (keyStr[0] != '\0') {
                        SDL_strlcpy(entries[entryCount].section, currentSection, MAX_CONFIG_KEY);
                        SDL_strlcpy(entries[entryCount].key, keyStr, MAX_CONFIG_KEY);
                        SDL_strlcpy(entries[entryCount].value, valueStr, MAX_CONFIG_VALUE);
                        entryCount++;
                    }
                }
            }
        }
    }

    SDL_CloseIO(file);
    return true;
}

bool ConfigManager::save() {
    assert(configFilePath[0] != '\0');

    SDL_IOStream* file = SDL_IOFromFile(configFilePath, "w");
    if (!file) {
        return false;
    }

    // Group entries by section and write them
    char currentSection[MAX_CONFIG_KEY] = "";
    bool firstSection = true;

    for (int i = 0; i < entryCount; i++) {
        // Write section header if section changed
        if (SDL_strcmp(currentSection, entries[i].section) != 0) {
            SDL_strlcpy(currentSection, entries[i].section, sizeof(currentSection));

            // Add blank line before new section (except first)
            if (!firstSection) {
                const char* newline = "\n";
                SDL_WriteIO(file, newline, SDL_strlen(newline));
            }
            firstSection = false;

            // Write section header if not empty
            if (currentSection[0] != '\0') {
                char sectionHeader[MAX_CONFIG_LINE];
                SDL_snprintf(sectionHeader, sizeof(sectionHeader), "[%s]\n", currentSection);
                SDL_WriteIO(file, sectionHeader, SDL_strlen(sectionHeader));
            }
        }

        // Write key=value
        char line[MAX_CONFIG_LINE];
        SDL_snprintf(line, sizeof(line), "%s = %s\n", entries[i].key, entries[i].value);
        SDL_WriteIO(file, line, SDL_strlen(line));
    }

    SDL_CloseIO(file);
    return true;
}

const char* ConfigManager::getString(const char* section, const char* key, const char* defaultValue) {
    int index = findEntry(section, key);
    if (index >= 0) {
        return entries[index].value;
    }
    return defaultValue;
}

int ConfigManager::getInt(const char* section, const char* key, int defaultValue) {
    int index = findEntry(section, key);
    if (index >= 0) {
        return SDL_atoi(entries[index].value);
    }
    return defaultValue;
}

void ConfigManager::setString(const char* section, const char* key, const char* value) {
    int index = findEntry(section, key);
    if (index >= 0) {
        // Update existing entry
        SDL_strlcpy(entries[index].value, value, MAX_CONFIG_VALUE);
    } else {
        // Add new entry
        assert(entryCount < MAX_CONFIG_ENTRIES);
        SDL_strlcpy(entries[entryCount].section, section, MAX_CONFIG_KEY);
        SDL_strlcpy(entries[entryCount].key, key, MAX_CONFIG_KEY);
        SDL_strlcpy(entries[entryCount].value, value, MAX_CONFIG_VALUE);
        entryCount++;
    }
}

void ConfigManager::setInt(const char* section, const char* key, int value) {
    char valueStr[32];
    SDL_snprintf(valueStr, sizeof(valueStr), "%d", value);
    setString(section, key, valueStr);
}

// Legacy functions for backward compatibility

Config loadConfig() {
    Config config;
    char* prefPath = SDL_GetPrefPath(PREF_PATH_PREFIX, PREF_PATH_APPLICATION);
    if (prefPath) {
        char configFile[1024];
        SDL_snprintf(configFile, sizeof(configFile), "%sconfig.ini", prefPath);

        ConfigManager manager;
        if (manager.load(configFile)) {
            config.display = manager.getInt("Display", "display", 0);
            config.fullscreenMode = (SDL_WindowFlags)manager.getInt("Display", "fullscreen", SDL_WINDOW_FULLSCREEN);
            const char* keybindings = manager.getString("Input", "keybindings", "");
            SDL_strlcpy(config.keybindings, keybindings, MAX_KEYBINDING_STRING);
            config.gpuIndex = manager.getInt("Graphics", "gpu_index", -1);
        }

        SDL_free(prefPath);
    }
    return config;
}

void saveConfig(const Config& config) {
    char* prefPath = SDL_GetPrefPath(PREF_PATH_PREFIX, PREF_PATH_APPLICATION);
    if (prefPath) {
        char configFile[1024];
        SDL_snprintf(configFile, sizeof(configFile), "%sconfig.ini", prefPath);

        ConfigManager manager;
        manager.load(configFile);  // Load existing config (or initialize path if file doesn't exist)
        manager.setInt("Display", "display", config.display);
        manager.setInt("Display", "fullscreen", config.fullscreenMode);
        if (config.keybindings[0] != '\0') {
            manager.setString("Input", "keybindings", config.keybindings);
        }
        manager.setInt("Graphics", "gpu_index", config.gpuIndex);
        manager.save();

        SDL_free(prefPath);
    }
}