include(FetchContent)

# 1. Memory Allocator (microsoft/mimalloc)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG v2.1.7
        GIT_SHALLOW ON
)

# 2. Game Engine Containers & Smart Pointers (electronicarts/EASTL)
set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        EASTL
        GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
        GIT_TAG 3.21.23
        GIT_SHALLOW ON
)

# 3. Fast Thread-Safe Logging (gabime/spdlog)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.0
        GIT_SHALLOW ON
)

# 4. Lightweight C++20 Thread Pool (bshoshany/thread-pool)
FetchContent_Declare(
        thread_pool
        GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
        GIT_TAG v4.1.0
        GIT_SHALLOW ON
)

# 5. Windowing (glfw/glfw)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.5.1
        GIT_SHALLOW ON
)

# 6. Realtime Math (nfrechette/rtm) - header only.
#
# RTM is NOT a CMake package and must not be add_subdirectory()'d:
#   - its root CMakeLists.txt is the project that builds RTM's own tests and benchmarks,
#     calls enable_testing(), and force-overrides CMAKE_CONFIGURATION_TYPES globally;
#   - it declares cmake_minimum_required(VERSION 3.2...3.25), which CMake 4 refuses
#     because compatibility with versions below 3.5 was removed.
#
# SOURCE_SUBDIR names a directory that has no CMakeLists.txt, so FetchContent downloads
# the headers and never runs that build. (This is the supported replacement for the
# deprecated single-argument FetchContent_Populate(rtm), which errors once
# cmake_minimum_required reaches 3.30 / policy CMP0169 becomes NEW.)
FetchContent_Declare(
        rtm
        GIT_REPOSITORY https://github.com/nfrechette/rtm.git
        GIT_TAG v2.4.0
        GIT_SHALLOW ON
        SOURCE_SUBDIR "cmake/no-consumer-build"
)

FetchContent_MakeAvailable(mimalloc EASTL spdlog thread_pool glfw rtm)

set_target_properties(
        glfw
        uninstall
        update_mappings
        PROPERTIES
        FOLDER "ThirdParty"
)

# RTM is header-only: one INTERFACE target, no artifact to link.
#
# SYSTEM: third-party headers are not held to the engine's warning level.
# cxx_std_20: RTM picks its std::bit_cast path under C++20, which is what the engine uses.
add_library(RTM INTERFACE)
add_library(RTM::RTM ALIAS RTM)

target_include_directories(RTM SYSTEM INTERFACE
        "${rtm_SOURCE_DIR}/includes"
)
target_compile_features(RTM INTERFACE cxx_std_20)

add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE "${thread_pool_SOURCE_DIR}/include")