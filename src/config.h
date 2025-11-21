#ifndef CONFIG_H
#define CONFIG_H

#include <SDL3/SDL.h>

#define MAX_KEYBINDING_STRING 2048
#define MAX_CONFIG_SECTIONS 16
#define MAX_CONFIG_ENTRIES 64
#define MAX_CONFIG_LINE 512
#define MAX_CONFIG_KEY 64
#define MAX_CONFIG_VALUE 256

struct Config {
    int display = 0;
    SDL_WindowFlags fullscreenMode = SDL_WINDOW_FULLSCREEN;
    char keybindings[MAX_KEYBINDING_STRING] = {0};
};

// Config manager for INI-style configuration files
class ConfigManager {
private:
    struct ConfigEntry {
        char section[MAX_CONFIG_KEY];
        char key[MAX_CONFIG_KEY];
        char value[MAX_CONFIG_VALUE];
    };
    
    ConfigEntry entries[MAX_CONFIG_ENTRIES];
    int entryCount;
    char configFilePath[1024];
    
    void trimWhitespace(char* str);
    int findEntry(const char* section, const char* key);
    
public:
    ConfigManager();
    
    // Load config from file
    bool load(const char* filename);
    
    // Save config to file
    bool save();
    
    // Read a string value from a section
    const char* getString(const char* section, const char* key, const char* defaultValue = "");
    
    // Read an integer value from a section
    int getInt(const char* section, const char* key, int defaultValue = 0);
    
    // Write a string value to a section
    void setString(const char* section, const char* key, const char* value);
    
    // Write an integer value to a section
    void setInt(const char* section, const char* key, int value);
};

// Legacy functions for backward compatibility
Config loadConfig();
void saveConfig(const Config& config);

#endif // CONFIG_H