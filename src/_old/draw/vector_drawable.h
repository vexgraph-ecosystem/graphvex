#ifndef DRAW_VECTOR_DRAWABLE_H
#define DRAW_VECTOR_DRAWABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "draw/drawable.h"
#include "../graphics/type.h"
#include "paint/brush.h"
#include "paint/stroke.h"
#include "vector/shape.h"

// draw/vector_drawable.h — Infinite-canvas vector board (commands + camera pan/zoom).
//
// A VectorDrawable records vector commands (rect, circle, path with brush/stroke)
// in float world coordinates with camera pan and zoom transformations.
// Commands can be cleared or rendered onto a raster Drawable board.

#define VECTOR_CMD_FILL_RECT   1u
#define VECTOR_CMD_DRAW_RECT   2u
#define VECTOR_CMD_FILL_CIRCLE 3u
#define VECTOR_CMD_DRAW_CIRCLE 4u
#define VECTOR_CMD_FILL_PATH   5u
#define VECTOR_CMD_DRAW_PATH   6u

// Slot Record: single recorded vector command row.
// VectorCommand borrows Shape via const Shape *shape (borrowed view semantics
// per Darling source model, caller retains, never freed by VectorDrawable).
typedef struct VectorCommand {
    uint32_t kind;      // command kind (VECTOR_CMD_*)
    float x;            // rect origin X
    float y;            // rect origin Y
    float w;            // rect width
    float h;            // rect height
    float cx;           // circle center X
    float cy;           // circle center Y
    float r;            // circle radius
    const Shape *shape; // borrowed view of path geometry (caller retains, never freed by VectorDrawable)
    Brush brush;        // fill brush style
    Stroke stroke;      // stroke style
} VectorCommand;

typedef struct VectorDrawable {
    VectorCommand *commands; // dynamic array of recorded vector commands
    size_t commandCount;     // number of recorded commands
    size_t commandCapacity;  // allocated command array capacity
    float panX;              // camera world pan X offset
    float panY;              // camera world pan Y offset
    float zoom;              // camera zoom scaling factor
    uint32_t width;          // canvas pixel width
    uint32_t height;         // canvas pixel height
    bool dirty;              // true once any vector command recorded
    uint64_t typeId;         // block-header type id (TYPE_VECTOR_DRAWABLE_SINGLETON)
} VectorDrawable;

// Constructors
VectorDrawable *VectorDrawable_0(void);
VectorDrawable *VectorDrawable_2(uint32_t w, uint32_t h);

// Core functions
void VectorDrawable_free(VectorDrawable *self);
void VectorDrawable_fillRect(VectorDrawable *self, float x, float y, float w, float h, const Brush *brush);
void VectorDrawable_drawRect(VectorDrawable *self, float x, float y, float w, float h, const Stroke *stroke);
void VectorDrawable_fillCircle(VectorDrawable *self, float cx, float cy, float r, const Brush *brush);
void VectorDrawable_drawCircle(VectorDrawable *self, float cx, float cy, float r, const Stroke *stroke);
void VectorDrawable_fillPath(VectorDrawable *self, const Shape *shape, const Brush *brush);
void VectorDrawable_drawPath(VectorDrawable *self, const Shape *shape, const Stroke *stroke);
void VectorDrawable_clear(VectorDrawable *self);
void VectorDrawable_render(VectorDrawable *self, Drawable *dest);

// Symmetric Setters
void VectorDrawable_setPan(VectorDrawable *self, float panX, float panY);
void VectorDrawable_setPanX(VectorDrawable *self, float panX);
void VectorDrawable_setPanY(VectorDrawable *self, float panY);
void VectorDrawable_setZoom(VectorDrawable *self, float zoom);
void VectorDrawable_setSize(VectorDrawable *self, uint32_t w, uint32_t h);
void VectorDrawable_setWidth(VectorDrawable *self, uint32_t width);
void VectorDrawable_setHeight(VectorDrawable *self, uint32_t height);
void VectorDrawable_setDirty(VectorDrawable *self, bool dirty);

// Symmetric Getters
void VectorDrawable_getPan(const VectorDrawable *self, float *outPanX, float *outPanY);
float VectorDrawable_getPanX(const VectorDrawable *self);
float VectorDrawable_getPanY(const VectorDrawable *self);
float VectorDrawable_getZoom(const VectorDrawable *self);
uint32_t VectorDrawable_getWidth(const VectorDrawable *self);
uint32_t VectorDrawable_getHeight(const VectorDrawable *self);
bool VectorDrawable_isDirty(const VectorDrawable *self);
size_t VectorDrawable_getCommandCount(const VectorDrawable *self);
uint64_t VectorDrawable_getTypeId(const VectorDrawable *self);

#define VectorDrawable(...) CONSTRUCTOR_DISPATCH(VectorDrawable, __VA_ARGS__)

#endif
