cmake_minimum_required(VERSION 3.21)

project(VEngine LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_OBJECT_PATH_MAX 240)
# Allow Linux (.so) and macOS (.dylib) executables to load modules from their own folder
if(APPLE)
    set(CMAKE_BUILD_RPATH "@executable_path")
    set(CMAKE_INSTALL_RPATH "@executable_path")
elseif(UNIX)
    set(CMAKE_BUILD_RPATH "$ORIGIN")
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# Auto-generate .gitignore if missing
# NOTE: the per-project and per-module CMakeLists.txt files are generated from Templates/ on every
# configure, so they must not be committed. If you already have a .gitignore, add those two lines by hand.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.gitignore")
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/.gitignore"
            "# VEngine Automated Git Ignore\n"
            "Intermediate/\n"
            "Build/\n"
            ".idea/\n"
            ".vs/\n"
            "cmake-build-*/\n"
            "*.user\n"
            "\n"
            "# Generated from Templates/ on every configure\n"
            "/*/CMakeLists.txt\n"
            "/*/Source/*/CMakeLists.txt\n"
            "\n"
            "# Generated module boilerplate: IMPLEMENT_MODULE lives here, one per module.\n"
            "*.gen.cpp\n"
    )
endif()

# Unified output path for Single-Config (Ninja) and Multi-Config (VS/Xcode) generators.
# $<CONFIG> expands to Debug/Release/etc. for every generator, so no per-config overrides are needed.
set(CENTRAL_BIN "${CMAKE_CURRENT_SOURCE_DIR}/Build/Binaries/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CENTRAL_BIN}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CENTRAL_BIN}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CENTRAL_BIN}")

include("${CMAKE_SOURCE_DIR}/Templates/EngineMacros.cmake")
vengine_orchestrate_workspace()