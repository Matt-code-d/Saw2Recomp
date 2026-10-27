# saw2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(saw2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SAW2_SOURCES
    src/main.cpp
    src/stubs.cpp
    src/math_polyfill.cpp
    src/ue3_framerate_unlock.cpp
    src/saw2_hud_mod.cpp
)

if(WIN32)
    list(APPEND SAW2_SOURCES src/saw2.rc)
    add_executable(saw2 WIN32 ${SAW2_SOURCES})
else()
    add_executable(saw2 ${SAW2_SOURCES})
endif()

rexglue_setup_target(saw2 GPU_PLUGINS xenos)

# HUD art next to saw2.exe (hpbar frame + fill line).
add_custom_command(TARGET saw2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/hpbar.png"
        "$<TARGET_FILE_DIR:saw2>/hpbar.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/hpbarline.png"
        "$<TARGET_FILE_DIR:saw2>/hpbarline.png"
    COMMENT "Copying HUD hpbar assets next to saw2.exe"
    VERBATIM)

# Publish the WPF launcher next to saw2.exe (Windows).
option(SAW2_BUILD_LAUNCHER "Build Saw2Launcher into the same folder as saw2.exe" ON)
if(WIN32 AND SAW2_BUILD_LAUNCHER)
    find_program(SAW2_DOTNET_EXE dotnet)
    if(SAW2_DOTNET_EXE)
        set(SAW2_LAUNCHER_PROJ "${CMAKE_SOURCE_DIR}/launcher/Saw2Launcher.csproj")
        file(GLOB_RECURSE SAW2_LAUNCHER_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/launcher/*.cs"
            "${CMAKE_SOURCE_DIR}/launcher/*.xaml"
            "${CMAKE_SOURCE_DIR}/launcher/*.csproj")
        set(SAW2_LAUNCHER_STAMP "${CMAKE_BINARY_DIR}/Saw2Launcher.publish.stamp")
        add_custom_command(
            OUTPUT "${SAW2_LAUNCHER_STAMP}"
            COMMAND "${SAW2_DOTNET_EXE}" publish "${SAW2_LAUNCHER_PROJ}"
                -c Release
                -o "${CMAKE_BINARY_DIR}"
                --self-contained false
                -p:DebugType=None
                -p:DebugSymbols=false
            COMMAND "${CMAKE_COMMAND}" -E touch "${SAW2_LAUNCHER_STAMP}"
            DEPENDS ${SAW2_LAUNCHER_SOURCES}
            COMMENT "Publishing Saw2Launcher.exe next to saw2.exe"
            VERBATIM)
        add_custom_target(saw2_launcher ALL DEPENDS "${SAW2_LAUNCHER_STAMP}")
        add_dependencies(saw2_launcher saw2)
    else()
        message(STATUS "dotnet not found — skipping Saw2Launcher publish (install .NET 8 SDK)")
    endif()
endif()
