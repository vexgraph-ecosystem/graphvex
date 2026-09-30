#ifndef GRAPHICS_GRAPHICS_H
#define GRAPHICS_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// graphvex R3 — graphics/graphics.h
//
// THE CLEAN CORE. The UI is rectangles, so the drawable vocabulary is:
//
//     fill rect   •  image rect  •  text rect   (+ clip push/pop, + clear)
//
// There is NO drawRect, NO circles, NO paths, NO strokes. A rect with a border
// is a rect with a border; a "circle" is a rounded rect; a "stroke" is a border.
// Everything is one by-value Rect, batched into ONE forward render pass.
//
// Two layers:
//   1. DisplayList — the retained quad list the UI paint pass fills ("the
//      graphics language"). Pure data, no GPU, fully testable headless.
//   2. Backend     — a function-pointer row that DRAINS a list into pixels.
//      The raster backend is the headless test backend; the GPU backend lands
//      behind the SAME row (callers never touch a backend type).
//
// Laws honoured: Strict 0xRRGGBBAA Color Law (monotonic shifts below), Native
// Pixel Law (every rect is native hardware pixels; NDC exists only in shaders),
// Forward Rendering & Bounded Surface Law (viewport-bounded, scissored forward).

// ── geometry ────────────────────────────────────────────────────────────────
// Native pixels, Y-down. Mirrors vexspoke's R2 Rectangle (which becomes the
// shared shape once graphvex links R2). By value: a quad list cannot afford a
// heap hop per rect.
typedef struct Rect {
    float x;
    float y;
    float w;
    float h;
} Rect;

static inline bool Rect_isEmpty(const Rect r) { return r.w <= 0.0f || r.h <= 0.0f; }
static inline bool Rect_contains(const Rect r, float px, float py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}
static inline Rect Rect_intersect(const Rect a, const Rect b) {
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    float y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    Rect r = {x0, y0, x1 - x0, y1 - y0};
    return r;
}

// ── color (Strict 0xRRGGBBAA Color Law) ─────────────────────────────────────
typedef uint32_t Color;   // 0xRRGGBBAA
#define COLOR_BLACK       0x000000FFu
#define COLOR_WHITE       0xFFFFFFFFu
#define COLOR_CLEAR       0x00000000u
#define COLOR_RGBA(r, g, b, a) \
    ((((uint32_t)(r) & 0xFFu) << 24) | (((uint32_t)(g) & 0xFFu) << 16) | \
     (((uint32_t)(b) & 0xFFu) << 8) | ((uint32_t)(a) & 0xFFu))
static inline uint8_t Color_red(Color c) { return (uint8_t)((c >> 24) & 0xFFu); }
static inline uint8_t Color_green(Color c) { return (uint8_t)((c >> 16) & 0xFFu); }
static inline uint8_t Color_blue(Color c) { return (uint8_t)((c >> 8) & 0xFFu); }
static inline uint8_t Color_alpha(Color c) { return (uint8_t)(c & 0xFFu); }

// ── brush: color + optional rounded corners + optional border ───────────────
typedef struct Brush {
    Color color;
    float radius;       // corner radius in native px; 0 = square
    Color border;     // border color (alpha 0 = none)
    float borderWidth;  // native px; 0 = none
} Brush;

// ── the drawable Image (pixel buffer; defined in image.h) ───────────────────
typedef struct Image Image;

// ── the display list ────────────────────────────────────────────────────────
enum { CMD_RECT = 0, CMD_IMAGE, CMD_TEXT, CMD_CLIP_PUSH, CMD_CLIP_POP };

typedef struct DrawCmd {
    int32_t kind;
    Rect dst;         // destination rect (native px)
    Rect src;         // source rect for images (native px); zero for rects
    Color color;      // fill / text color
    float radius;
    float border;
    Color borderColor;
    const Image *image;   // borrowed drawable image (null for rects/text)
    const char *text;   // borrowed; NUL-terminated (never freed by the list)
} DrawCmd;

typedef struct DisplayList DisplayList;

DisplayList *DisplayList_0(void);
void DisplayList_free(DisplayList *dl);
void DisplayList_clear(DisplayList *dl);
void DisplayList_rect(DisplayList *dl, Rect dst, const Brush *brush);
void DisplayList_image(DisplayList *dl, const Image *image, Rect src, Rect dst);
void DisplayList_text(DisplayList *dl, Rect dst, const char *text, Color color);
void DisplayList_clip(DisplayList *dl, Rect rect);
void DisplayList_unclip(DisplayList *dl);
size_t DisplayList_count(const DisplayList *dl);
const DrawCmd *DisplayList_cmds(const DisplayList *dl);

// ── the backend row (the slim seam) ─────────────────────────────────────────
typedef struct Backend {
    uint32_t id;
    bool (*begin)(void);
    bool (*end)(void);
    bool (*present)(void);
    bool (*resize)(uint32_t width, uint32_t height);
    bool (*clear)(Color color);
    bool (*clip)(const Rect *rect);   // null = reset
    bool (*fillRect)(const Rect *rect, const Brush *brush);
    bool (*drawImage)(const Image *image, const Rect *dst);
    bool (*drawText)(const Rect *rect, const char *text, const Brush *brush);
    bool (*capture)(Image *dest);   // screenshot: copy the current target into dest
} Backend;

// ── core ────────────────────────────────────────────────────────────────────
bool Graphics_register(const Backend *row);
bool Graphics_use(uint32_t backendId);
uint32_t Graphics_backendId(void);
const Backend *Graphics_current(void);

bool Graphics_begin(void);
bool Graphics_end(void);
bool Graphics_present(void);
bool Graphics_resize(uint32_t width, uint32_t height);
bool Graphics_clear(Color color);
bool Graphics_clip(const Rect *rect);
bool Graphics_fillRect(const Rect *rect, const Brush *brush);
bool Graphics_drawImage(const Image *image, const Rect *dst);
bool Graphics_drawText(const Rect *rect, const char *text, const Brush *brush);

// Screenshot: copy the current render target's pixels (RGBA8, 0xRRGGBBAA byte
// order) into `dest`, sizing it to the target. Backend-agnostic — the raster
// backend copies its framebuffer; a GPU backend does a readback. This is what
// a CAPTURE() test uses to grab a frame for a human or an agent to inspect.
bool Graphics_capture(Image *dest);

// CAPTURE(&image) — screenshot shortcut for tests/harnesses.
#define CAPTURE(dest) Graphics_capture(dest)

// Stream an entire list through the active backend (the One Paint Path).
bool Graphics_submit(DisplayList *dl);

// ── the headless CPU backend (tests / CI) ───────────────────────────────────
#define BACKEND_NONE    0u
#define BACKEND_RASTER  1u

const Backend *RasterGraphics_row(void);
bool Raster_configure(uint32_t width, uint32_t height);   // (re)allocate the target
const uint32_t *Raster_pixels(void);                      // 0xRRGGBBAA, tightly packed
uint32_t Raster_pixelAt(uint32_t x, uint32_t y);          // 0 when out of bounds

#endif // GRAPHICS_GRAPHICS_H
