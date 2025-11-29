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
            include_directories($ENV{HOME}/.local/include)
            link_directories($ENV{HOME}/.local/lib)
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
file(GLOB SHADER_SOURCES "${CMAKE_SOURCE_DIR}/shaders/*.glsl")
set(SHADER_FILES)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    if(SHADER_NAME MATCHES "vertex")
        set(STAGE vert)
    else()
        set(STAGE frag)
    endif()
    set(SPV_FILE ${CMAKE_BINARY_DIR}/${SHADER_NAME}.spv)
    list(APPEND SHADER_FILES ${SPV_FILE})
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC} -fshader-stage=${STAGE} ${SHADER} -o ${SPV_FILE}
        DEPENDS ${SHADER}
    )
endforeach()
add_custom_target(shaders DEPENDS ${SHADER_FILES})

add_executable(packer tools/packer.cpp src/ResourceTypes.h)
target_link_libraries(packer ${LZ4_LIBRARIES} ${PNG_LIBRARIES} squish)

file(GLOB SCENE_FILES "${CMAKE_SOURCE_DIR}/scenes/*.lua")
file(GLOB RES_FILES "${CMAKE_SOURCE_DIR}/res/*")
file(GLOB FX_FILES "${CMAKE_SOURCE_DIR}/res/fx/*.lua")

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/res.pak
    COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${SHADER_FILES} ${SCENE_FILES} ${RES_FILES} ${FX_FILES}
    DEPENDS packer ${SHADER_FILES} ${SCENE_FILES} ${RES_FILES} ${FX_FILES} shaders
)

add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(shader_triangle src/main.cpp src/config.cpp src/resource.cpp src/VulkanRenderer.cpp src/LuaInterface.cpp src/SceneManager.cpp src/Box2DPhysics.cpp src/SceneLayer.cpp src/AudioManager.cpp src/ParticleSystem.cpp src/ImGuiManager.cpp)
else()
    add_executable(shader_triangle src/main.cpp src/config.cpp src/resource.cpp src/VulkanRenderer.cpp src/LuaInterface.cpp src/SceneManager.cpp src/Box2DPhysics.cpp src/SceneLayer.cpp src/AudioManager.cpp src/ParticleSystem.cpp)
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