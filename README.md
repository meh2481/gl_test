# Shader Triangle

A C++ application using Vulkan rendering with Lua scripting and Box2D physics integration. Features scene management, physics simulation, particle effects, 3D objects with lighting, and debug tooling.

## Features

- Vulkan-based rendering with MSAA
- Scene management with Lua scripting and scene stacking
- Box2D 3.x physics engine with Lua bindings and debug visualization
- 2D object rendering with multi-light Phong shading (destructible box, lantern, lightsaber, rock)
- Sprite and animated sprite rendering
- Particle system with Lua-scriptable emitters
- Water polygon effect
- Procedural shader effects: nebula, cloud, fade
- Animation engine for sprite animations
- OpenAL 3D audio with OPUS format support
  - Spatial audio positioning
  - Audio effects (lowpass, reverb)
  - Multiple simultaneous audio sources
- Gamepad support with rumble (including DualSense trigger rumble)
- Camera pan (right-click drag) and zoom (scroll wheel)
- Mouse drag interactions exposed to Lua
- Hot-reloading of shaders and scenes (F5 in debug mode)
- Multithreading support with SDL threading
  - Async hot-reload (non-blocking shader compilation)
  - Async resource preloading and decompression
- Custom memory allocator system (small + large slab allocators)
- Custom no-STL containers (String, Vector, HashTable, HashSet, Stack)
- Precomputed trigonometry lookup table
- Log file written to platform preferences directory (`last_run.log`)

## Dependencies

- SDL3
- Vulkan
- Box2D 3.x
- Lua 5.4
- OpenAL
- OpusFile (libopusfile)
- libpng
- libsquish
- CMake
- ImGui - Debug builds only

### Dependency Installation (Arch)

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

The application opens in fullscreen mode on the primary display, showing the nebula procedural shader scene.

### Controls

- **ESC**: Pop the current scene (exits from the default scene)
- **ENTER**: Push the menu scene
- **P**: Push the physics demo scene
- **A**: Push the audio test scene
- **E**: Push the particle editor scene (debug builds only)
- **SPACE** (Physics scene): Apply force to objects
- **R** (Physics scene): Reset physics simulation
- **1-3** (Audio scene): Play sound at different positions
- **4-6** (Audio scene): Toggle audio effects
- **+/-** (Audio scene): Adjust global volume
- **Right-click + drag**: Pan camera
- **Scroll wheel**: Zoom camera
- **Left-click**: Drag interaction (scene-defined behaviour)
- **ALT+ENTER**: Toggle fullscreen/windowed mode
- **F5** (Debug builds only): Hot-reload shaders and Lua scripts

### Debug-only Features

- **Debug Console**: In-application ImGui window displaying all C++ (`SDL_Log`) and Lua (`print`) output with scrollable history and clear button
- **Particle Editor**: Visual editor for designing and previewing particle systems, with live preview synced to camera pan/zoom
- **Memory Allocator Window**: Live view of small and large allocator usage
- **Thread Profiler**: Per-thread busy/idle state timeline

### Other Features

- **Action-based keybinding system**: Flexible many-to-many mapping between keys/gamepad buttons and actions
- **Lua-based scene management**: Scenes are defined in Lua scripts (`res/scenes/*.lua`) with support for scene stacking
- **3D Audio system**: OPUS audio with spatial positioning and effects
- **Resource packing**: Assets (shaders, textures, audio, Lua scripts) are packed into `res.pak` using CMPR compression with optional texture atlas generation
- **Configuration persistence**: Display, fullscreen mode, GPU selection, present mode, and keybindings are saved between sessions
- **Hot-reloading**: In debug builds, F5 recompiles shaders and rebuilds the pak file in a background thread, then reloads the current scene without restarting
