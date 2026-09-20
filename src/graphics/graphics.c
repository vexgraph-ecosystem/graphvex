#include "graphics/graphics.h"

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Graphics
 * ============================================================================
 * Unified Graphics runtime backend dispatch seam decoupling client drawing calls
 * from hardware rendering drivers. Encapsulates a static dispatch table populated
 * by hardware drivers (VkGraphics, MetalGraphics, RasterGraphics) through a single
 * row copy. Forwarder routines invoke corresponding driver functions, enforcing the
 * Pixel Coordinate Contract where client code submits native window coordinates while
 * normalized device coordinates are constrained strictly to vertex pipeline stages.
 * Operates under the Strict 0xRRGGBBAA Color Law (bits 31..24 red, 23..16 green, 15..8 blue,
 * 7..0 alpha) across all clear operations, ensuring identical cross-backend color presentation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Graphics (graphics/graphics.c — defined in graphics/graphics.h)
 * LEVEL: L3 — Module (backend seam the drivers stand behind)
 * ============================================================================
 * The unified Graphics seam: one function-pointer table is the whole
 * backend contract. A backend class (VkGraphics, MetalGraphics,
 * RasterGraphics) exports one const row; Graphics_setGraphics copies it
 * into the file-static `currentGraphics` context; every Graphics_*
 * forwarder calls through that context. Renderer switching is one line
 * (the Pixel Coordinate Contract: native pixels out, NDC only inside
 * vertex shaders). The graphvex/drawable.h CPU stubs remain the analysis
 * surface; this seam is the runtime surface backends actually implement.
 *
 * STRUCT FIELDS (Mirroring graphics/graphics.h):
 * ----------------------------------------------------------------------------
 *   uint32_t backendId;                      // GRAPHICS_BACKEND_* self-reported by the row
 *   bool (*begin)(void);                     // start recording a frame
 *   bool (*end)(void);                       // flush/submit the frame
 *   bool (*present)(void);                   // swap/present the frame
 *   bool (*resize)(uint32_t w, uint32_t h);  // native px, cold path
 *   bool (*clear)(uint32_t color);           // 0xRRGGBBAA full-drawable clear
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
 * Public Constructors: (.h)
 *   - (none)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Graphics_begin(void)                                 : Start recording frame
 *   - Graphics_end(void)                                   : Flush/submit frame
 *   - Graphics_present(void)                               : Swap/present frame
 *   - Graphics_resize(width, height)                       : Resize surface
 *   - Graphics_clear(color)                                : Clear surface (0xRRGGBBAA)
 *   - Graphics_clip(rect)                                  : Scissor clip rectangle
 *   - Graphics_fillRect(rect, brush)                       : Fill rectangle
 *   - Graphics_drawRect(rect, stroke)                      : Draw rectangle outline
 *   - Graphics_fillCircle(cx, cy, radius, brush)           : Fill circle
 *   - Graphics_drawCircle(cx, cy, radius, stroke)          : Draw circle outline
 *   - Graphics_fillPath(shape, brush)                      : Fill path
 *   - Graphics_drawPath(shape, stroke)                     : Draw path outline
 *   - Graphics_drawImage(image, dst)                       : Blit image
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Graphics_setGraphics(backendId)                      : Copy row into currentGraphics
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Graphics_getGraphicsId(void)                         : Active backend ID (0 = none)
 *   - Graphics_getCurrent(void)                            : Active row pointer
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// The active table. Zero-initialized: every row entry is NULL until
// Graphics_setGraphics succeeds, so every forwarder cold-returns false.
static Graphics currentGraphics;

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

// (none — context is file-static)

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

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

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
bool Graphics_setGraphics(uint32_t backendId) {
    const Graphics *row = nullptr;
    switch (backendId) {
        case GRAPHICS_BACKEND_RASTER:
            row = RasterGraphics_getRow();
            break;
        case GRAPHICS_BACKEND_VULKAN:
            row = VkGraphics_getRow();
            break;
        case GRAPHICS_BACKEND_METAL:
            row = MetalGraphics_getRow();
            break;
        default:
            break;
    }
    if (row == nullptr)
        return false;
    currentGraphics = *row;
    return true;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
uint32_t Graphics_getGraphicsId(void) {
    return currentGraphics.backendId;
}

;;GETTER
const Graphics *Graphics_getCurrent(void) {
    return &currentGraphics;
}