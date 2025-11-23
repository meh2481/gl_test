cmake_minimum_required(VERSION 3.10)
project(ShaderTriangle)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(LUA REQUIRED lua5.4)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(SQUISH REQUIRED libsquish)
pkg_check_modules(OPENAL REQUIRED openal)
pkg_check_modules(OPUSFILE REQUIRED opusfile)
include_directories(${SDL3_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS} ${LZ4_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS} ${SQUISH_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIRS} ${OPUSFILE_INCLUDE_DIRS})
find_package(box2d CONFIG REQUIRED)

# ImGui setup for debug builds only
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Try CMake config first (for Arch imgui-full), then pkg-config (for Ubuntu libimgui-dev), then manual for CI
    find_package(imgui QUIET)
    if(imgui_FOUND)
        include_directories(${imgui_INCLUDE_DIRS})
        set(IMGUI_LIBRARIES imgui::imgui)
    else()
        pkg_check_modules(IMGUI QUIET imgui)
        if(IMGUI_FOUND)
            include_directories(${IMGUI_INCLUDE_DIRS})
            set(IMGUI_LIBRARIES ${IMGUI_LIBRARIES})
        else()
            # Manual setup for CI where ImGui is built manually
            include_directories(/usr/include)
            set(IMGUI_LIBRARIES imgui)
        endif()
    endif()
endif()

# Find glslc for shader compilation
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK.")
endif()

# Compile shaders
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/vertex.glsl -o vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/nebula_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/nebula_fragment.glsl -o nebula_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/nebula_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/menu_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/menu_fragment.glsl -o menu_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/menu_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/cloud_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/cloud_fragment.glsl -o cloud_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/cloud_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/debug_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/debug_vertex.glsl -o debug_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/debug_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/debug_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/debug_fragment.glsl -o debug_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/debug_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/sprite_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/sprite_vertex.glsl -o sprite_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/sprite_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/sprite_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/sprite_fragment.glsl -o sprite_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/sprite_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/phong_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/phong_vertex.glsl -o phong_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/phong_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/phong_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/phong_fragment.glsl -o phong_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/phong_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/toon_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/toon_vertex.glsl -o toon_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/toon_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/toon_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/toon_fragment.glsl -o toon_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/toon_fragment.glsl
)
add_custom_target(shaders DEPENDS vertex.spv nebula_fragment.spv menu_fragment.spv cloud_fragment.spv debug_vertex.spv debug_fragment.spv sprite_vertex.spv sprite_fragment.spv phong_vertex.spv phong_fragment.spv toon_vertex.spv toon_fragment.spv)

add_executable(packer tools/packer.cpp src/ResourceTypes.h)
target_link_libraries(packer ${LZ4_LIBRARIES} ${PNG_LIBRARIES} squish)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/res.pak
    COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/nebula_fragment.spv ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/menu_fragment.spv ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/cloud_fragment.spv ${CMAKE_BINARY_DIR}/debug_vertex.spv ${CMAKE_BINARY_DIR}/debug_fragment.spv ${CMAKE_BINARY_DIR}/sprite_vertex.spv ${CMAKE_BINARY_DIR}/sprite_fragment.spv ${CMAKE_BINARY_DIR}/phong_vertex.spv ${CMAKE_BINARY_DIR}/phong_fragment.spv ${CMAKE_BINARY_DIR}/toon_vertex.spv ${CMAKE_BINARY_DIR}/toon_fragment.spv ${CMAKE_SOURCE_DIR}/scenes/default.lua ${CMAKE_SOURCE_DIR}/scenes/menu.lua ${CMAKE_SOURCE_DIR}/scenes/physics.lua ${CMAKE_SOURCE_DIR}/scenes/audio_test.lua ${CMAKE_SOURCE_DIR}/res/metalwall.png ${CMAKE_SOURCE_DIR}/res/rock.png ${CMAKE_SOURCE_DIR}/res/metalwall.norm.png ${CMAKE_SOURCE_DIR}/res/rock.norm.png ${CMAKE_SOURCE_DIR}/res/lantern.png ${CMAKE_SOURCE_DIR}/res/lantern.norm.png ${CMAKE_SOURCE_DIR}/res/sfx.opus
    DEPENDS packer ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/nebula_fragment.spv ${CMAKE_BINARY_DIR}/menu_fragment.spv ${CMAKE_BINARY_DIR}/cloud_fragment.spv ${CMAKE_BINARY_DIR}/debug_vertex.spv ${CMAKE_BINARY_DIR}/debug_fragment.spv ${CMAKE_BINARY_DIR}/sprite_vertex.spv ${CMAKE_BINARY_DIR}/sprite_fragment.spv ${CMAKE_BINARY_DIR}/phong_vertex.spv ${CMAKE_BINARY_DIR}/phong_fragment.spv ${CMAKE_BINARY_DIR}/toon_vertex.spv ${CMAKE_BINARY_DIR}/toon_fragment.spv ${CMAKE_SOURCE_DIR}/scenes/default.lua ${CMAKE_SOURCE_DIR}/scenes/menu.lua ${CMAKE_SOURCE_DIR}/scenes/physics.lua ${CMAKE_SOURCE_DIR}/scenes/audio_test.lua ${CMAKE_SOURCE_DIR}/res/metalwall.png ${CMAKE_SOURCE_DIR}/res/rock.png ${CMAKE_SOURCE_DIR}/res/metalwall.norm.png ${CMAKE_SOURCE_DIR}/res/rock.norm.png ${CMAKE_SOURCE_DIR}/res/lantern.png ${CMAKE_SOURCE_DIR}/res/lantern.norm.png ${CMAKE_SOURCE_DIR}/res/sfx.opus
)

add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(shader_triangle src/main.cpp src/config.cpp src/resource.cpp src/VulkanRenderer.cpp src/LuaInterface.cpp src/SceneManager.cpp src/Box2DPhysics.cpp src/SceneLayer.cpp src/AudioManager.cpp src/ImGuiManager.cpp)
else()
    add_executable(shader_triangle src/main.cpp src/config.cpp src/resource.cpp src/VulkanRenderer.cpp src/LuaInterface.cpp src/SceneManager.cpp src/Box2DPhysics.cpp src/SceneLayer.cpp src/AudioManager.cpp)
endif()
add_dependencies(shader_triangle shaders res_pak)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(shader_triangle ${SDL3_LIBRARIES} Vulkan::Vulkan ${LZ4_LIBRARIES} ${LUA_LIBRARIES} box2d::box2d ${OPENAL_LIBRARIES} ${OPUSFILE_LIBRARIES} ${IMGUI_LIBRARIES})
else()
    target_link_libraries(shader_triangle ${SDL3_LIBRARIES} Vulkan::Vulkan ${LZ4_LIBRARIES} ${LUA_LIBRARIES} box2d::box2d ${OPENAL_LIBRARIES} ${OPUSFILE_LIBRARIES})
endif()

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
)