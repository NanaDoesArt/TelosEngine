include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 6.2
        GIT_SHALLOW TRUE
)

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

FetchContent_MakeAvailable(raylib mimalloc EASTL spdlog thread_pool)

add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE "${thread_pool_SOURCE_DIR}/include")