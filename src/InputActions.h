#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>
#include <cassert>

// Define all possible actions in the application
enum Action {
    ACTION_EXIT = 0,
    ACTION_MENU,
    ACTION_PHYSICS_DEMO,
    ACTION_AUDIO_TEST,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_HOTRELOAD,
    ACTION_APPLY_FORCE,
    ACTION_RESET_PHYSICS,
    ACTION_TOGGLE_DEBUG_DRAW,
    ACTION_DRAG_START,
    ACTION_DRAG_END,
    ACTION_COUNT  // Always keep last
};

#define MAX_BINDINGS_PER_KEY 8
#define MAX_BINDINGS_PER_ACTION 8

// Simple array-based list structure
struct ActionList {
    Action actions[MAX_BINDINGS_PER_KEY];
    int count;

    ActionList() : count(0) {}

    void add(Action action) {
        assert(count < MAX_BINDINGS_PER_KEY);
        actions[count++] = action;
    }

    void remove(Action action) {
        for (int i = 0; i < count; ++i) {
            if (actions[i] == action) {
                actions[i] = actions[count - 1];
                --count;
                break;
            }
        }
    }

    bool contains(Action action) const {
        for (int i = 0; i < count; ++i) {
            if (actions[i] == action) {
                return true;
            }
        }
        return false;
    }
};

struct KeyList {
    int keys[MAX_BINDINGS_PER_ACTION];
    int count;

    KeyList() : count(0) {}

    void add(int key) {
        assert(count < MAX_BINDINGS_PER_ACTION);
        keys[count++] = key;
    }

    void remove(int key) {
        for (int i = 0; i < count; ++i) {
            if (keys[i] == key) {
                keys[i] = keys[count - 1];
                --count;
                break;
            }
        }
    }
};

struct GamepadButtonList {
    int buttons[MAX_BINDINGS_PER_ACTION];
    int count;

    GamepadButtonList() : count(0) {}

    void add(int button) {
        assert(count < MAX_BINDINGS_PER_ACTION);
        buttons[count++] = button;
    }

    void remove(int button) {
        for (int i = 0; i < count; ++i) {
            if (buttons[i] == button) {
                buttons[i] = buttons[count - 1];
                --count;
                break;
            }
        }
    }
};

