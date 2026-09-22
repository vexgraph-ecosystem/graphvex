cmake_minimum_required(VERSION 3.28)
project(graphvex C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(APPLE)
    enable_language(OBJC)
endif()

# Release: dead-code stripping so unused symbols cost zero bytes in artifacts.
# (Duplicated per repo for standalone builds; harmless when built in-tree.)
if(APPLE)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip>)
elseif(UNIX)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

# Upstream dependency (if not already provided by parent build).
# graphvex is a graphics FOUNDATION layer: it may depend ONLY on vexspoke.
# It must NEVER include headers from hotcwap, darling, or api-haven
# (the Vertical Integration Law). Those layers link graphvex, never the reverse.
if(NOT TARGET vexspoke)
    option(GRAPHVEX_USE_UPSTREAM "Depend on vexspoke via FetchContent" OFF)
    if(GRAPHVEX_USE_UPSTREAM)
        include(FetchContent)
        FetchContent_Declare(
            vexspoke
            GIT_REPOSITORY https://github.com/vexgraph-dev/vexspoke.git
            GIT_TAG main
        )
        FetchContent_MakeAvailable(vexspoke)
    endif()
endif()

# ============================================================================
# The legacy tree was retired to src/_old/ (gitignored, unbuilt reference) in
# this commit. The language-of-graphics sources land next commit; an INTERFACE
# target keeps the graphvex name linkable for downstream consumers meanwhile.
# ============================================================================
if(NOT TARGET graphvex)
add_library(graphvex INTERFACE)
target_include_directories(graphvex INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(TARGET vexspoke)
    target_link_libraries(graphvex INTERFACE vexspoke)
endif()
endif()
