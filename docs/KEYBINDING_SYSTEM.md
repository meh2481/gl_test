# Keybinding System

## Overview

The application uses an action-based keybinding system that allows many-to-many mapping between keys and actions. This provides flexibility in configuration and maintains backwards compatibility with the original key-based system.

## Architecture

### Actions

Actions are defined in `src/InputActions.h` as an enum:

```cpp
enum Action {
    ACTION_EXIT = 0,
    ACTION_MENU,
    ACTION_PHYSICS_DEMO,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_HOTRELOAD,
    ACTION_APPLY_FORCE,
    ACTION_RESET_PHYSICS,
    ACTION_COUNT
};
```

### KeybindingManager

The `KeybindingManager` class manages the mapping between keys and actions:

- **Many-to-many mapping**: Multiple keys can trigger the same action, and a single key can trigger multiple actions
- **C-style arrays**: Uses fixed-size arrays instead of STL vectors for better performance
- **Default bindings**: Set up in the constructor

Default keybindings:
- `ESC` → ACTION_EXIT
- `RETURN` → ACTION_MENU
- `P` → ACTION_PHYSICS_DEMO
- `SPACE` → ACTION_APPLY_FORCE
- `R` → ACTION_RESET_PHYSICS

### Lua Integration

Actions are exposed to Lua scenes through the `onAction(action)` callback function. Action constants are registered as global variables:

```lua
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_MENU then
        pushScene("menu.lua")
    end
end
```

## Backwards Compatibility

The system maintains full backwards compatibility with the original key-based handlers:

1. Raw key codes are still passed to `onKeyDown(keyCode)` and `onKeyUp(keyCode)`
2. Scenes can implement either or both callback types
3. Existing scenes continue to work without modification

## Usage Example

### Adding a New Action

1. Add the action to the `Action` enum in `src/InputActions.h`
2. Register the action constant in Lua (`src/LuaInterface.cpp`)
3. Add default keybinding in `KeybindingManager` constructor
4. Implement the action handler in Lua scenes

### Runtime Keybinding Changes

The `KeybindingManager` provides methods to modify keybindings at runtime:

```cpp
// Bind a new key to an action
keybindings.bind(SDLK_q, ACTION_EXIT);

// Unbind a key from an action
keybindings.unbind(SDLK_ESCAPE, ACTION_EXIT);

// Check if a key is bound to an action
bool isBound = keybindings.isKeyBoundToAction(SDLK_ESCAPE, ACTION_EXIT);
```

## Design Decisions

### C-Style Arrays vs STL Vectors

The implementation uses C-style arrays with fixed maximum sizes instead of `std::vector`:

- Follows project coding guidelines
- Provides predictable memory usage
- Avoids dynamic allocations during gameplay

Limits:
- Maximum 8 bindings per key (`MAX_BINDINGS_PER_KEY`)
- Maximum 8 bindings per action (`MAX_BINDINGS_PER_ACTION`)

These limits can be increased if needed by modifying the constants in `InputActions.h`.

### Action-First vs Key-First

The system prioritizes actions over raw keys:

1. Key press is detected
2. Raw key event is sent to Lua (backwards compatibility)
3. Actions bound to the key are resolved
4. Each action is dispatched to Lua

This allows scenes to respond to either individual keys or higher-level actions.

## Future Enhancements

Possible improvements to the system:

1. **Configuration file**: Load keybindings from a config file
2. **In-game rebinding**: Allow users to change keybindings through a settings menu
3. **Key combinations**: Support modifier keys (Shift, Ctrl, Alt) as part of the binding
4. **Action priorities**: Define execution order when multiple actions are bound to one key
5. **Save/load**: Persist custom keybindings between sessions
