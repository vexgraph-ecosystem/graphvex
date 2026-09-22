#include "vulkan/vk_graphics.h"

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

#include "vulkan/vk.h"

// ;;INTENTION("fillRect/drawRect are live over the Vk_fillRect quad primitive;
// circles/path/image await their tessellation / bindless-texture plumbing and
// cold-return false, per the Two-Semicolon Annotation Style Law")

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VkGraphics
 * ============================================================================
 * Live hardware Vulkan backend row fulfilling the unified Graphics seam.
 * Adapts the Vulkan loader and swapchain presentation pipeline into the
 * uniform Graphics vtable in compliance with the Unified Graphics Abstraction Law
 * and the Strict 0xRRGGBBAA Color Law.
 *
 * Encodes all viewport clearing and blit operations strictly in 0xRRGGBBAA:
 * channel 0 (red) extracts from bits 24..31, channel 1 (green) from bits 16..23,
 * channel 2 (blue) from bits 8..15, and channel 3 (alpha) from bits 0..7.
 * Drawable verbs record into the live command buffer bound by
 * VkGraphics_bindFrame inside the host's frame-renderer pass (the seam's
 * Vk_clearPresent invokes that callback INSIDE the swapchain render pass, so
 * recording here is recording on screen: one table verb per quad).
 * ============================================================================
 */

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
 * RasterGraphics.
 *
 * STRUCT FIELDS (Mirroring vulkan/vk_graphics.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   uint32_t width;        // newest native-px drawable extent; 0 until resize
 *   uint32_t height;       // newest native-px drawable extent
 *   uint32_t clearColor;   // staged 0xRRGGBBAA for the next demand-present
 *   bool clearPending;     // a clear staged since the last present
 *   bool frameOpen;        // begin() succeeded and end() has not run
 *   void *boundCmdBuffer;  // live seam command buffer (VkGraphics_bindFrame)
 *   uint32_t boundW;       // drawable extent of the bound pass, native px
 *   uint32_t boundH;       // drawable extent of the bound pass, native px
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VkGraphics_0(void)                      : Register process-global singleton
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - VkGraphics_getRow(void)                 : Query const Graphics row table
 *   - VkGraphics_resize(width, height)        : Bind native-px drawable extent
 *   - VkGraphics_bindFrame(cb, w, h)          : Bind live seam render-pass command buffer
 *
 * Private Core Functions: (.c static)
 *   - vkBegin(void)                           : Gate readiness and open frame
 *   - vkEnd(void)                             : Close frame window, release bound buffer
 *   - vkPresent(void)                         : Demand-present clear to swapchain
 *   - vkResize(width, height)                 : Record latest extent
 *   - vkClear(color)                          : Stage 0xRRGGBBAA clear into seam
 *   - vkClip(rect)                            : Update scissor clip (draft)
 *   - vkFillRect(rect, brush)                 : Solid rectangle fill (LIVE — Vk_fillRect)
 *   - vkDrawRect(rect, stroke)                : Rectangle stroke (LIVE — Vk_fillRect bars)
 *   - vkFillCircle(cx, cy, radius, brush)     : Circle fill (draft)
 *   - vkDrawCircle(cx, cy, radius, stroke)    : Circle stroke (draft)
 *   - vkFillPath(shape, brush)                : Path fill (draft)
 *   - vkDrawPath(shape, stroke)               : Path stroke (draft)
 *   - vkDrawImage(image, dst)                 : Image draw (draft)
 *
 * Public Setters: (.h)
 *   - (none)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - VkGraphics_getWidth(void)               : Query drawable width
 *   - VkGraphics_getHeight(void)              : Query drawable height
 *   - VkGraphics_getClearColor(void)          : Query staged clear color
 *   - VkGraphics_isClearPending(void)         : Check if clear is pending
 *   - VkGraphics_isFrameOpen(void)            : Check if frame is open
 *   - VkGraphics_isReady(void)                : Check readiness
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// The file-local process-global behind the row (zero steady-state
// allocation, matching RasterGraphics).
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
    vkGraphics.boundCmdBuffer = nullptr;
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
    // 0xRRGGBBAA law decode (the Strict 0xRRGGBBAA Color Law): red = bits
    // 24..31, green = bits 16..23, blue = bits 8..15, alpha = bits 0..7.
    Vk_setClearColor(
        (float) ((color >> 24) & 0xFFu) / 255.0f,
        (float) ((color >> 16) & 0xFFu) / 255.0f,
        (float) ((color >> 8) & 0xFFu) / 255.0f,
        (float) (color & 0xFFu) / 255.0f
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
    // LIVE: the seam's VkGraphics_bindFrame hands the open swapchain render
    // pass to this row; Vk_fillRect records the quad at drawable-pixel
    // coords with its own scissor (the exact primitive the Panel rows paint).
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (Vk_ready() == false)
        return false;
    if (vkGraphics.frameOpen == false || vkGraphics.boundCmdBuffer == nullptr)
        return false;
    if (rect == nullptr || brush == nullptr)
        return false;
    if ((*rect).width <= 0.0f || (*rect).height <= 0.0f)
        return false;
    // 0xRRGGBBAA law decode (the Strict 0xRRGGBBAA Color Law): red = bits
    // 24..31, green = bits 16..23, blue = bits 8..15; alpha = bits 0..7
    // modulated by brush opacity (clamped 0..1).
    float op = (*brush).opacity;
    if (op < 0.0f)
        op = 0.0f;
    if (op > 1.0f)
        op = 1.0f;
    uint32_t color = (*brush).color;
    float a = (float) ((color & 0xFFu) * op) / 255.0f;
    Vk_fillRect(vkGraphics.boundCmdBuffer,
                (float) vkGraphics.boundW, (float) vkGraphics.boundH,
                (*rect).x, (*rect).y, (*rect).width, (*rect).height,
                (float) ((color >> 24) & 0xFFu) / 255.0f,
                (float) ((color >> 16) & 0xFFu) / 255.0f,
                (float) ((color >> 8) & 0xFFu) / 255.0f,
                a);
    return true;
}

static bool vkDrawRect(const Rectangle *rect, const Stroke *stroke) {
    // LIVE: the same 4-bar model RasterGraphics implDrawRect uses — top,
    // bottom, left, right bands recorded through Vk_fillRect. Hairline
    // (width < 1) draws a 1px band, matching the Raster row.
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (Vk_ready() == false)
        return false;
    if (vkGraphics.frameOpen == false || vkGraphics.boundCmdBuffer == nullptr)
        return false;
    if (rect == nullptr || stroke == nullptr)
        return false;
    if ((*rect).width <= 0.0f || (*rect).height <= 0.0f)
        return false;
    float wf = (*stroke).width;
    if (wf < 1.0f)
        wf = 1.0f;
    float th = (float) (int32_t) (wf + 0.5f);
    float x0 = (*rect).x;
    float y0 = (*rect).y;
    float x1 = (*rect).x + (*rect).width;
    float y1 = (*rect).y + (*rect).height;
    uint32_t color = (*stroke).color;
    float r = (float) ((color >> 24) & 0xFFu) / 255.0f;
    float g = (float) ((color >> 16) & 0xFFu) / 255.0f;
    float b = (float) ((color >> 8) & 0xFFu) / 255.0f;
    float a = (float) (color & 0xFFu) / 255.0f;
    Vk_fillRect(vkGraphics.boundCmdBuffer,
                (float) vkGraphics.boundW, (float) vkGraphics.boundH,
                x0, y0, x1 - x0, th, r, g, b, a);
    Vk_fillRect(vkGraphics.boundCmdBuffer,
                (float) vkGraphics.boundW, (float) vkGraphics.boundH,
                x0, y1 - th, x1 - x0, th, r, g, b, a);
    Vk_fillRect(vkGraphics.boundCmdBuffer,
                (float) vkGraphics.boundW, (float) vkGraphics.boundH,
                x0, y0 + th, th, y1 - y0 - 2.0f * th, r, g, b, a);
    Vk_fillRect(vkGraphics.boundCmdBuffer,
                (float) vkGraphics.boundW, (float) vkGraphics.boundH,
                x1 - th, y0 + th, th, y1 - y0 - 2.0f * th, r, g, b, a);
    return true;
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

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

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

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

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

bool VkGraphics_bindFrame(void *cmdBuffer, uint32_t width, uint32_t height) {
    if (registered == false || Vk_isDeviceLost())
        return false;
    if (cmdBuffer == nullptr || width == 0 || height == 0)
        return false;
    vkGraphics.boundCmdBuffer = cmdBuffer;
    vkGraphics.boundW = width;
    vkGraphics.boundH = height;
    vkGraphics.frameOpen = true;
    return true;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
uint32_t VkGraphics_getWidth(void) {
    return registered ? vkGraphics.width : 0u;
}

;;GETTER
uint32_t VkGraphics_getHeight(void) {
    return registered ? vkGraphics.height : 0u;
}

;;GETTER
uint32_t VkGraphics_getClearColor(void) {
    return registered ? vkGraphics.clearColor : 0u;
}

;;GETTER
bool VkGraphics_isClearPending(void) {
    return registered && vkGraphics.clearPending;
}

;;GETTER
bool VkGraphics_isFrameOpen(void) {
    return registered && vkGraphics.frameOpen;
}

;;GETTER
bool VkGraphics_isReady(void) {
    return registered && Vk_ready() && !Vk_isDeviceLost();
}