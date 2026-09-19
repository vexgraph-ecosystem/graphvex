#include "metal/metal_graphics.h"

#include "annotation/overview.h"

// ;;INCOMPLETE — Metal is the secondary backend: macOS renders Vulkan
// through MoltenVK today, so this row only registers and selects. Every
// verb and device call cold-returns false until the engine takes shape and
// this pair becomes metal_graphics.m with CAMetalLayer wiring.

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: MetalGraphics (metal/metal_graphics.c — defined in
 *                          metal/metal_graphics.h)
 * LEVEL: L3 — Module (secondary backend row behind the Graphics seam)
 * ============================================================================
 * The Metal row of the unified Graphics seam: registration + selection
 * work (setGraphics with GRAPHICS_BACKEND_METAL succeeds once
 * MetalGraphics_0 runs), everything else cold-false (;;INCOMPLETE). The
 * struct mirrors VkGraphics field-for-field so the native-CAMetalLayer
 * implementation can take shape without an API break.
 *
 * STRUCT FIELDS (Mirroring metal/metal_graphics.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   uint32_t width;        // newest native-px drawable extent; 0 (stub)
 *   uint32_t height;       // newest native-px drawable extent
 *   uint32_t clearColor;   // staged 0xAARRGGBB; 0 (stub)
 *   bool clearPending;     // unused (stub)
 *   bool frameOpen;        // unused (stub)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - MetalGraphics_0() : register the process-global singleton (idempotent)
 *
 * Core Functions:
 *   - MetalGraphics_getRow() : the const row (NULL until registered)
 *
 * Getters:
 *   - MetalGraphics_getWidth / getHeight / getClearColor
 *   - MetalGraphics_isClearPending / isFrameOpen / isReady
 *
 * Row implementation (static, behind the table):
 *   - metalBegin/End/Present/Resize : false (;;INCOMPLETE)
 *   - metalClear/Clip + verbs       : false (;;INCOMPLETE)
 * ============================================================================
 */

// The file-local process-global behind the row.
static MetalGraphics metalGraphics;
static Graphics metalGraphicsRow;
static bool registered;

// ROW IMPLEMENTATION (static, behind the Graphics table; ;;INCOMPLETE)
static bool metalBegin(void) {
    return false;
}

static bool metalEnd(void) {
    return false;
}

static bool metalPresent(void) {
    return false;
}

static bool metalResize(uint32_t width, uint32_t height) {
    (void) width;
    (void) height;
    return false;
}

static bool metalClear(uint32_t color) {
    (void) color;
    return false;
}

static bool metalClip(const Rectangle *rect) {
    (void) rect;
    return false;
}

static bool metalFillRect(const Rectangle *rect, const Brush *brush) {
    (void) rect;
    (void) brush;
    return false;
}

static bool metalDrawRect(const Rectangle *rect, const Stroke *stroke) {
    (void) rect;
    (void) stroke;
    return false;
}

static bool metalFillCircle(float cx, float cy, float radius, const Brush *brush) {
    (void) cx;
    (void) cy;
    (void) radius;
    (void) brush;
    return false;
}

static bool metalDrawCircle(float cx, float cy, float radius, const Stroke *stroke) {
    (void) cx;
    (void) cy;
    (void) radius;
    (void) stroke;
    return false;
}

static bool metalFillPath(const Shape *shape, const Brush *brush) {
    (void) shape;
    (void) brush;
    return false;
}

static bool metalDrawPath(const Shape *shape, const Stroke *stroke) {
    (void) shape;
    (void) stroke;
    return false;
}

static bool metalDrawImage(const Image *image, const Rectangle *dst) {
    (void) image;
    (void) dst;
    return false;
}

// CONSTRUCTORS
MetalGraphics *MetalGraphics_0(void) {
    if (registered == false) {
        static const Graphics row = {
            GRAPHICS_BACKEND_METAL,
            metalBegin, metalEnd, metalPresent, metalResize,
            metalClear, metalClip,
            metalFillRect, metalDrawRect, metalFillCircle, metalDrawCircle,
            metalFillPath, metalDrawPath, metalDrawImage
        };
        metalGraphicsRow = row;
        registered = true;
    }
    return &metalGraphics;
}

// CORE FUNCTIONS
const Graphics *MetalGraphics_getRow(void) {
    if (registered == false)
        return nullptr;
    return &metalGraphicsRow;
}

// GETTERS
uint32_t MetalGraphics_getWidth(void) {
    return registered ? metalGraphics.width : 0u;
}

uint32_t MetalGraphics_getHeight(void) {
    return registered ? metalGraphics.height : 0u;
}

uint32_t MetalGraphics_getClearColor(void) {
    return registered ? metalGraphics.clearColor : 0u;
}

bool MetalGraphics_isClearPending(void) {
    return registered && metalGraphics.clearPending;
}

bool MetalGraphics_isFrameOpen(void) {
    return registered && metalGraphics.frameOpen;
}

bool MetalGraphics_isReady(void) {
    return registered;
}