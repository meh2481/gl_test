# Shader Triangle

A C++ application that opens a fullscreen window using SDL2 and renders a colored triangle using an OpenGL shader.

## Dependencies

- SDL2
- OpenGL
- GLEW
- CMake

Install on Ubuntu/Debian:

```
sudo apt-get update
sudo apt-get install cmake libsdl2-dev libglew-dev libgl1-mesa-dev
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

The application will open in fullscreen mode displaying a colored triangle (red, green, blue vertices). Press ESC or close the window to exit.