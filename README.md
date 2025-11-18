# Shader Triangle

A C++ application that opens a fullscreen window using SDL2 and renders procedural shader effects using Vulkan. Features a Lua-based scene management system with multiple shader effects including nebula and plasma visualizations.

## Dependencies

- SDL2
- Vulkan SDK (includes glslc shader compiler)
- LZ4 compression library
- Lua (5.3 or later)
- CMake (3.10 or later)

Install on Ubuntu/Debian:

```
sudo apt-get update
sudo apt-get install cmake libsdl2-dev liblz4-dev liblua5.3-dev
```

For Vulkan SDK, download and install from [LunarG](https://vulkan.lunarg.com/sdk/home) or install the `vulkan-sdk` package if available in your distribution.

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
- **ALT+ENTER**: Toggle fullscreen/windowed mode
- **F5** (Debug builds only): Hot-reload shaders and Lua scripts

### Features

- **Lua-based scene management**: Scenes are defined in Lua scripts (`scenes/*.lua`) with support for scene stacking
- **Multiple shader effects**: Includes nebula, plasma, and cloud procedural shaders
- **Resource packing**: Assets are compressed into a `.pak` file using LZ4 compression
- **Configuration persistence**: Display and fullscreen settings are saved between sessions
- **Hot-reloading**: In debug builds, press F5 to reload shaders and scripts without restarting