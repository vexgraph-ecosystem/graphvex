#include "graphics/graphics.h"

#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Graphics (graphics/graphics.c — defined in graphics/graphics.h)
 * LEVEL: L3 — Module (backend seam the drivers stand behind)
 * ============================================================================
 * The unified Graphics seam: one function-pointer table is the whole
 * backend contract. A backend class (VkGraphics, MetalGraphics,
 * DirectGraphics) exports one const row; Graphics_setGraphics copies it
 * into the file-static `currentGraphics` context; every Graphics_*
 * forwarder calls through that context. Renderer switching is one line
 * (the Pixel Coordinate Contract: native pixels out, NDC only inside
 * vertex shaders). The graphvex/drawable.h CPU stubs remain the analysis
 * surface; this seam is the runtime surface backends actually implement.
 *
 * STRUCT FIELDS (Mirroring graphics/graphics.h):
 * ----------------------------------------------------------------------------
 *   uint32_t backendId;          // GRAPHICS_BACKEND_* self-reported by the row
 *   bool (*begin)(void);         // start recording a frame
 *   bool (*end)(void);           // flush/submit the frame
 *   bool (*present)(void);       // swap/present the frame
 *   bool (*resize)(uint32_t w, uint32_t h);  // native px, cold path
 *   bool (*clear)(uint32_t color);           // 0xAARRGGBB full-drawable clear
 *   bool (*clip)(const Rectangle *rect);     // scissor; NULL = reset
 *   bool (*fillRect)(const Rectangle *rect, const Brush *brush);
 *   bool (*drawRect)(const Rectangle *rect, const Stroke *stroke);
 *   bool (*fillCircle)(float cx, float cy, float radius, const Brush *brush);
 *   bool (*drawCircle)(float cx, float cy, float radius, const Stroke *stroke);
 *   bool (*fillPath)(const Shape *shape, const Brush *brush);
 *   bool (*drawPath)(const Shape *shape, const Stroke *stroke);
 *   bool (*drawImage)(const Image *image, const Rectangle *dst);
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - (none — the context is file-static; selection replaces the row)
 *
 * Core Functions:
 *   - Graphics_setGraphics(backendId) : copy row into currentGraphics
 *   - Graphics_getGraphicsId()        : 0 = none selected
 *   - Graphics_getCurrent()           : active row (never null)
 *
 * Forwarders (null-guarded; false while no backend is selected):
 *   - Graphics_begin / Graphics_end / Graphics_present
 *   - Graphics_resize(width, height)
 *   - Graphics_clear(color) / Graphics_clip(rect)
 *   - Graphics_fillRect / Graphics_drawRect
 *   - Graphics_fillCircle / Graphics_drawCircle
 *   - Graphics_fillPath / Graphics_drawPath
 *   - Graphics_drawImage(image, dst)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   (none)
 * ============================================================================
 */

// The active table. Zero-initialized: every row entry is NULL until
// Graphics_setGraphics succeeds, so every forwarder cold-returns false.
static Graphics currentGraphics;

// CONSTRUCTORS
// (none — see FUNCTION REGISTRY)

// CORE FUNCTIONS
bool Graphics_setGraphics(uint32_t backendId) {
    const Graphics *row = nullptr;
    switch (backendId) {
        case GRAPHICS_BACKEND_DIRECT:
            row = DirectGraphics_getRow();
            break;
        case GRAPHICS_BACKEND_VULKAN:
        case GRAPHICS_BACKEND_METAL:
            // Rows land in their own feature commits (Cohesive Commits Law);
            // until then selection cold-returns false.
            break;
        default:
            break;
    }
    if (row == nullptr)
        return false;
    currentGraphics = *row;
    return true;
}

uint32_t Graphics_getGraphicsId(void) {
    return currentGraphics.backendId;
}

const Graphics *Graphics_getCurrent(void) {
    return &currentGraphics;
}

// FORWARDERS
bool Graphics_begin(void) {
    return currentGraphics.begin ? currentGraphics.begin() : false;
}

bool Graphics_end(void) {
    return currentGraphics.end ? currentGraphics.end() : false;
}

bool Graphics_present(void) {
    return currentGraphics.present ? currentGraphics.present() : false;
}

bool Graphics_resize(uint32_t width, uint32_t height) {
    return currentGraphics.resize ? currentGraphics.resize(width, height) : false;
}

bool Graphics_clear(uint32_t color) {
    return currentGraphics.clear ? currentGraphics.clear(color) : false;
}

bool Graphics_clip(const Rectangle *rect) {
    return currentGraphics.clip ? currentGraphics.clip(rect) : false;
}

bool Graphics_fillRect(const Rectangle *rect, const Brush *brush) {
    return currentGraphics.fillRect ? currentGraphics.fillRect(rect, brush) : false;
}

bool Graphics_drawRect(const Rectangle *rect, const Stroke *stroke) {
    return currentGraphics.drawRect ? currentGraphics.drawRect(rect, stroke) : false;
}

bool Graphics_fillCircle(float cx, float cy, float radius, const Brush *brush) {
    return currentGraphics.fillCircle ? currentGraphics.fillCircle(cx, cy, radius, brush) : false;
}

bool Graphics_drawCircle(float cx, float cy, float radius, const Stroke *stroke) {
    return currentGraphics.drawCircle ? currentGraphics.drawCircle(cx, cy, radius, stroke) : false;
}

bool Graphics_fillPath(const Shape *shape, const Brush *brush) {
    return currentGraphics.fillPath ? currentGraphics.fillPath(shape, brush) : false;
}

bool Graphics_drawPath(const Shape *shape, const Stroke *stroke) {
    return currentGraphics.drawPath ? currentGraphics.drawPath(shape, stroke) : false;
}

bool Graphics_drawImage(const Image *image, const Rectangle *dst) {
    return currentGraphics.drawImage ? currentGraphics.drawImage(image, dst) : false;
}