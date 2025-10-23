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
include_directories(${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS} ${LZ4_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS})

# Find glslc for shader compilation
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK.")
endif()

# Compile shaders
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/nebula_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/nebula_vertex.glsl -o nebula_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/nebula_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/nebula_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/nebula_fragment.glsl -o nebula_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/nebula_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/menu_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/menu_vertex.glsl -o menu_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/menu_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/menu_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/menu_fragment.glsl -o menu_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/menu_fragment.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/cloud_vertex.spv
    COMMAND ${GLSLC} -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/cloud_vertex.glsl -o cloud_vertex.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/cloud_vertex.glsl
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/cloud_fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/cloud_fragment.glsl -o cloud_fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/cloud_fragment.glsl
)
add_custom_target(shaders DEPENDS nebula_vertex.spv nebula_fragment.spv menu_vertex.spv menu_fragment.spv cloud_vertex.spv cloud_fragment.spv)

add_executable(packer src/packer.cpp src/ResourceTypes.h)
target_link_libraries(packer ${LZ4_LIBRARIES})

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/res.pak
    COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${CMAKE_BINARY_DIR}/nebula_vertex.spv ${CMAKE_BINARY_DIR}/nebula_fragment.spv ${CMAKE_BINARY_DIR}/menu_vertex.spv ${CMAKE_BINARY_DIR}/menu_fragment.spv ${CMAKE_BINARY_DIR}/cloud_vertex.spv ${CMAKE_BINARY_DIR}/cloud_fragment.spv ${CMAKE_SOURCE_DIR}/scenes/default.lua ${CMAKE_SOURCE_DIR}/scenes/menu.lua
    DEPENDS packer ${CMAKE_BINARY_DIR}/nebula_vertex.spv ${CMAKE_BINARY_DIR}/nebula_fragment.spv ${CMAKE_BINARY_DIR}/menu_vertex.spv ${CMAKE_BINARY_DIR}/menu_fragment.spv ${CMAKE_BINARY_DIR}/cloud_vertex.spv ${CMAKE_BINARY_DIR}/cloud_fragment.spv ${CMAKE_SOURCE_DIR}/scenes/default.lua ${CMAKE_SOURCE_DIR}/scenes/menu.lua
)

add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

add_executable(shader_triangle src/main.cpp src/config.cpp src/resource.cpp src/VulkanRenderer.cpp src/LuaInterface.cpp src/SceneManager.cpp)
add_dependencies(shader_triangle shaders res_pak)
target_link_libraries(shader_triangle ${SDL2_LIBRARIES} Vulkan::Vulkan ${LZ4_LIBRARIES} ${LUA_LIBRARIES})

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
)