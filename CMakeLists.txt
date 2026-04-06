cmake_minimum_required(VERSION 3.10)
project(ShaderTriangle)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector -fno-builtin")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(SQUISH REQUIRED libsquish)
pkg_check_modules(OPENAL REQUIRED IMPORTED_TARGET openal)
pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile)
include_directories(${SDL3_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS} ${SQUISH_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIRS} ${OPUSFILE_INCLUDE_DIRS})
find_package(box2d CONFIG REQUIRED)

# ImGui setup for debug builds only
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Try CMake config first (for Arch imgui-full), then pkg-config (for Ubuntu libimgui-dev), then manual for CI
    find_package(imgui QUIET)
    if(imgui_FOUND)
        include_directories(${imgui_INCLUDE_DIRS})
        set(IMGUI_LIBRARIES imgui::imgui)
    else()
        pkg_check_modules(IMGUI QUIET IMPORTED_TARGET imgui)
        if(IMGUI_FOUND)
            include_directories(${IMGUI_INCLUDE_DIRS})
            set(IMGUI_LIBRARIES PkgConfig::IMGUI)
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

# Compile shaders (now in res/shaders/ and res/objects/*/)
file(GLOB SHADER_SOURCES "${CMAKE_SOURCE_DIR}/res/shaders/*.glsl")
file(GLOB OBJECT_SHADER_SOURCES "${CMAKE_SOURCE_DIR}/res/objects/*/*.glsl")
list(APPEND SHADER_SOURCES ${OBJECT_SHADER_SOURCES})
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

add_executable(packer tools/packer.cpp src/core/ResourceTypes.h)
target_sources(packer PRIVATE src/compress/Compress.cpp)
target_link_libraries(packer PkgConfig::PNG squish)

file(GLOB_RECURSE RES_FILES
    "${CMAKE_SOURCE_DIR}/res/*.png"
    "${CMAKE_SOURCE_DIR}/res/*.opus"
    "${CMAKE_SOURCE_DIR}/res/*.lua"
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/res.pak
    COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${SHADER_FILES} ${RES_FILES} --output-atlases
    DEPENDS packer ${SHADER_FILES} ${RES_FILES} shaders
)

add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(shader_triangle src/main.cpp src/core/config.cpp src/core/TrigLookup.cpp src/resources/resource.cpp src/vulkan/VulkanRenderer.cpp src/vulkan/VulkanBuffer.cpp src/vulkan/VulkanTexture.cpp src/vulkan/VulkanDescriptor.cpp src/vulkan/VulkanPipeline.cpp src/vulkan/VulkanLight.cpp src/vulkan/VulkanWaterPolygon.cpp src/scene/LuaInterface.cpp src/scene/SceneManager.cpp src/physics/Box2DPhysics.cpp src/scene/SceneLayer.cpp src/audio/AudioManager.cpp src/effects/ParticleSystem.cpp src/debug/ImGuiManager.cpp src/effects/WaterEffect.cpp src/memory/SmallMemoryAllocator.cpp src/memory/LargeMemoryAllocator.cpp src/core/String.cpp src/animation/AnimationEngine.cpp src/compress/Compress.cpp)
else()
    add_executable(shader_triangle src/main.cpp src/core/config.cpp src/core/TrigLookup.cpp src/resources/resource.cpp src/vulkan/VulkanRenderer.cpp src/vulkan/VulkanBuffer.cpp src/vulkan/VulkanTexture.cpp src/vulkan/VulkanDescriptor.cpp src/vulkan/VulkanPipeline.cpp src/vulkan/VulkanLight.cpp src/vulkan/VulkanWaterPolygon.cpp src/scene/LuaInterface.cpp src/scene/SceneManager.cpp src/physics/Box2DPhysics.cpp src/scene/SceneLayer.cpp src/audio/AudioManager.cpp src/effects/ParticleSystem.cpp src/effects/WaterEffect.cpp src/memory/SmallMemoryAllocator.cpp src/memory/LargeMemoryAllocator.cpp src/core/String.cpp src/animation/AnimationEngine.cpp src/compress/Compress.cpp)
endif()
add_dependencies(shader_triangle shaders res_pak)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(shader_triangle PkgConfig::SDL3 Vulkan::Vulkan PkgConfig::LUA box2d::box2d PkgConfig::OPENAL PkgConfig::OPUSFILE ${IMGUI_LIBRARIES})
else()
    target_link_libraries(shader_triangle PkgConfig::SDL3 Vulkan::Vulkan PkgConfig::LUA box2d::box2d PkgConfig::OPENAL PkgConfig::OPUSFILE)
endif()

target_link_options(shader_triangle PRIVATE -nostdlib)



# Enforce no direct libm linkage for shader_triangle.
# This keeps unresolved math symbols visible as link errors instead of silently
# being satisfied by a transitive -lm from dependency metadata.
function(strip_libm_from_interface target_name)
    if(TARGET ${target_name})
        get_target_property(_iface_libs ${target_name} INTERFACE_LINK_LIBRARIES)
        if(_iface_libs)
            set(_filtered_iface_libs "${_iface_libs}")
            list(FILTER _filtered_iface_libs EXCLUDE REGEX "(^|;)m($|;)")
            list(FILTER _filtered_iface_libs EXCLUDE REGEX "(^|;)-lm($|;)")
            set_target_properties(${target_name} PROPERTIES INTERFACE_LINK_LIBRARIES "${_filtered_iface_libs}")
        endif()
    endif()
endfunction()

strip_libm_from_interface(PkgConfig::SDL3)
strip_libm_from_interface(PkgConfig::OPENAL)
strip_libm_from_interface(PkgConfig::OPUSFILE)
strip_libm_from_interface(PkgConfig::IMGUI)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(shader_triangle PRIVATE -Wl,--as-needed)
    target_link_options(shader_triangle PRIVATE -nostdlib++)
    target_compile_options(shader_triangle PRIVATE -fno-rtti -fno-exceptions)
    target_sources(shader_triangle PRIVATE src/core/no_stl_shims.cpp)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(shader_triangle PRIVATE
        -Os
        -fno-exceptions
        -fno-asynchronous-unwind-tables
        -fno-unwind-tables
        -ffunction-sections
        -fdata-sections
        -fno-rtti
        -fvisibility=hidden
        -fomit-frame-pointer
        -fno-ident
    )
    target_link_options(shader_triangle PRIVATE
        -s
        -Wl,-O1
        -Wl,--gc-sections
        -Wl,--strip-all
        -Wl,--build-id=none
        -Wl,-z,max-page-size=4096
    )
    add_custom_command(TARGET shader_triangle POST_BUILD
        COMMAND strip --strip-all $<TARGET_FILE:shader_triangle>
        COMMENT "Stripping debug symbols from release binary"
    )
endif()

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
)
