#ifndef CONFIG_H
#define CONFIG_H

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#define MAX_KEYBINDING_STRING 2048
#define MAX_CONFIG_SECTIONS 16
#define MAX_CONFIG_ENTRIES 64
#define MAX_CONFIG_LINE 512
#define MAX_CONFIG_KEY 64
#define MAX_CONFIG_VALUE 256

#define PREF_PATH_PREFIX "RetSphinxEngine"
#define PREF_PATH_APPLICATION "ShaderTriangle"
#define MAX_PREF_PATH 1024

// Maximum length of an ISO 639-1 language code (e.g. "en", "fr"), including null terminator.
// Must match DIALOGUE_LANG_CODE_LEN in ResourceTypes.h.
#define MAX_LANGUAGE_CODE 8

struct Config {
    int display = 0;
    SDL_WindowFlags fullscreenMode = SDL_WINDOW_FULLSCREEN;
    char keybindings[MAX_KEYBINDING_STRING] = {0};
    int gpuIndex = -1;  // -1 means auto-select, otherwise use specified GPU index
    // Vulkan present mode: "fifo" (vsync), "mailbox" (triple-buf), "immediate" (uncapped), "fifo_relaxed"
    // An empty string means unspecified; the renderer will default to "fifo"
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    // Log level: verbose, debug, info, warn, error, critical
    // Defaults to "debug" in debug builds and "error" in release builds
    SDL_LogPriority logLevel =
#ifdef DEBUG
        SDL_LOG_PRIORITY_DEBUG;
#else
        SDL_LOG_PRIORITY_ERROR;
#endif
    // ISO 639-1 language code used for dialogue text selection (e.g. "en", "fr", "es").
    char language[MAX_LANGUAGE_CODE] = "en";
};

// Config manager for INI-style configuration files
class ConfigManager {
private:
    struct ConfigEntry {
        char section[MAX_CONFIG_KEY];
        char key[MAX_CONFIG_KEY];
        char value[MAX_CONFIG_VALUE];
    };

    struct ConfigComment {
        char section[MAX_CONFIG_KEY];
        char key[MAX_CONFIG_KEY];
        char comment[MAX_CONFIG_LINE];
    };

    ConfigEntry entries[MAX_CONFIG_ENTRIES];
    int entryCount;
    ConfigComment comments[MAX_CONFIG_ENTRIES];
    int commentCount;
    char configFilePath[1024];

    void trimWhitespace(char* str);
    int findEntry(const char* section, const char* key);
    int findComment(const char* section, const char* key);

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

    // Register a comment to be written before a specific section/key entry
    void setKeyComment(const char* section, const char* key, const char* comment);
};

// Legacy functions for backward compatibility
Config loadConfig();
void saveConfig(const Config& config);

// Global current language used by dialogue loading at runtime.
extern char gCurrentLanguage[MAX_LANGUAGE_CODE];
void setCurrentLanguage(const char* languageCode);
const char* getCurrentLanguage();

// Helper function to build a path in the config directory
// Returns true if successful, false otherwise
// pathBuffer should be at least MAX_PREF_PATH bytes
bool getPrefFilePath(char* pathBuffer, Uint64 bufferSize, const char* filename);

// Parse a present mode string from config
// Valid values: "fifo" (vsync), "mailbox" (triple-buffered), "immediate" (uncapped), "fifo_relaxed"
// Returns the string as-is if valid, or empty string if invalid
const char* parsePresentModeString(const char* modeString);

// Parse a present mode string and convert to Vulkan enum
// Valid values: "fifo" (vsync), "mailbox" (triple-buffered), "immediate" (uncapped), "fifo_relaxed"
// Invalid or empty strings default to VK_PRESENT_MODE_FIFO_KHR
VkPresentModeKHR parsePresentModeEnum(const char* modeString);

// Convert Vulkan present mode enum to string for human readable display
const char* getActivePresentModeString(VkPresentModeKHR activePresentMode);

// Parse a log level string and convert to SDL_LogPriority enum
// Valid values: "verbose", "debug", "info", "warn", "error", "critical"
// Invalid or empty strings default to DEBUG in debug builds, ERROR in release builds
SDL_LogPriority parseLogLevelEnum(const char* levelString);

// Convert SDL_LogPriority enum to lowercase string for config storage
const char* getLogLevelString(SDL_LogPriority priority);

#endif // CONFIG_H