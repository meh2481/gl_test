# Shader Triangle

A C++ application using Vulkan rendering with Lua scripting and Box2D physics integration. Features scene management, physics simulation, and debug visualization.

## Features

- Vulkan-based rendering
- Scene management with Lua scripting
- Box2D physics engine with Lua bindings
- Physics debug visualization
- Hot-reloading of shaders and scenes (F5 in debug mode)
- Multithreading support with SDL threading
  - Async hot-reload (non-blocking shader compilation)
  - Async resource loading and decompression
  - Async physics simulation support
- OpenAL 3D audio with OPUS format support
  - Spatial audio positioning
  - Audio effects (lowpass, reverb)
  - Multiple simultaneous audio sources

## Dependencies

- SDL2
- Vulkan
- Box2D 3.1.0
- Lua 5.4
- LZ4
- OpenAL
- OpusFile (libopusfile)
- libpng
- libsquish
- CMake
- ImGui (libimgui-dev) - Debug builds only

### Dependency Installation (arch)

   ```
   yay -S imgui-full
   pacman -S sdl3 git-lfs
   git lfs install
   ```

## Building

1. Create a build directory:
   ```
   mkdir build
   cd build
   ```

2. Configure with CMake:
   ```
   cmake ..
   ```

3. Build:
   ```
   make
   ```

## Running

```
./shader_triangle
```

The application will open in fullscreen mode displaying procedural shader effects.

### Controls

- **ESC**: Exit the application or pop the current scene
- **ENTER**: Push the menu scene (from default scene)
- **P**: Push the physics demo scene
- **A**: Push the audio test scene
- **SPACE** (Physics scene): Apply force to objects
- **R** (Physics scene): Reset physics simulation
- **1-3** (Audio scene): Play sound at different positions
- **4-6** (Audio scene): Toggle audio effects
- **+/-** (Audio scene): Adjust global volume
- **ALT+ENTER**: Toggle fullscreen/windowed mode
- **F5** (Debug builds only): Hot-reload shaders and Lua scripts

### Features

- **Debug Console** (Debug builds only): In-application console window displays all C++ (std::cout) and Lua (print) output with scrollable history and clear button
- **Action-based keybinding system**: Flexible many-to-many mapping between keys and actions (see [docs/KEYBINDING_SYSTEM.md](docs/KEYBINDING_SYSTEM.md))
- **Lua-based scene management**: Scenes are defined in Lua scripts (`scenes/*.lua`) with support for scene stacking
- **Multiple shader effects**: Includes nebula, plasma, and cloud procedural shaders
- **3D Audio system**: OPUS audio support with spatial positioning and effects
- **Resource packing**: Assets are compressed into a `.pak` file using LZ4 compression
- **Configuration persistence**: Display and fullscreen settings are saved between sessions
- **Hot-reloading**: In debug builds, press F5 to reload shaders and scripts without restarting
