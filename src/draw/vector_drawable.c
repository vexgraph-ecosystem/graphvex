#include "draw/vector_drawable.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VectorDrawable
 * ============================================================================
 * Resolution-independent infinite-canvas vector drawing board maintaining an
 * ordered stream of vector rendering commands.
 *
 * Records geometric paths, shapes, radial circles, and styled strokes into a dynamically
 * scalable command buffer with integrated camera pan and zoom affine transforms.
 * Renders into any target raster board with hardware-independent precision in compliance
 * with the Unified Graphics Abstraction Law and the Dynamic Scalability & Anti-Hardcoding Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VectorDrawable (draw/vector_drawable.c)
 * LEVEL: L3 — Module Code (infinite-canvas vector board behavior)
 * ============================================================================
 * Infinite-canvas vector board: Shape + Stroke/Brush list, float world
 * coordinates, and camera pan/zoom. Records vector commands into an owned
 * dynamic array of VectorCommand slot records, which can be rendered to any
 * raster Drawable board with pan/zoom camera transform applied.
 *
 * STRUCT FIELDS (Mirroring draw/vector_drawable.h):
 * ----------------------------------------------------------------------------
 *   VectorDrawable {
 *     VectorCommand *commands; // dynamic array of recorded vector commands
 *     size_t commandCount;     // number of recorded commands
 *     size_t commandCapacity;  // allocated command array capacity
 *     float panX;              // camera world pan X offset
 *     float panY;              // camera world pan Y offset
 *     float zoom;              // camera zoom scaling factor
 *     uint32_t width;          // canvas pixel width
 *     uint32_t height;         // canvas pixel height
 *     bool dirty;              // true once any vector command recorded
 *     uint64_t typeId;         // block-header type id (TYPE_VECTOR_DRAWABLE_SINGLETON)
 *   }
 *
 * SLOT RECORD (dumb entry struct owned exclusively by VectorDrawable):
 * ----------------------------------------------------------------------------
 *   VectorCommand {
 *     uint32_t kind;      // command opcode (1=FILL_RECT, 2=DRAW_RECT, 3=FILL_CIRCLE, 4=DRAW_CIRCLE, 5=FILL_PATH, 6=DRAW_PATH)
 *     float x;            // rect origin X in world coords
 *     float y;            // rect origin Y in world coords
 *     float w;            // rect width in world units
 *     float h;            // rect height in world units
 *     float cx;           // circle center X in world coords
 *     float cy;           // circle center Y in world coords
 *     float r;            // circle radius in world units
 *     const Shape *shape; // borrowed view of path geometry (caller retains, never freed by VectorDrawable)
 *     Brush brush;        // fill brush style
 *     Stroke stroke;      // draw stroke style
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VectorDrawable_0(void)                         : Allocate default 1x1 vector canvas
 *   - VectorDrawable_2(w, h)                         : Allocate sized vector canvas
 *
 * Private Constructors: (.c static)
 *   - vectorDrawableCreate(w, h)                     : Allocate and initialize canvas
 *
 * Public Core Functions: (.h)
 *   - VectorDrawable_free(self)                      : Release canvas and command array
 *   - VectorDrawable_fillRect(self, x, y, w, h, b)   : Record filled rectangle
 *   - VectorDrawable_drawRect(self, x, y, w, h, s)   : Record stroked rectangle
 *   - VectorDrawable_fillCircle(self, cx, cy, r, b)  : Record filled circle
 *   - VectorDrawable_drawCircle(self, cx, cy, r, s)  : Record stroked circle
 *   - VectorDrawable_fillPath(self, shape, brush)    : Record filled vector shape
 *   - VectorDrawable_drawPath(self, shape, stroke)   : Record stroked vector shape
 *   - VectorDrawable_clear(self)                     : Clear recorded command array
 *   - VectorDrawable_render(self, dest)              : Render commands to raster board
 *
 * Private Core Functions: (.c static)
 *   - vectorDrawableFreeStorage(self)                : Deallocate heap memory
 *   - vectorDrawableAddCommand(self, kind)           : Append command opcode slot
 *
 * Public Setters: (.h)
 *   - VectorDrawable_setPan(self, panX, panY)        : Set 2D camera pan offsets
 *   - VectorDrawable_setPanX(self, panX)             : Set camera pan X offset
 *   - VectorDrawable_setPanY(self, panY)             : Set camera pan Y offset
 *   - VectorDrawable_setZoom(self, zoom)             : Set camera zoom scaling
 *   - VectorDrawable_setSize(self, w, h)             : Set canvas dimensions
 *   - VectorDrawable_setWidth(self, width)           : Set canvas width
 *   - VectorDrawable_setHeight(self, height)         : Set canvas height
 *   - VectorDrawable_setDirty(self, dirty)           : Set canvas dirty flag
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - VectorDrawable_getPan(self, outX, outY)        : Retrieve 2D camera pan offsets
 *   - VectorDrawable_getPanX(self)                   : Query camera pan X offset
 *   - VectorDrawable_getPanY(self)                   : Query camera pan Y offset
 *   - VectorDrawable_getZoom(self)                   : Query camera zoom scaling
 *   - VectorDrawable_getWidth(self)                  : Query canvas pixel width
 *   - VectorDrawable_getHeight(self)                 : Query canvas pixel height
 *   - VectorDrawable_isDirty(self)                   : Query canvas dirty flag
 *   - VectorDrawable_getCommandCount(self)           : Query recorded command count
 *   - VectorDrawable_getTypeId(self)                 : Query block type ID
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// draw/vector_drawable.c — Infinite-canvas vector board implementation.

static void vectorDrawableFreeStorage(VectorDrawable *self) {
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static VectorDrawable *vectorDrawableCreate(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0)
        return nullptr;
    VectorDrawable *self = (VectorDrawable*) Memory_alloc(TYPE_VECTOR_DRAWABLE_SINGLETON, sizeof(VectorDrawable));
    if (!self)
        self = (VectorDrawable*) calloc(1, sizeof(VectorDrawable));
    if (!self)
        return nullptr;
    size_t cap = 16;
    VectorCommand *cmds = (VectorCommand*) calloc(cap, sizeof(VectorCommand));
    if (!cmds) {
        vectorDrawableFreeStorage(self);
        return nullptr;
    }
    (*self).commands = cmds;
    (*self).commandCount = 0;
    (*self).commandCapacity = cap;
    (*self).panX = 0.0f;
    (*self).panY = 0.0f;
    (*self).zoom = 1.0f;
    (*self).width = w;
    (*self).height = h;
    (*self).dirty = false;
    (*self).typeId = TYPE_VECTOR_DRAWABLE_SINGLETON;
    return self;
}

static VectorCommand *vectorDrawableAddCommand(VectorDrawable *self, uint32_t kind) {
    if (!self)
        return nullptr;
    size_t count = (*self).commandCount;
    size_t cap = (*self).commandCapacity;
    if (count >= cap) {
        size_t newCap = cap == 0 ? 16 : cap * 2;
        VectorCommand *newCmds = (VectorCommand*) realloc((*self).commands, newCap * sizeof(VectorCommand));
        if (!newCmds)
            return nullptr;
        (*self).commands = newCmds;
        (*self).commandCapacity = newCap;
    }
    VectorCommand *cmd = &(*self).commands[count];
    memset(cmd, 0, sizeof(VectorCommand));
    (*cmd).kind = kind;
    (*self).commandCount = count + 1;
    (*self).dirty = true;
    return cmd;
}

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

VectorDrawable *VectorDrawable_0(void) {
    return vectorDrawableCreate(1, 1);
}

VectorDrawable *VectorDrawable_2(uint32_t w, uint32_t h) {
    return vectorDrawableCreate(w, h);
}


// CORE FUNCTIONS
void VectorDrawable_free(VectorDrawable *self) {
    if (!self)
        return;
    if ((*self).commands) {
        free((*self).commands);
        (*self).commands = nullptr;
    }
    vectorDrawableFreeStorage(self);
}

void VectorDrawable_fillRect(VectorDrawable *self, float x, float y, float w, float h, const Brush *brush) {
    VectorCommand *cmd = vectorDrawableAddCommand(self, VECTOR_CMD_FILL_RECT);
    if (!cmd)
        return;
    (*cmd).x = x;
    (*cmd).y = y;
    (*cmd).w = w;
    (*cmd).h = h;
    if (brush)
        (*cmd).brush = *brush;
}

void VectorDrawable_drawRect(VectorDrawable *self, float x, float y, float w, float h, const Stroke *stroke) {
    VectorCommand *cmd = vectorDrawableAddCommand(self, VECTOR_CMD_DRAW_RECT);
    if (!cmd)
        return;
    (*cmd).x = x;
    (*cmd).y = y;
    (*cmd).w = w;
    (*cmd).h = h;
    if (stroke)
        (*cmd).stroke = *stroke;
}

void VectorDrawable_fillCircle(VectorDrawable *self, float cx, float cy, float r, const Brush *brush) {
    VectorCommand *cmd = vectorDrawableAddCommand(self, VECTOR_CMD_FILL_CIRCLE);
    if (!cmd)
        return;
    (*cmd).cx = cx;
    (*cmd).cy = cy;
    (*cmd).r = r;
    if (brush)
        (*cmd).brush = *brush;
}

void VectorDrawable_drawCircle(VectorDrawable *self, float cx, float cy, float r, const Stroke *stroke) {
    VectorCommand *cmd = vectorDrawableAddCommand(self, VECTOR_CMD_DRAW_CIRCLE);
    if (!cmd)
        return;
    (*cmd).cx = cx;
    (*cmd).cy = cy;
    (*cmd).r = r;
    if (stroke)
        (*cmd).stroke = *stroke;
}

void VectorDrawable_fillPath(VectorDrawable *self, const Shape *shape, const Brush *brush) {
    VectorCommand *cmd = vectorDrawableAddCommand(self, VECTOR_CMD_FILL_PATH);
    if (!cmd)
        return;
    (*cmd).shape = shape;
    if (brush)
        (*cmd).brush = *brush;
}

void VectorDrawable_drawPath(VectorDrawable *self, const Shape *shape, const Stroke *stroke) {
    VectorCommand *cmd = vectorDrawableAddCommand(self, VECTOR_CMD_DRAW_PATH);
    if (!cmd)
        return;
    (*cmd).shape = shape;
    if (stroke)
        (*cmd).stroke = *stroke;
}

void VectorDrawable_clear(VectorDrawable *self) {
    if (!self)
        return;
    (*self).commandCount = 0;
    (*self).dirty = true;
}

void VectorDrawable_render(VectorDrawable *self, Drawable *dest) {
    if (!self || !dest)
        return;
    float px = (*self).panX;
    float py = (*self).panY;
    float z = (*self).zoom;
    for (size_t i = 0; i < (*self).commandCount; i++) {
        VectorCommand *cmd = &(*self).commands[i];
        uint32_t kind = (*cmd).kind;
        if (kind == VECTOR_CMD_FILL_RECT)
            Drawable_fillRect(dest, ((*cmd).x + px) * z, ((*cmd).y + py) * z, (*cmd).w * z, (*cmd).h * z, &(*cmd).brush);
        else if (kind == VECTOR_CMD_DRAW_RECT)
            Drawable_drawRect(dest, ((*cmd).x + px) * z, ((*cmd).y + py) * z, (*cmd).w * z, (*cmd).h * z, &(*cmd).stroke);
        else if (kind == VECTOR_CMD_FILL_CIRCLE)
            Drawable_fillCircle(dest, ((*cmd).cx + px) * z, ((*cmd).cy + py) * z, (*cmd).r * z, &(*cmd).brush);
        else if (kind == VECTOR_CMD_DRAW_CIRCLE)
            Drawable_drawCircle(dest, ((*cmd).cx + px) * z, ((*cmd).cy + py) * z, (*cmd).r * z, &(*cmd).stroke);
        else if (kind == VECTOR_CMD_FILL_PATH && (*cmd).shape)
            Drawable_fillPath(dest, (*cmd).shape, &(*cmd).brush);
        else if (kind == VECTOR_CMD_DRAW_PATH && (*cmd).shape)
            Drawable_drawPath(dest, (*cmd).shape, &(*cmd).stroke);
    }
    Drawable_setDirty(dest, true);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void VectorDrawable_setPan(VectorDrawable *self, float panX, float panY) {
    if (!self)
        return;
    (*self).panX = panX;
    (*self).panY = panY;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setPanX(VectorDrawable *self, float panX) {
    if (!self)
        return;
    (*self).panX = panX;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setPanY(VectorDrawable *self, float panY) {
    if (!self)
        return;
    (*self).panY = panY;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setZoom(VectorDrawable *self, float zoom) {
    if (!self)
        return;
    (*self).zoom = zoom;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setSize(VectorDrawable *self, uint32_t w, uint32_t h) {
    if (!self)
        return;
    (*self).width = w;
    (*self).height = h;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setWidth(VectorDrawable *self, uint32_t width) {
    if (!self)
        return;
    (*self).width = width;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setHeight(VectorDrawable *self, uint32_t height) {
    if (!self)
        return;
    (*self).height = height;
    (*self).dirty = true;
}

;;SETTER
void VectorDrawable_setDirty(VectorDrawable *self, bool dirty) {
    if (!self)
        return;
    (*self).dirty = dirty;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
void VectorDrawable_getPan(const VectorDrawable *self, float *outPanX, float *outPanY) {
    if (outPanX)
        *outPanX = self ? (*self).panX : 0.0f;
    if (outPanY)
        *outPanY = self ? (*self).panY : 0.0f;
}

;;GETTER
float VectorDrawable_getPanX(const VectorDrawable *self) {
    return self ? (*self).panX : 0.0f;
}

;;GETTER
float VectorDrawable_getPanY(const VectorDrawable *self) {
    return self ? (*self).panY : 0.0f;
}

;;GETTER
float VectorDrawable_getZoom(const VectorDrawable *self) {
    return self ? (*self).zoom : 0.0f;
}

;;GETTER
uint32_t VectorDrawable_getWidth(const VectorDrawable *self) {
    return self ? (*self).width : 0;
}

;;GETTER
uint32_t VectorDrawable_getHeight(const VectorDrawable *self) {
    return self ? (*self).height : 0;
}

;;GETTER
bool VectorDrawable_isDirty(const VectorDrawable *self) {
    return self ? (*self).dirty : false;
}

;;GETTER
size_t VectorDrawable_getCommandCount(const VectorDrawable *self) {
    return self ? (*self).commandCount : 0;
}

;;GETTER
uint64_t VectorDrawable_getTypeId(const VectorDrawable *self) {
    return self ? (*self).typeId : 0;
}
