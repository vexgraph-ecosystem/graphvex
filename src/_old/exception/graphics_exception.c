#include "graphics_exception.h"

#include <stdarg.h>
#include <stdio.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphicsException
 * ============================================================================
 * Specialized graphics error handling structure extending the fundamental Exception
 * model. Encapsulates categorized engine failure codes (such as pipeline creation
 * failures, shader compilation issues, swapchain breakage, or lost device state)
 * along with raw Vulkan or hardware driver integer result codes. Formats callsite
 * telemetry, source file locations, line markers, and contextual diagnostic messages
 * into unified debug output streams.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GraphicsException (graphvex/src/graphvex/exception/graphics_exception.c)
 * LEVEL: L2 — Behavior (graphics subsystem exception handling)
 * ============================================================================
 * Graphics error state tracking combining high-level engine error codes with
 * low-level backend driver result codes.
 *
 * STRUCT FIELDS (Mirroring graphics_exception.h):
 * ----------------------------------------------------------------------------
 *   GraphicsException {
 *     Exception base;           // base exception structure
 *     GraphicsErrorCode code;   // categorized engine error code
 *     int32_t vkResult;         // low-level backend driver status code
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - GraphicsException_init(self, code, vkResult, site, file, line, fmt, ...) : Initialize exception
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - GraphicsException_free(self)                                             : Release exception
 *   - GraphicsException_print(self)                                            : Print diagnostic info
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - (none)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - GraphicsErrorCode_name(code)                                             : Query error name string
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

void GraphicsException_init(GraphicsException *self,
                            GraphicsErrorCode code,
                            int32_t vkResult,
                            const char *site,
                            const char *file,
                            int line,
                            const char *fmt,
                            ...) {
    if (self == NULL) return;

    (*self).code = code;
    (*self).vkResult = vkResult;

    va_list args;
    va_start(args, fmt);
    Exception_initV(&((*self).base), EXCEPTION_GRAPHICS, site, file, line, fmt, args);
    va_end(args);

    (*self).base.type = TYPE_GRAPHICS_EXCEPTION_SINGLETON;

    Exception_setDetails(&((*self).base), "GraphicsErrorCode: %s (%d), VkResult: %d",
                         GraphicsErrorCode_name(code), (int)code, (int)vkResult);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void GraphicsException_free(GraphicsException *self) {
    if (self == NULL) return;
    Exception_free(&((*self).base));
}

void GraphicsException_print(const GraphicsException *self) {
    if (self == NULL) return;
    Exception_print(&((*self).base));
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
const char *GraphicsErrorCode_name(GraphicsErrorCode code) {
    switch (code) {
        case GRAPHICS_SUCCESS:
            return "GRAPHICS_SUCCESS";
        case GRAPHICS_ERROR_PIPELINE_CREATION_FAILED:
            return "GRAPHICS_ERROR_PIPELINE_CREATION_FAILED";
        case GRAPHICS_ERROR_SHADER_MODULE_FAILED:
            return "GRAPHICS_ERROR_SHADER_MODULE_FAILED";
        case GRAPHICS_ERROR_SWAPCHAIN_CREATION_FAILED:
            return "GRAPHICS_ERROR_SWAPCHAIN_CREATION_FAILED";
        case GRAPHICS_ERROR_DEVICE_LOST:
            return "GRAPHICS_ERROR_DEVICE_LOST";
        case GRAPHICS_ERROR_INVALID_DIMENSIONS:
            return "GRAPHICS_ERROR_INVALID_DIMENSIONS";
        case GRAPHICS_ERROR_LAYER_PRESENT_FAILED:
            return "GRAPHICS_ERROR_LAYER_PRESENT_FAILED";
        case GRAPHICS_ERROR_SCENE_NOT_READY:
            return "GRAPHICS_ERROR_SCENE_NOT_READY";
        case GRAPHICS_ERROR_UNKNOWN:
        default:
            return "GRAPHICS_ERROR_UNKNOWN";
    }
}
