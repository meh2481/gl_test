#pragma once

#include <SDL2/SDL.h>
#include <unordered_map>
#include <cassert>

// Define all possible actions in the application
enum Action {
    ACTION_EXIT = 0,
    ACTION_MENU,
    ACTION_PHYSICS_DEMO,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_HOTRELOAD,
    ACTION_APPLY_FORCE,
    ACTION_RESET_PHYSICS,
    ACTION_TOGGLE_DEBUG_DRAW,
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

// Maps keys to actions and vice versa (many-to-many)
class KeybindingManager {
public:
    KeybindingManager() {
        // Set up default keybindings
        bind(SDLK_ESCAPE, ACTION_EXIT);
        bind(SDLK_RETURN, ACTION_MENU);
        bind(SDLK_p, ACTION_PHYSICS_DEMO);
        bind(SDLK_SPACE, ACTION_APPLY_FORCE);
        bind(SDLK_r, ACTION_RESET_PHYSICS);
        bind(SDLK_d, ACTION_TOGGLE_DEBUG_DRAW);
        // ALT+ENTER is handled separately via modifier check
        // F5 is handled separately in main.cpp for debug builds
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

    // Get all actions bound to a key
    const ActionList& getActionsForKey(int keyCode) const {
        static const ActionList empty;
        auto it = keyToActions_.find(keyCode);
        if (it != keyToActions_.end()) {
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

    // Check if a key is bound to an action
    bool isKeyBoundToAction(int keyCode, Action action) const {
        const auto& actions = getActionsForKey(keyCode);
        return actions.contains(action);
    }

    // Clear all bindings
    void clearAllBindings() {
        keyToActions_.clear();
        actionToKeys_.clear();
    }

    // Get all bindings as a string for serialization
    // Format: "key1:action1,action2;key2:action3"
    void serializeBindings(char* buffer, int bufferSize) const {
        int offset = 0;
        bool firstKey = true;
        
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
        
        if (offset < bufferSize) {
            buffer[offset] = '\0';
        }
    }

    // Load bindings from a string
    // Format: "key1:action1,action2;key2:action3"
    void deserializeBindings(const char* data) {
        if (!data || *data == '\0') return;
        
        clearAllBindings();
        
        // Parse the string
        const char* ptr = data;
        while (*ptr) {
            // Parse key code
            int key = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                key = key * 10 + (*ptr - '0');
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
                    bind(key, static_cast<Action>(action));
                }
                
                if (*ptr == ',') ++ptr;
            }
            
            if (*ptr == ';') ++ptr;
        }
    }

private:
    std::unordered_map<int, ActionList> keyToActions_;
    std::unordered_map<Action, KeyList> actionToKeys_;
};
