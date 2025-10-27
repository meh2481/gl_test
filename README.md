# Shader Triangle

A C++ application using Vulkan rendering with Lua scripting and Box2D physics integration. Features scene management, physics simulation, and debug visualization.

## Features

- Vulkan-based rendering
- Scene management with Lua scripting
- Box2D physics engine with Lua bindings
- Physics debug visualization
- Hot-reloading of shaders and scenes (F5 in debug mode)

## Dependencies

- SDL2
- Vulkan
- Box2D
- Lua 5.4
- LZ4
- CMake

Install on Ubuntu/Debian:

```
sudo apt-get update
sudo apt-get install cmake libsdl2-dev vulkan-tools libvulkan-dev glslc liblz4-dev lua5.4 liblua5.4-dev libbox2d-dev
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

The application will open in fullscreen mode displaying a colored triangle (red, green, blue vertices). 

### Controls

- **ESC**: Close the current scene/exit application
- **ENTER**: Open the menu scene
- **P**: Open the physics demo scene  
- **SPACE** (in physics demo): Apply upward impulse to the ball
- **R** (in physics demo): Reset physics objects to initial positions
- **ALT+ENTER**: Toggle fullscreen/windowed mode
- **F5** (debug mode): Hot-reload shaders and scenes

## Physics Demo

The physics demo scene demonstrates Box2D integration:
- Dynamic boxes that fall and collide
- A dynamic circle (ball) that bounces
- Static ground platform
- Debug visualization of physics shapes

Press 'P' from the main scene to try it out!