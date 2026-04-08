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

# When OFF, only the packer tool is built (no SDL3/Vulkan/Lua/Box2D/OpenAL/OpusFile needed).
# Useful for a native host build that supplies a packer binary for cross-compilation.
option(BUILD_GAME "Build the main game executable" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(SQUISH REQUIRED IMPORTED_TARGET libsquish)
# jsoncpp is only used by the packer host tool, never by the game itself.
# Skip it (and the packer target below) when cross-compiling for Android so
# the android sysroot does not need a jsoncpp cross-build.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
endif()
include_directories(${PNG_INCLUDE_DIRS})

if(BUILD_GAME)
    find_package(Vulkan REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)
    pkg_check_modules(OPENAL REQUIRED IMPORTED_TARGET openal)
    pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile)
    pkg_check_modules(OGG QUIET IMPORTED_TARGET ogg)
    pkg_check_modules(OPUS QUIET IMPORTED_TARGET opus)
    # opusfile depends on ogg and opus; with dynamic libraries these are resolved
    # transitively by the linker, but static builds require them to be linked
    # explicitly.  Append them to opusfile's interface so any target that links
    # PkgConfig::OPUSFILE automatically gets ogg and opus too.
    if(OGG_FOUND AND TARGET PkgConfig::OGG)
        set_property(TARGET PkgConfig::OPUSFILE APPEND PROPERTY INTERFACE_LINK_LIBRARIES PkgConfig::OGG)
    endif()
    if(OPUS_FOUND AND TARGET PkgConfig::OPUS)
        set_property(TARGET PkgConfig::OPUSFILE APPEND PROPERTY INTERFACE_LINK_LIBRARIES PkgConfig::OPUS)
    endif()
    include_directories(${SDL3_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIRS} ${OPUSFILE_INCLUDE_DIRS})
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
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_executable(packer tools/packer.cpp src/core/ResourceTypes.h)
    target_sources(packer PRIVATE src/compress/Compress.cpp)
    target_link_libraries(packer PkgConfig::PNG PkgConfig::ZLIB PkgConfig::SQUISH PkgConfig::JSONCPP)
endif()

if(BUILD_GAME)
    file(GLOB_RECURSE RES_FILES
        "${CMAKE_SOURCE_DIR}/res/*.png"
        "${CMAKE_SOURCE_DIR}/res/*.opus"
        "${CMAKE_SOURCE_DIR}/res/*.lua"
        "${CMAKE_SOURCE_DIR}/res/*.loop"
    )

    # When cross-compiling, the built packer is a foreign executable and cannot run
    # on the host.  Require the caller to supply a native packer via -DNATIVE_PACKER=.
    if(CMAKE_CROSSCOMPILING)
        if(NOT NATIVE_PACKER)
            message(FATAL_ERROR "NATIVE_PACKER must be set to a native packer executable when cross-compiling")
        endif()
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/res.pak
            COMMAND "${NATIVE_PACKER}" ${CMAKE_BINARY_DIR}/res.pak ${SHADER_FILES} ${RES_FILES} --output-atlases
            DEPENDS ${SHADER_FILES} ${RES_FILES} shaders
        )
    else()
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/res.pak
            COMMAND packer ${CMAKE_BINARY_DIR}/res.pak ${SHADER_FILES} ${RES_FILES} --output-atlases
            DEPENDS packer ${SHADER_FILES} ${RES_FILES} shaders
        )
    endif()

    add_custom_target(res_pak DEPENDS ${CMAKE_BINARY_DIR}/res.pak)

    set(GAME_SOURCES
        src/main.cpp
        src/core/config.cpp
        src/core/TrigLookup.cpp
        src/resources/resource.cpp
        src/vulkan/VulkanRenderer.cpp
        src/vulkan/VulkanBuffer.cpp
        src/vulkan/VulkanTexture.cpp
        src/vulkan/VulkanDescriptor.cpp
        src/vulkan/VulkanPipeline.cpp
        src/vulkan/VulkanLight.cpp
        src/vulkan/VulkanWaterPolygon.cpp
        src/scene/LuaInterface.cpp
        src/scene/SceneManager.cpp
        src/physics/Box2DPhysics.cpp
        src/scene/SceneLayer.cpp
        src/audio/AudioManager.cpp
        src/effects/ParticleSystem.cpp
        src/effects/WaterEffect.cpp
        src/memory/SmallMemoryAllocator.cpp
        src/memory/LargeMemoryAllocator.cpp
        src/core/String.cpp
        src/animation/AnimationEngine.cpp
        src/compress/Compress.cpp
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND GAME_SOURCES src/debug/ImGuiManager.cpp)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        add_library(shader_triangle SHARED ${GAME_SOURCES})
        set_target_properties(shader_triangle PROPERTIES OUTPUT_NAME "main")
    else()
        add_executable(shader_triangle ${GAME_SOURCES})
    endif()
    add_dependencies(shader_triangle shaders res_pak)

    # Embed the application icon on Windows builds (native or cross-compiled)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(APP_ICON_RC "${CMAKE_BINARY_DIR}/app_icon.rc")
        file(WRITE "${APP_ICON_RC}" "// IDI_ICON1 is the standard Windows resource identifier for the application icon.\nIDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/res/icons/rock.ico\"\n")
        target_sources(shader_triangle PRIVATE "${APP_ICON_RC}")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_libraries(shader_triangle PkgConfig::SDL3 Vulkan::Vulkan PkgConfig::LUA box2d::box2d PkgConfig::OPENAL PkgConfig::OPUSFILE ${IMGUI_LIBRARIES})
    else()
        target_link_libraries(shader_triangle PkgConfig::SDL3 Vulkan::Vulkan PkgConfig::LUA box2d::box2d PkgConfig::OPENAL PkgConfig::OPUSFILE)
    endif()

    if(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        target_link_options(shader_triangle PRIVATE -nostdlib)
    endif()

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
        if(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
            target_link_options(shader_triangle PRIVATE -nostdlib++)
        endif()
        if(WIN32)
            target_link_options(shader_triangle PRIVATE
                -static-libgcc
                -static-libstdc++
                -Wl,-Bstatic,-lpthread,-Bdynamic
            )
        endif()
        target_compile_options(shader_triangle PRIVATE -fno-rtti -fno-exceptions)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
            target_sources(shader_triangle PRIVATE src/core/no_stl_shims.cpp)
        else()
            target_sources(shader_triangle PRIVATE src/core/android_main.cpp)
            target_link_libraries(shader_triangle log android)
        endif()
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
        )
        if(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
            target_link_options(shader_triangle PRIVATE
                -Wl,--build-id=none
                -Wl,-z,max-page-size=4096
            )
        endif()
        add_custom_command(TARGET shader_triangle POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:shader_triangle>
            COMMENT "Stripping debug symbols from release binary"
        )
    endif()
endif()

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
)
