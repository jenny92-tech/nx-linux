# INPUT: Explicit libGPUCounters source; the tested version is commit 378c7b59e2da.
# OUTPUT: Read-only ARM Mali counter sampler, without game/runtime dependencies.
# POS: Optional profiler; not linked into or enabled by the normal launcher.
cmake_minimum_required(VERSION 3.13.5)
project(d2r_gpu_counters LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(HWCPIPE_SOURCE "" CACHE PATH "Explicit libGPUCounters source directory")
if(NOT EXISTS "${HWCPIPE_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Set HWCPIPE_SOURCE; see ../GPU-PROFILING.zh-CN.md")
endif()
add_subdirectory("${HWCPIPE_SOURCE}" counters)
add_executable(d2r-gpu-counters probe.cpp)
target_link_libraries(d2r-gpu-counters PRIVATE hwcpipe device)
