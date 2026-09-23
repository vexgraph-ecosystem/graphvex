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
# graphvex — the language of graphics (renovation tree).
#
# lang/   the contracts (backend-agnostic vocabulary)      — headers only
# device/ the device contract machinery (dialect registry) — DeviceRow + Device
# image/  the pixel currency (no row: a resource)          — Image
# filter/ the ordered compositor filter stack              — FilterRow + Filter + Stack
# vulkan/ the Vulkan dialect (device first)                — DeviceRow (vulkan)
#
# The old tree lives on disk as src/_old/ (gitignored, never built) and is the
# porting reference. Modules are wired here as they land.
# ============================================================================
if(NOT TARGET graphvex)
add_library(graphvex STATIC
    # device — the first word of the language
    src/device/device.c
    # image — the pixel currency
    src/image/image.c
    # filter — the ordered compositor filter stack
    src/filter/filter.c
    src/filter/filter_stack.c
    src/filter/blur/box_blur.c
    src/filter/blur/gaussian_blur.c
    src/filter/contrast/contrast.c
    # vulkan dialect — device only for now (surface/render land next)
    src/vulkan/vk_device.c
    # raster + null dialects — the headless device matrix
    src/raster/raster_device.c
    src/null/null_device.c
    # compositor — the board collage + filter chain
    src/compositor/compositor.c
    # element vocabulary — transform + graphics component + element node
    src/transform/transform.c
    src/component/graphics_component.c
    src/component/component.c
    src/element/element_node.c
    src/element/element_painter.c
    # the element Panel — the first UI element
    src/graphics_panel/graphics_panel.c
    # the element Label — text + the [] formatter + reactive bindings
    src/label/label.c
    # font — the built-in bitmap font (the first text render)
    src/font/font.c
    # the unified Graphics seam — table + paint types + the software row
    src/graphics/graphics.c
    src/paint/brush.c
    src/paint/stroke.c
    src/raster/raster_graphics.c
)

