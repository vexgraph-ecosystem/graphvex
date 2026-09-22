#ifndef GRAPHVEX_EXCEPTION_GRAPHICS_EXCEPTION_H
#define GRAPHVEX_EXCEPTION_GRAPHICS_EXCEPTION_H

#include "exception/exception.h"
#include "graphvex/type.h"

typedef enum GraphicsErrorCode {
    GRAPHICS_SUCCESS = 0,
    GRAPHICS_ERROR_PIPELINE_CREATION_FAILED,
    GRAPHICS_ERROR_SHADER_MODULE_FAILED,
    GRAPHICS_ERROR_SWAPCHAIN_CREATION_FAILED,
    GRAPHICS_ERROR_DEVICE_LOST,
    GRAPHICS_ERROR_INVALID_DIMENSIONS,
    GRAPHICS_ERROR_LAYER_PRESENT_FAILED,
    GRAPHICS_ERROR_SCENE_NOT_READY,
    GRAPHICS_ERROR_UNKNOWN
} GraphicsErrorCode;

typedef struct GraphicsException {
    Exception base;
    GraphicsErrorCode code;
    int32_t vkResult;
} GraphicsException;

const char *GraphicsErrorCode_name(GraphicsErrorCode code);

void GraphicsException_init(GraphicsException *self,
                           GraphicsErrorCode code,
                           int32_t vkResult,
                           const char *site,
                           const char *file,
                           int line,
                           const char *fmt,
                           ...);

void GraphicsException_free(GraphicsException *self);

void GraphicsException_print(const GraphicsException *self);

#endif // GRAPHVEX_EXCEPTION_GRAPHICS_EXCEPTION_H
