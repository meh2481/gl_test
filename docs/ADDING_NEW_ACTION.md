# Example: Adding a New Action

This guide demonstrates how to add a new action to the keybinding system.

## Scenario

We want to add a "TOGGLE_DEBUG" action that can be triggered by pressing the 'D' key.

## Step 1: Define the Action

Edit `src/InputActions.h` and add the new action to the `Action` enum:

```cpp
enum Action {
    ACTION_EXIT = 0,
    ACTION_MENU,
    ACTION_PHYSICS_DEMO,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_HOTRELOAD,
    ACTION_APPLY_FORCE,
    ACTION_RESET_PHYSICS,
    ACTION_TOGGLE_DEBUG,  // <-- Add new action here
    ACTION_COUNT  // Always keep last
};
```

## Step 2: Register the Action Constant in Lua

Edit `src/LuaInterface.cpp` and add the action to two places:

### In `registerFunctions()` (around line 455):

```cpp
    // Register Action constants
    lua_pushinteger(luaState_, ACTION_EXIT);
    lua_setglobal(luaState_, "ACTION_EXIT");
    lua_pushinteger(luaState_, ACTION_MENU);
    lua_setglobal(luaState_, "ACTION_MENU");
    // ... other actions ...
    lua_pushinteger(luaState_, ACTION_TOGGLE_DEBUG);  // <-- Add here
    lua_setglobal(luaState_, "ACTION_TOGGLE_DEBUG");
}
```

### In `loadScene()` (around line 82):

```cpp
    // Copy Action constants
    const char* actionConstants[] = {
        "ACTION_EXIT", "ACTION_MENU", "ACTION_PHYSICS_DEMO", "ACTION_TOGGLE_FULLSCREEN",
        "ACTION_HOTRELOAD", "ACTION_APPLY_FORCE", "ACTION_RESET_PHYSICS",
        "ACTION_TOGGLE_DEBUG",  // <-- Add here
        nullptr
    };
```

## Step 3: Add Default Keybinding

Edit `src/InputActions.h` and add the default keybinding in the `KeybindingManager` constructor:

```cpp
    KeybindingManager() {
        // Set up default keybindings
        bind(SDLK_ESCAPE, ACTION_EXIT);
        bind(SDLK_RETURN, ACTION_MENU);
        bind(SDLK_p, ACTION_PHYSICS_DEMO);
        bind(SDLK_SPACE, ACTION_APPLY_FORCE);
        bind(SDLK_r, ACTION_RESET_PHYSICS);
        bind(SDLK_d, ACTION_TOGGLE_DEBUG);  // <-- Add here
    }
```

## Step 4: Implement the Action Handler in Lua

Edit your scene file (e.g., `scenes/default.lua`) and handle the new action:

```lua
-- Handle actions (new action-based system)
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_MENU then
        pushScene("menu.lua")
    end
    if action == ACTION_PHYSICS_DEMO then
        pushScene("physics.lua")
    end
    -- Add handler for the new action
    if action == ACTION_TOGGLE_DEBUG then
        print("Debug mode toggled!")
        -- Add your debug toggle logic here
    end
end
```

## Step 5: (Optional) Update Documentation

Update `README.md` to document the new control:

```markdown
### Controls

- **ESC**: Exit the application or pop the current scene
- **ENTER**: Push the menu scene (from default scene)
- **P**: Push the physics demo scene
- **D**: Toggle debug mode  <-- Add documentation
- ...
```

## Alternative: Adding Multiple Keys to an Action

You can bind multiple keys to the same action:

```cpp
    KeybindingManager() {
        // ... existing bindings ...
        
        // Bind both 'D' and 'F3' to toggle debug
        bind(SDLK_d, ACTION_TOGGLE_DEBUG);
        bind(SDLK_F3, ACTION_TOGGLE_DEBUG);
    }
```

Now either the 'D' key or 'F3' will trigger the ACTION_TOGGLE_DEBUG action.

## Complete Example

See the implementation of `ACTION_APPLY_FORCE` in the codebase for a complete working example:

1. Defined in `src/InputActions.h` enum
2. Registered in `src/LuaInterface.cpp`
3. Bound to SPACE key in `KeybindingManager`
4. Implemented in `scenes/physics.lua`
