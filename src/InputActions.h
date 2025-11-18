#pragma once

#include <SDL2/SDL.h>
#include <unordered_map>
#include <vector>
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
    ACTION_COUNT  // Always keep last
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
        // ALT+ENTER is handled separately via modifier check
        // F5 is handled separately in main.cpp for debug builds
    }

    // Bind a key to an action
    void bind(int keyCode, Action action) {
        keyToActions_[keyCode].push_back(action);
        actionToKeys_[action].push_back(keyCode);
    }

    // Unbind a key from an action
    void unbind(int keyCode, Action action) {
        // Remove action from key's action list
        auto& actions = keyToActions_[keyCode];
        for (size_t i = 0; i < actions.size(); ++i) {
            if (actions[i] == action) {
                actions[i] = actions[actions.size() - 1];
                actions.pop_back();
                break;
            }
        }

        // Remove key from action's key list
        auto& keys = actionToKeys_[action];
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == keyCode) {
                keys[i] = keys[keys.size() - 1];
                keys.pop_back();
                break;
            }
        }
    }

    // Get all actions bound to a key
    const std::vector<Action>& getActionsForKey(int keyCode) const {
        static const std::vector<Action> empty;
        auto it = keyToActions_.find(keyCode);
        if (it != keyToActions_.end()) {
            return it->second;
        }
        return empty;
    }

    // Get all keys bound to an action
    const std::vector<int>& getKeysForAction(Action action) const {
        static const std::vector<int> empty;
        auto it = actionToKeys_.find(action);
        if (it != actionToKeys_.end()) {
            return it->second;
        }
        return empty;
    }

    // Check if a key is bound to an action
    bool isKeyBoundToAction(int keyCode, Action action) const {
        const auto& actions = getActionsForKey(keyCode);
        for (size_t i = 0; i < actions.size(); ++i) {
            if (actions[i] == action) {
                return true;
            }
        }
        return false;
    }

private:
    std::unordered_map<int, std::vector<Action>> keyToActions_;
    std::unordered_map<Action, std::vector<int>> actionToKeys_;
};
