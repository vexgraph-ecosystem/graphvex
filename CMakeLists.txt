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
# (Rule 17). Those layers link graphvex, never the reverse.
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

if(NOT TARGET graphvex)
add_library(graphvex STATIC
    src/graphvex/version.c
    src/graphvex/gfx_loop.c
    src/buffer/buffer.c
    src/buffer/color_buffer.c
    src/buffer/depth_buffer.c
    src/buffer/frame_buffer.c
    src/buffer/height_buffer.c
    src/buffer/normal_buffer.c
    src/buffer/shadow_buffer.c
    src/buffer/stencil_buffer.c
    src/font/font.c
    src/font/font_bake.c
    src/io/vfs.c
    src/vulkan/sdf_gpu.c
    src/vulkan/graphics_layer.c
    src/vulkan/spv_watch.c
    src/vulkan/vk_loader.c
    src/vulkan/vk_instance.c
    src/vulkan/vk_mac.c
    src/vulkan/vk_layer.c
    src/vulkan/vk_pane.c
    src/vulkan/vk_window_seam.c
    src/vulkan/vk_scene.c
    src/vulkan/vk_view.c
    src/vulkan/vk_iosurface.c
    src/vulkan/texture/texture.c
    src/image/image.c
    src/image/frame_importer.c
    src/sync/fence.c
    src/sync/semaphore.c
    src/sync/command_buffer.c
    src/sync/command_queue.c
    src/buffer/gpu_buffer.c
    src/image/bindless_heap.c
    src/paint/stroke.c
    src/paint/brush.c
    src/paint/vector_brush.c
    src/paint/raster_brush.c
    src/draw/drawable.c
    src/scene/pipeline.c
    src/scene/pass.c
    src/scene/scene.c
    src/mesh/mesh.c
    src/mesh/meshlet.c
    src/image/swapchain.c
    src/vector/shape.c
    src/graphics/graphics.c
    src/direct/direct_graphics.c
    src/draw/layered_drawable.c
    src/draw/vector_drawable.c
    src/draw/layered_vector_drawable.c
)

target_include_directories(graphvex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(graphvex PRIVATE -Wall -Wextra -Werror -mcpu=native)

# Rule 39/40: DEBUG macro gates Vulkan validation layers + debug messenger.
# In Debug builds, enable exhaustive validation; in Release, everything is
# compiled out and stripped via -ffunction-sections/-fdata-sections + -dead_strip.
target_compile_definitions(graphvex PRIVATE
    $<$<CONFIG:Debug>:DEBUG=1>
)

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_include_directories(graphvex PUBLIC ${Vulkan_INCLUDE_DIRS})
elseif(EXISTS "/opt/homebrew/include/vulkan/vulkan.h")
    target_include_directories(graphvex PUBLIC /opt/homebrew/include)
endif()

if(TARGET vexspoke)
    target_link_libraries(graphvex PUBLIC vexspoke)
endif()

# Apple GPU/bridge frameworks (vk_iosurface.c, texture.c image decode).
if(APPLE)
    target_sources(graphvex PRIVATE
        src/objc/font_cocoa.m
        src/objc/graphics_layer_cocoa.m
    )
    set_source_files_properties(src/objc/font_cocoa.m src/objc/graphics_layer_cocoa.m PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(graphvex PUBLIC
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework IOSurface"
        "-framework Foundation"
        "-framework AppKit"
        "-framework CoreText"
        "-framework Metal"
        "-framework QuartzCore"
    )
endif()
endif() # NOT TARGET graphvex

set(GRAPHVEX_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/graphvex")
if(NOT EXISTS "${GRAPHVEX_TEST_DIR}")
    set(GRAPHVEX_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()

# Buffer smoke test (relocated from the vexspoke `vexspoke_demo` harness with the move).
if(EXISTS "${GRAPHVEX_TEST_DIR}/buffer_test.c" AND NOT TARGET buffer_test)
    add_executable(buffer_test
        ${GRAPHVEX_TEST_DIR}/buffer_test.c
    )
    target_link_libraries(buffer_test PRIVATE graphvex)
endif()

# GraphicsLayer board-shim seam test: headless cold-boundary matrix
# (nullptr/hostile-size/role/pulse). Pure-C side only, no GPU, no AppKit.
if(EXISTS "${GRAPHVEX_TEST_DIR}/graphics_layer_test.c" AND NOT TARGET graphics_layer_test)
    add_executable(graphics_layer_test
        ${GRAPHVEX_TEST_DIR}/graphics_layer_test.c
    )
    target_link_libraries(graphics_layer_test PRIVATE graphvex)
    target_compile_options(graphics_layer_test PRIVATE -Wall -Wextra -Werror)
endif()
# Texture retire-ring contract test: headless cold-validation matrix
# (nullptr/wrong-id/zero-size/overflow) + retire depth/capacity/seq defaults
# + shutdown-with-no-flight. Links graphvex; needs no GPU.
if(EXISTS "${GRAPHVEX_TEST_DIR}/texture_retire_test.c" AND NOT TARGET texture_retire_test)
    add_executable(texture_retire_test
        ${GRAPHVEX_TEST_DIR}/texture_retire_test.c
    )
    target_link_libraries(texture_retire_test PRIVATE graphvex)
endif()

# GfxLoop demand-driven scheduler and 2-VkImage bridge test
if(EXISTS "${GRAPHVEX_TEST_DIR}/gfx_loop_test.c" AND NOT TARGET gfx_loop_test)
    add_executable(gfx_loop_test
        ${GRAPHVEX_TEST_DIR}/gfx_loop_test.c
    )
    target_link_libraries(gfx_loop_test PRIVATE graphvex)
    target_compile_options(gfx_loop_test PRIVATE -Wall -Wextra -Werror)
endif()

# Unified Graphics seam: setGraphics/forwarder cold matrix + DirectGraphics
# pixel-exact software row tests (headless, no GPU, no AppKit).
if(EXISTS "${GRAPHVEX_TEST_DIR}/direct_graphics_test.c" AND NOT TARGET direct_graphics_test)
    add_executable(direct_graphics_test
        ${GRAPHVEX_TEST_DIR}/direct_graphics_test.c
    )
    target_link_libraries(direct_graphics_test PRIVATE graphvex)
    target_compile_options(direct_graphics_test PRIVATE -Wall -Wextra -Werror)
endif()

