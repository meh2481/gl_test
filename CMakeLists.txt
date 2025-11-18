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
find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(LUA REQUIRED lua)
pkg_check_modules(PNG REQUIRED libpng)
pkg_check_modules(SQUISH REQUIRED libsquish)
include_directories(${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS} ${LZ4_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS} ${SQUISH_INCLUDE_DIRS})
find_package(box2d CONFIG REQUIRED)

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
add_custom_target(shaders DEPENDS vertex.spv nebula_fragment.spv menu_fragment.spv cloud_fragment.spv debug_vertex.spv debug_fragment.spv sprite_vertex.spv sprite_fragment.spv)

add_executable(packer src/packer.cpp src/ResourceTypes.h)
target_link_libraries(packer ${LZ4_LIBRARIES} ${PNG_LIBRARIES} squish)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/res.pak
    COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/nebula_fragment.spv ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/menu_fragment.spv ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/cloud_fragment.spv ${CMAKE_BINARY_DIR}/debug_vertex.spv ${CMAKE_BINARY_DIR}/debug_fragment.spv ${CMAKE_BINARY_DIR}/sprite_vertex.spv ${CMAKE_BINARY_DIR}/sprite_fragment.spv ${CMAKE_SOURCE_DIR}/scenes/default.lua ${CMAKE_SOURCE_DIR}/scenes/menu.lua ${CMAKE_SOURCE_DIR}/scenes/physics.lua ${CMAKE_SOURCE_DIR}/res/metalwall.png ${CMAKE_SOURCE_DIR}/res/rock.png
    DEPENDS packer ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/nebula_fragment.spv ${CMAKE_BINARY_DIR}/menu_fragment.spv ${CMAKE_BINARY_DIR}/cloud_fragment.spv ${CMAKE_BINARY_DIR}/debug_vertex.spv ${CMAKE_BINARY_DIR}/debug_fragment.spv ${CMAKE_BINARY_DIR}/sprite_vertex.spv ${CMAKE_BINARY_DIR}/sprite_fragment.spv ${CMAKE_SOURCE_DIR}/scenes/default.lua ${CMAKE_SOURCE_DIR}/scenes/menu.lua ${CMAKE_SOURCE_DIR}/scenes/physics.lua ${CMAKE_SOURCE_DIR}/res/metalwall.png ${CMAKE_SOURCE_DIR}/res/rock.png
)

add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

add_executable(shader_triangle src/main.cpp src/config.cpp src/resource.cpp src/VulkanRenderer.cpp src/LuaInterface.cpp src/SceneManager.cpp src/Box2DPhysics.cpp src/SceneLayer.cpp)
add_dependencies(shader_triangle shaders res_pak)
target_link_libraries(shader_triangle ${SDL2_LIBRARIES} Vulkan::Vulkan ${LZ4_LIBRARIES} ${LUA_LIBRARIES} box2d::box2d)

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
)