target_include_directories(graphvex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(graphvex PRIVATE -Wall -Wextra -Werror -mcpu=native)

# DEBUG macro gates Vulkan validation layers + debug messenger.
target_compile_definitions(graphvex PRIVATE
    $<$<CONFIG:Debug>:DEBUG=1>
)

# Vulkan headers (the vulkan dialect dlopens the loader; headers are build-time).
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_include_directories(graphvex PUBLIC ${Vulkan_INCLUDE_DIRS})
elseif(EXISTS "/opt/homebrew/include/vulkan/vulkan.h")
    target_include_directories(graphvex PUBLIC /opt/homebrew/include)
endif()

# dlopen (the Vulkan loader) — libSystem on macOS, libdl elsewhere.
if(CMAKE_DL_LIBS)
    target_link_libraries(graphvex PUBLIC ${CMAKE_DL_LIBS})
endif()

if(TARGET vexspoke)
    target_link_libraries(graphvex PUBLIC vexspoke)
endif()

# Apple GPU/bridge frameworks (the objc dialects land here as they port).
if(APPLE)
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

# ----------------------------------------------------------------------------
# Headless tests. The renovation tree ships no test yet; as modules land their
# tests appear under tests/graphvex and are wired here (the Test Segregation
# Law: no tests in the source tree).
# ----------------------------------------------------------------------------
set(GRAPHVEX_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/graphvex")
if(NOT EXISTS "${GRAPHVEX_TEST_DIR}")
    set(GRAPHVEX_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()

# Device + filter smoke: register the dialect/algorithm rows, create a device
# and a filter stack, fold an image. Headless (no window).
if(EXISTS "${GRAPHVEX_TEST_DIR}/device_filter_test.c" AND NOT TARGET device_filter_test)
    add_executable(device_filter_test
        ${GRAPHVEX_TEST_DIR}/device_filter_test.c
    )
    target_link_libraries(device_filter_test PRIVATE graphvex)
    target_compile_options(device_filter_test PRIVATE -Wall -Wextra -Werror)
endif()

# Multi-dialect matrix: vulkan + raster + null registered at once, each created
# through the one Device contract (proves dialects coexist).
if(EXISTS "${GRAPHVEX_TEST_DIR}/device_matrix_test.c" AND NOT TARGET device_matrix_test)
    add_executable(device_matrix_test
        ${GRAPHVEX_TEST_DIR}/device_matrix_test.c
    )
    target_link_libraries(device_matrix_test PRIVATE graphvex)
    target_compile_options(device_matrix_test PRIVATE -Wall -Wextra -Werror)
endif()

# Compositor: board collage + filter chain over two board images (headless).
if(EXISTS "${GRAPHVEX_TEST_DIR}/compositor_test.c" AND NOT TARGET compositor_test)
    add_executable(compositor_test
        ${GRAPHVEX_TEST_DIR}/compositor_test.c
    )
    target_link_libraries(compositor_test PRIVATE graphvex)
    target_compile_options(compositor_test PRIVATE -Wall -Wextra -Werror)
endif()

# Element vocabulary: anchor/pivot/origin resolve + transform + hit-test +
# ElementNode layout (headless).
if(EXISTS "${GRAPHVEX_TEST_DIR}/component_test.c" AND NOT TARGET component_test)
    add_executable(component_test
        ${GRAPHVEX_TEST_DIR}/component_test.c
    )
    target_link_libraries(component_test PRIVATE graphvex)
    target_compile_options(component_test PRIVATE -Wall -Wextra -Werror)
endif()

# Unified Graphics seam: the software row paints rects/circles/images into an
# Image (headless, no GPU).
if(EXISTS "${GRAPHVEX_TEST_DIR}/graphics_test.c" AND NOT TARGET graphics_test)
    add_executable(graphics_test
        ${GRAPHVEX_TEST_DIR}/graphics_test.c
    )
    target_link_libraries(graphics_test PRIVATE graphvex)
    target_compile_options(graphics_test PRIVATE -Wall -Wextra -Werror)
endif()

# UI paint pass: place an ElementNode, paint it through the software row, read
# the pixels back (headless).
if(EXISTS "${GRAPHVEX_TEST_DIR}/element_painter_test.c" AND NOT TARGET element_painter_test)
    add_executable(element_painter_test
        ${GRAPHVEX_TEST_DIR}/element_painter_test.c
    )
    target_link_libraries(element_painter_test PRIVATE graphvex)
    target_compile_options(element_painter_test PRIVATE -Wall -Wextra -Werror)
endif()

# Label: the [] formatter, the reactive re-render, the rendered glyph.
if(EXISTS "${GRAPHVEX_TEST_DIR}/label_test.c" AND NOT TARGET label_test)
    add_executable(label_test
        ${GRAPHVEX_TEST_DIR}/label_test.c
    )
    target_link_libraries(label_test PRIVATE graphvex)
    target_compile_options(label_test PRIVATE -Wall -Wextra -Werror)
endif()

# toString Law: value + one-layer struct dump, nullptr, truncation flag.
if(EXISTS "${GRAPHVEX_TEST_DIR}/tostring_test.c" AND NOT TARGET tostring_test)
    add_executable(tostring_test
        ${GRAPHVEX_TEST_DIR}/tostring_test.c
    )
    target_link_libraries(tostring_test PRIVATE graphvex)
    target_compile_options(tostring_test PRIVATE -Wall -Wextra -Werror)
endif()

# Size: the AUTO FourCC sentinel + the AUTO protocol (predicates, AUTO guard).
if(EXISTS "${GRAPHVEX_TEST_DIR}/size_test.c" AND NOT TARGET size_test)
    add_executable(size_test
        ${GRAPHVEX_TEST_DIR}/size_test.c
    )
    target_link_libraries(size_test PRIVATE graphvex)
    target_compile_options(size_test PRIVATE -Wall -Wextra -Werror)
endif()

# Picture fit contract: STRETCH/CONTAIN/COVER/WINDOW geometry + the two-door
# window spelling + anchors + clamp, then bg fill + fitted draw pixels.
if(EXISTS "${GRAPHVEX_TEST_DIR}/image_fit_test.c" AND NOT TARGET image_fit_test)
    add_executable(image_fit_test
        ${GRAPHVEX_TEST_DIR}/image_fit_test.c
    )
    target_link_libraries(image_fit_test PRIVATE graphvex)
    target_compile_options(image_fit_test PRIVATE -Wall -Wextra -Werror)
endif()