// Maps keys/gamepad buttons to actions and vice versa (many-to-many)
class KeybindingManager {
public:
    KeybindingManager() {
        // Set up default keybindings
        bind(SDLK_ESCAPE, ACTION_EXIT);
        bind(SDLK_RETURN, ACTION_MENU);
        bind(SDLK_P, ACTION_PHYSICS_DEMO);
        bind(SDLK_A, ACTION_AUDIO_TEST);
        bind(SDLK_SPACE, ACTION_APPLY_FORCE);
        bind(SDLK_R, ACTION_RESET_PHYSICS);
        bind(SDLK_D, ACTION_TOGGLE_DEBUG_DRAW);
        // ALT+ENTER is handled separately via modifier check
        // F5 is handled separately in main.cpp for debug builds

        // Set up default gamepad bindings
        bindGamepad(SDL_GAMEPAD_BUTTON_SOUTH, ACTION_EXIT);
        bindGamepad(SDL_GAMEPAD_BUTTON_START, ACTION_MENU);
        bindGamepad(SDL_GAMEPAD_BUTTON_NORTH, ACTION_PHYSICS_DEMO);
        bindGamepad(SDL_GAMEPAD_BUTTON_WEST, ACTION_AUDIO_TEST);
        bindGamepad(SDL_GAMEPAD_BUTTON_EAST, ACTION_APPLY_FORCE);
        bindGamepad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ACTION_RESET_PHYSICS);
        bindGamepad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ACTION_TOGGLE_DEBUG_DRAW);
    }

    // Bind a key to an action
    void bind(int keyCode, Action action) {
        keyToActions_[keyCode].add(action);
        actionToKeys_[action].add(keyCode);
    }

    // Unbind a key from an action
    void unbind(int keyCode, Action action) {
        keyToActions_[keyCode].remove(action);
        actionToKeys_[action].remove(keyCode);
    }

    // Bind a gamepad button to an action
    void bindGamepad(int buttonCode, Action action) {
        gamepadButtonToActions_[buttonCode].add(action);
        actionToGamepadButtons_[action].add(buttonCode);
    }

    // Unbind a gamepad button from an action
    void unbindGamepad(int buttonCode, Action action) {
        gamepadButtonToActions_[buttonCode].remove(action);
        actionToGamepadButtons_[action].remove(buttonCode);
    }

    // Get all actions bound to a key
    const ActionList& getActionsForKey(int keyCode) const {
        static const ActionList empty;
        auto it = keyToActions_.find(keyCode);
        if (it != keyToActions_.end()) {
            return it->second;
        }
        return empty;
    }

    // Get all actions bound to a gamepad button
    const ActionList& getActionsForGamepadButton(int buttonCode) const {
        static const ActionList empty;
        auto it = gamepadButtonToActions_.find(buttonCode);
        if (it != gamepadButtonToActions_.end()) {
            return it->second;
        }
        return empty;
    }

    // Get all keys bound to an action
    const KeyList& getKeysForAction(Action action) const {
        static const KeyList empty;
        auto it = actionToKeys_.find(action);
        if (it != actionToKeys_.end()) {
            return it->second;
        }
        return empty;
    }

    // Get all gamepad buttons bound to an action
    const GamepadButtonList& getGamepadButtonsForAction(Action action) const {
        static const GamepadButtonList empty;
        auto it = actionToGamepadButtons_.find(action);
        if (it != actionToGamepadButtons_.end()) {
            return it->second;
        }
        return empty;
    }

    // Check if a key is bound to an action
    bool isKeyBoundToAction(int keyCode, Action action) const {
        const auto& actions = getActionsForKey(keyCode);
        return actions.contains(action);
    }

    // Check if a gamepad button is bound to an action
    bool isGamepadButtonBoundToAction(int buttonCode, Action action) const {
        const auto& actions = getActionsForGamepadButton(buttonCode);
        return actions.contains(action);
    }

    // Clear all bindings
    void clearAllBindings() {
        keyToActions_.clear();
        actionToKeys_.clear();
        gamepadButtonToActions_.clear();
        actionToGamepadButtons_.clear();
    }

    // Get all bindings as a string for serialization
    // Format: "key1:action1,action2;key2:action3|button1:action1;button2:action2"
    void serializeBindings(char* buffer, int bufferSize) const {
        int offset = 0;
        bool firstKey = true;

        // Serialize keyboard bindings
        for (const auto& pair : keyToActions_) {
            if (pair.second.count == 0) continue;

            if (!firstKey && offset < bufferSize - 1) {
                buffer[offset++] = ';';
            }
            firstKey = false;

            // Write key code
            offset += SDL_snprintf(buffer + offset, bufferSize - offset, "%d:", pair.first);

            // Write actions
            for (int i = 0; i < pair.second.count; ++i) {
                if (i > 0 && offset < bufferSize - 1) {
                    buffer[offset++] = ',';
                }
                offset += SDL_snprintf(buffer + offset, bufferSize - offset, "%d", pair.second.actions[i]);
            }
        }

        // Add separator between keyboard and gamepad bindings
        if (offset < bufferSize - 1) {
            buffer[offset++] = '|';
        }

        // Serialize gamepad bindings
        bool firstButton = true;
        for (const auto& pair : gamepadButtonToActions_) {
            if (pair.second.count == 0) continue;

            if (!firstButton && offset < bufferSize - 1) {
                buffer[offset++] = ';';
            }
            firstButton = false;

            // Write button code
            offset += SDL_snprintf(buffer + offset, bufferSize - offset, "%d:", pair.first);

            // Write actions
            for (int i = 0; i < pair.second.count; ++i) {
                if (i > 0 && offset < bufferSize - 1) {
                    buffer[offset++] = ',';
                }
                offset += SDL_snprintf(buffer + offset, bufferSize - offset, "%d", pair.second.actions[i]);
            }
        }

        if (offset < bufferSize) {
            buffer[offset] = '\0';
        }
    }

    // Load bindings from a string
    // Format: "key1:action1,action2;key2:action3|button1:action1;button2:action2"
    void deserializeBindings(const char* data) {
        if (!data || *data == '\0') return;

#ifdef DEBUG
        // In debug mode, preserve default bindings for actions not in config
        // Save default bindings for each action
        KeyList defaultKeyBindings[ACTION_COUNT];
        GamepadButtonList defaultGamepadBindings[ACTION_COUNT];
        for (int i = 0; i < ACTION_COUNT; ++i) {
            const KeyList& keys = getKeysForAction(static_cast<Action>(i));
            for (int j = 0; j < keys.count; ++j) {
                defaultKeyBindings[i].add(keys.keys[j]);
            }
            const GamepadButtonList& buttons = getGamepadButtonsForAction(static_cast<Action>(i));
            for (int j = 0; j < buttons.count; ++j) {
                defaultGamepadBindings[i].add(buttons.buttons[j]);
            }
        }
#endif

        clearAllBindings();

        // Find separator between keyboard and gamepad bindings
        const char* separator = SDL_strchr(data, '|');
        const char* keyboardData = data;
        const char* gamepadData = separator ? separator + 1 : nullptr;

        // Parse keyboard bindings
        const char* ptr = keyboardData;
        while (*ptr && ptr != separator) {
            // Parse key code
            int key = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                key = key * 10 + (*ptr - '0');
                ++ptr;
            }

            if (*ptr != ':') break;
            ++ptr;

            // Parse actions
            while (*ptr && *ptr != ';' && *ptr != '|') {
                int action = 0;
                while (*ptr >= '0' && *ptr <= '9') {
                    action = action * 10 + (*ptr - '0');
                    ++ptr;
                }

                if (action < ACTION_COUNT) {
                    bind(key, static_cast<Action>(action));
                }

                if (*ptr == ',') ++ptr;
            }

            if (*ptr == ';') ++ptr;
            if (*ptr == '|') break;
        }

        // Parse gamepad bindings if present
        if (gamepadData && *gamepadData) {
            ptr = gamepadData;
            while (*ptr) {
                // Parse button code
                int button = 0;
                while (*ptr >= '0' && *ptr <= '9') {
                    button = button * 10 + (*ptr - '0');
                    ++ptr;
                }

                if (*ptr != ':') break;
                ++ptr;

                // Parse actions
                while (*ptr && *ptr != ';') {
                    int action = 0;
                    while (*ptr >= '0' && *ptr <= '9') {
                        action = action * 10 + (*ptr - '0');
                        ++ptr;
                    }

                    if (action < ACTION_COUNT) {
                        bindGamepad(button, static_cast<Action>(action));
                    }

                    if (*ptr == ',') ++ptr;
                }

                if (*ptr == ';') ++ptr;
            }
        }

#ifdef DEBUG
        // Restore default bindings for actions that weren't in the config
        for (int i = 0; i < ACTION_COUNT; ++i) {
            Action action = static_cast<Action>(i);
            const KeyList& currentKeys = getKeysForAction(action);

            // If this action has no key bindings after loading config, restore defaults
            if (currentKeys.count == 0) {
                for (int j = 0; j < defaultKeyBindings[i].count; ++j) {
                    bind(defaultKeyBindings[i].keys[j], action);
                }
            }

            const GamepadButtonList& currentButtons = getGamepadButtonsForAction(action);

            // If this action has no gamepad bindings after loading config, restore defaults
            if (currentButtons.count == 0) {
                for (int j = 0; j < defaultGamepadBindings[i].count; ++j) {
                    bindGamepad(defaultGamepadBindings[i].buttons[j], action);
                }
            }
        }
#endif
    }

private:
    std::unordered_map<int, ActionList> keyToActions_;
    std::unordered_map<Action, KeyList> actionToKeys_;
    std::unordered_map<int, ActionList> gamepadButtonToActions_;
    std::unordered_map<Action, GamepadButtonList> actionToGamepadButtons_;
};
