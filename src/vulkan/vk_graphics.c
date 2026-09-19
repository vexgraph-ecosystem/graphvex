#include "vulkan/vk_graphics.h"

#include "annotation/overview.h"

#include "vulkan/vk.h"

// ;;DRAFT — present/clear/resize are live over the Vk_ seam; the drawable
// verbs defer to Vk_fillRect/Vk_draw* once the seam's frame renderer pass
// is exposed to this row (the Phase-2 darling integration).
// ;;INTENTION("same rationale, per the Two-Semicolon Annotation Style Law")

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkGraphics (vulkan/vk_graphics.c — defined in vulkan/vk_graphics.h)
 * LEVEL: L3 — Module (live GPU backend row behind the Graphics seam)
 * ============================================================================
 * The Vulkan backend row of the unified Graphics seam. Selecting it
 * (Graphics_setGraphics(GRAPHICS_BACKEND_VULKAN) after VkGraphics_0 + the
 * host's Vk_init) makes every drawable call flow through the one Graphics
 * table into the Vulkan loader seam — MoltenVK on macOS, direct
 * swapchains on Windows, one contract. The row carries no self pointer:
 * its implementations read the file-local VkGraphics singleton, matching
 * DirectGraphics.
 *
 * STRUCT FIELDS (Mirroring vulkan/vk_graphics.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   uint32_t width;        // newest native-px drawable extent; 0 until resize
 *   uint32_t height;       // newest native-px drawable extent
 *   uint32_t clearColor;   // staged 0xAARRGGBB for the next demand-present
 *   bool clearPending;     // a clear staged since the last present
 *   bool frameOpen;        // begin() succeeded and end() has not run
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - VkGraphics_0()   : register the process-global singleton (idempotent)
 *
 * Core Functions:
 *   - VkGraphics_getRow() : the const row (NULL until registered)
 *   - VkGraphics_resize() : bind native-px drawable extent (cold path)
 *
 * Getters:
 *   - VkGraphics_getWidth / VkGraphics_getHeight
 *   - VkGraphics_getClearColor / VkGraphics_isClearPending
 *   - VkGraphics_isFrameOpen / VkGraphics_isReady
 *
 * Row implementation (static, behind the table; lifecycle mapping in
 * vk_graphics.h):
 *   - vkBegin    : Vk_ready() gating; opens the frame window
 *   - vkEnd      : closes the frame window (submit lives in the seam present)
 *   - vkPresent  : demand-present via Vk_clearPresent when a clear is staged
 *   - vkResize   : records the newest extent; false on zero/dead (cold)
 *   - vkClear    : stages 0xAARRGGBB into Vk_setClearColor
 *   - vkClip     : ;;DRAFT false (scissor arrives with the darling pass)
 *   - vk* verbs  : ;;DRAFT false (map to Vk_fillRect/Vk_draw* later)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   (none)
 * ============================================================================
 */

// The file-local process-global behind the row (zero steady-state
// allocation, matching DirectGraphics).
static VkGraphics vkGraphics;
static Graphics vkGraphicsRow;
static bool registered;

// ROW IMPLEMENTATION (static, behind the Graphics table)
static bool vkBegin(void) {
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (Vk_ready() == false)
        return false;
    vkGraphics.frameOpen = true;
    return true;
}

static bool vkEnd(void) {
    if (registered == false)
        return false;
    vkGraphics.frameOpen = false;
    return true;
}

static bool vkPresent(void) {
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (Vk_ready() == false)
        return false;
    if (vkGraphics.clearPending == false)
        return true; // the Present-On-Demand Law: rest on the last composite
    vkGraphics.clearPending = false;
    return Vk_clearPresent();
}

static bool vkResize(uint32_t width, uint32_t height) {
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (width == 0 || height == 0)
        return false;
    vkGraphics.width = width;
    vkGraphics.height = height;
    return true;
}

static bool vkClear(uint32_t color) {
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (Vk_ready() == false)
        return false;
    vkGraphics.clearColor = color;
    vkGraphics.clearPending = true;
    Vk_setClearColor(
        (float) ((color >> 16) & 0xFFu) / 255.0f,
        (float) ((color >> 8) & 0xFFu) / 255.0f,
        (float) ((color >> 0) & 0xFFu) / 255.0f,
        (float) ((color >> 24) & 0xFFu) / 255.0f
    );
    return true;
}

static bool vkClip(const Rectangle *rect) {
    // ;;DRAFT — frame-level scissor state needs the seam's render-pass
    // plumbing; arrives with the Phase-2 darling pass.
    (void) rect;
    return false;
}

static bool vkFillRect(const Rectangle *rect, const Brush *brush) {
    // ;;DRAFT — maps to Vk_fillRect inside the open frame renderer pass
    // (Phase-2 darling integration).
    (void) rect;
    (void) brush;
    return false;
}

static bool vkDrawRect(const Rectangle *rect, const Stroke *stroke) {
    // ;;DRAFT — maps to Vk_fillRect (stroke->fill thickness) later.
    (void) rect;
    (void) stroke;
    return false;
}

static bool vkFillCircle(float cx, float cy, float radius, const Brush *brush) {
    // ;;DRAFT — tessellates to a filled fan quad later.
    (void) cx;
    (void) cy;
    (void) radius;
    (void) brush;
    return false;
}

static bool vkDrawCircle(float cx, float cy, float radius, const Stroke *stroke) {
    // ;;DRAFT — ring strip quad later.
    (void) cx;
    (void) cy;
    (void) radius;
    (void) stroke;
    return false;
}

static bool vkFillPath(const Shape *shape, const Brush *brush) {
    // ;;DRAFT — meshlet/tessellation path later.
    (void) shape;
    (void) brush;
    return false;
}

static bool vkDrawPath(const Shape *shape, const Stroke *stroke) {
    // ;;DRAFT — stroke expansion to quads later.
    (void) shape;
    (void) stroke;
    return false;
}

static bool vkDrawImage(const Image *image, const Rectangle *dst) {
    // ;;DRAFT — maps to Vk_drawTexture (bindless) later.
    (void) image;
    (void) dst;
    return false;
}

// CONSTRUCTORS
VkGraphics *VkGraphics_0(void) {
    if (registered == false) {
        static const Graphics row = {
            GRAPHICS_BACKEND_VULKAN,
            vkBegin, vkEnd, vkPresent, vkResize,
            vkClear, vkClip,
            vkFillRect, vkDrawRect, vkFillCircle, vkDrawCircle,
            vkFillPath, vkDrawPath, vkDrawImage
        };
        vkGraphicsRow = row;
        registered = true;
    }
    return &vkGraphics;
}

// CORE FUNCTIONS
const Graphics *VkGraphics_getRow(void) {
    if (registered == false)
        return nullptr;
    return &vkGraphicsRow;
}

bool VkGraphics_resize(uint32_t width, uint32_t height) {
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (width == 0 || height == 0)
        return false;
    vkGraphics.width = width;
    vkGraphics.height = height;
    return true;
}

// GETTERS
uint32_t VkGraphics_getWidth(void) {
    return registered ? vkGraphics.width : 0u;
}

uint32_t VkGraphics_getHeight(void) {
    return registered ? vkGraphics.height : 0u;
}

uint32_t VkGraphics_getClearColor(void) {
    return registered ? vkGraphics.clearColor : 0u;
}

bool VkGraphics_isClearPending(void) {
    return registered && vkGraphics.clearPending;
}

bool VkGraphics_isFrameOpen(void) {
    return registered && vkGraphics.frameOpen;
}

bool VkGraphics_isReady(void) {
    return registered && Vk_ready() && !Vk_isDeviceLost();
}