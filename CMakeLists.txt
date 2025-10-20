cmake_minimum_required(VERSION 3.10)
project(ShaderTriangle)
set(CMAKE_CXX_STANDARD 17)
find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

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
    OUTPUT ${CMAKE_BINARY_DIR}/fragment.spv
    COMMAND ${GLSLC} -fshader-stage=frag ${CMAKE_SOURCE_DIR}/shaders/fragment.glsl -o fragment.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/fragment.glsl
)
add_custom_target(shaders DEPENDS vertex.spv fragment.spv)

add_executable(packer src/packer.cpp src/ResourceTypes.h)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/res.pak
    COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/fragment.spv
    DEPENDS packer ${CMAKE_BINARY_DIR}/vertex.spv ${CMAKE_BINARY_DIR}/fragment.spv
)

add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

add_executable(shader_triangle src/main.cpp src/resource.cpp)
add_dependencies(shader_triangle shaders res_pak)
target_link_libraries(shader_triangle ${SDL2_LIBRARIES} Vulkan::Vulkan)