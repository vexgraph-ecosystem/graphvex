#include "nio/mem.h"
#include "oop/type.h"
#include "../graphics/type.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"
#include "vulkan/texture/texture.h"
#include "util/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Font (opaque handle with local atlas state)
 * LEVEL: L2 — Behavior (font atlas/glyph behavior API)
 * ============================================================================
 * Loaded TrueType font with a dynamic multi-page SDF atlas: live handles
 * raster glyphs on demand from the TTF, baked handles serve a prebuilt
 * dictionary, and both cascade to the platform color path for emoji.
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   GlyphSlot {            // Open-addressed glyph dictionary entry
 *     uint32_t codepoint;  // Unicode codepoint key
 *     GlyphMetrics metrics; // Atlas UVs, quad size, offsets, page, color flag
 *     uint64_t state;      // EMPTY/OCCUPIED/DELETED slot state
 *   }
 *   FontPage {             // One 2048^2 atlas page and its pack cursor
 *     uint8_t *atlas_rgba; // Page pixels (RGBA, SDF replicated / color bitmap)
 *     int32_t textureId;   // Bindless texture ID for this page
 *     bool isColor;        // True = straight-alpha emoji bitmaps, not SDF
 *     int current_x;       // Pack cursor X within the page
 *     int current_y;       // Pack cursor Y within the page
 *     int bottom_y;        // Row bottom for the tight-pack allocator
 *   }
 *   Font {                 // Opaque font handle (see font/font.h)
 *     stbtt_fontinfo info; // stb_truetype parsed TTF (live handles only)
 *     unsigned char *ttf_buffer; // Owned raw TTF bytes (NULL when baked)
 *     bool baked;          // Dictionary-only: no TTF, no on-demand SDF
 *     float bakedAscent;   // Cached ascent for baked handles
 *     float bakedDescent;  // Cached descent for baked handles
 *     float bakedLineGap;  // Cached line gap for baked handles
 *     char family[128];    // Family name for platform color-glyph cascade
 *     FontPage *pages;     // Atlas pages (grown on demand, no ceiling)
 *     size_t pageCount;    // Allocated page count
 *     size_t pageCap;      // Page array capacity
 *     GlyphSlot *slots;    // Glyph dictionary slots
 *     size_t slotCap;      // Slot array capacity
 *     size_t slotCount;    // Occupied slot count
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Font_load(path)
 *   - Font_free(font)
 *   - Font_pageCount(font)
 *   - Font_pageTextureId(font, page)
 *   - Font_pageIsColor(font, page)
 *   - Font_loadSystem(familyName)
 *   - Font_prewarm(font, chars)
 *   - Font_prewarmAscii(font)
 *   - Font_atlasDim(font)
 *   - Font_copyAtlasMonoPage(font, page, outMono)
 *   - Font_glyphCount(font)
 *   - Font_copyGlyphs(font, outCp, outMetrics, cap)
 *   - Font_coversCodepoint(font, codepoint)
 *   - Font_rasterSdfWork(font, codepoint, outWork)
 *   - Font_packSdfWork(font, work, outBase)
 *   - Font_freeSdfWork(work)
 *   - Font_indexGlyph(font, codepoint, metrics)
 *   - Font_rasterCoverageWork(font, codepoint, outWork)
 *   - Font_freeCovWork(work)
 *   - Font_bakeRefScale(font)
 *   - Font_createFromBaked(atlasMono, pageCount, atlasDim, codepoints, metrics, count, ascent, descent, lineGap)
 *
 * Setters:
 *   - Font_setFamily(font, familyName)
 *
 * Getters:
 *   - Font_getGlyph(font, codepoint, pixelHeight, outMetrics)
 *   - Font_getTextureId(font)
 *   - Font_getVMetrics(font, ascent, descent, lineGap)
 *   - Font_getScaleForPixelHeight(font, height)
 *   - Font_getKerning(font, cp1, cp2, pixelHeight)
 *   - Font_isBaked(font)
 * ============================================================================
 */


#define ATLAS_SIZE 2048
_Static_assert(ATLAS_SIZE == FONT_ATLAS_DIM, "atlas dim must match header");
#define SDF_REF_HEIGHT 128.0f
#define SDF_PADDING 8
#define SDF_ONEDGE 128
#define SDF_PIXEL_DIST_SCALE 16.0f

#define GLYPH_CACHE_INIT_CAP 128
#define GLYPH_STATE_EMPTY 0
#define GLYPH_STATE_OCCUPIED 1
#define GLYPH_STATE_DELETED 2

typedef struct GlyphSlot {
    uint32_t codepoint;
    GlyphMetrics metrics;
    uint64_t state;
} GlyphSlot;

typedef struct FontPage {
    uint8_t *atlas_rgba;
    int32_t textureId;
    bool isColor; // straight-alpha bitmaps (emoji), not SDF distances
    int current_x;
    int current_y;
    int bottom_y;
} FontPage;

struct Font {
    stbtt_fontinfo info;
    unsigned char *ttf_buffer;
    bool baked; // dictionary-only: no TTF, no on-demand SDF
    float bakedAscent;
    float bakedDescent;
    float bakedLineGap;
    char family[128]; // for platform color-glyph cascade (may be empty)
    FontPage *pages;
    size_t pageCount;
    size_t pageCap;
    GlyphSlot *slots;
    size_t slotCap;
    size_t slotCount;
};

// Platform color-glyph raster (emoji): CoreGraphics on Apple, stub elsewhere.
// outRGBA is malloc'd straight-alpha RGBA8, visual-top-first rows.
extern bool System_rasterColorGlyph(const char *familyName, uint32_t codepoint,
                                    int pixelHeight, uint8_t **outRGBA,
                                    int *outW, int *outH,
                                    float *outXOff, float *outYOff,
                                    float *outAdvance);

#define COLOR_REF_HEIGHT 128.0f

// Allocates one atlas page (RGBA + bindless texture) and appends it.
// Live handles start with page 0 from Font_load; overflow glyphs grow more
// pages without limit (the Dynamic Scalability & Anti-Hardcoding Law).
// Color pages hold straight-alpha bitmaps and are never mixed with SDF.
static bool allocPage(Font *font, bool isColor) {
    if ((*font).pageCount >= (*font).pageCap) {
        size_t newCap = (*font).pageCap == 0 ? 4 : (*font).pageCap * 2;
        // Memory_realloc rejects NULL, so the first page uses alloc.
        FontPage *next = (*font).pages
            ? (FontPage*) Memory_realloc((*font).pages, newCap * sizeof(FontPage))
            : (FontPage*) Memory_alloc(TYPE_ARRAY, newCap * sizeof(FontPage));
        if (!next)
            return false;
        (*font).pages = next;
        (*font).pageCap = newCap;
    }
    uint8_t *rgba = (uint8_t*) Memory_alloc(TYPE_ARRAY, ATLAS_SIZE * ATLAS_SIZE * 4);
    if (!rgba)
        return false;
    memset(rgba, 0, ATLAS_SIZE * ATLAS_SIZE * 4);
    // Headless baker (fontbake CLI) has no Vulkan device: keep the CPU atlas
    // and skip the upload with textureId -1. Runtime always has a device.
    int32_t texId = Texture_isReady() ? Texture_loadRaw(rgba, ATLAS_SIZE, ATLAS_SIZE) : -1;
    FontPage *pg = &(*font).pages[(*font).pageCount];
    (*pg).atlas_rgba = rgba;
    (*pg).textureId = texId;
    (*pg).isColor = isColor;
    (*pg).current_x = 0;
    (*pg).current_y = 0;
    (*pg).bottom_y = 0;
    (*font).pageCount++;
    return true;
}

static uint64_t hashGlyph(uint32_t codepoint) {
    return Hash_murmur3Mix64((uint64_t) codepoint);
}

static GlyphSlot *slotAt(Font *font, size_t index) {
    return &(*font).slots[index];
}

static void growCache(Font *font, size_t newCap) {
    size_t oldCap = (*font).slotCap;
    GlyphSlot *oldSlots = (*font).slots;
    GlyphSlot *newSlots = (GlyphSlot*) Memory_alloc(TYPE_ARRAY, newCap * sizeof(GlyphSlot));
    if (!newSlots)
        return;
    memset(newSlots, 0, newCap * sizeof(GlyphSlot));
    size_t mask = newCap - 1;
    for (size_t i = 0; i < oldCap; i++) {
        GlyphSlot *s = &oldSlots[i];
        if ((*s).state == GLYPH_STATE_OCCUPIED) {
            uint64_t h = hashGlyph((*s).codepoint);
            size_t idx = (size_t) (h & mask);
            while ((*(newSlots + idx)).state == GLYPH_STATE_OCCUPIED)
                idx = (idx + 1) & mask;
            *(newSlots + idx) = *s;
        }
    }
    if (oldSlots)
        Memory_free(oldSlots);
    (*font).slots = newSlots;
    (*font).slotCap = newCap;
}

static bool findGlyph(Font *font, uint32_t codepoint, GlyphMetrics *outMetrics) {
    if (!(*font).slots)
        return false;
    size_t cap = (*font).slotCap;
    if (cap == 0)
        return false;
    uint64_t h = hashGlyph(codepoint);
    size_t mask = cap - 1;
    size_t idx = (size_t) (h & mask);
    for (size_t i = 0; i < cap; i++) {
        GlyphSlot *s = slotAt(font, idx);
        uint64_t st = (*s).state;
        if (st == GLYPH_STATE_EMPTY)
            return false;
        if (st == GLYPH_STATE_OCCUPIED && (*s).codepoint == codepoint) {
            if (outMetrics)
                *outMetrics = (*s).metrics;
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

static void insertGlyph(Font *font, uint32_t codepoint, GlyphMetrics *metrics) {
    size_t cap = (*font).slotCap;
    if (cap == 0) {
        growCache(font, GLYPH_CACHE_INIT_CAP);
        cap = (*font).slotCap;
        if (cap == 0)
            return;
    }
    size_t load = cap - cap / 4;
    if ((*font).slotCount >= load) {
        growCache(font, cap * 2);
        cap = (*font).slotCap;
    }
    uint64_t h = hashGlyph(codepoint);
    size_t mask = cap - 1;
    size_t idx = (size_t) (h & mask);
    size_t firstDeleted = (size_t) -1;
    while (1) {
        GlyphSlot *s = slotAt(font, idx);
        uint64_t st = (*s).state;
        if (st == GLYPH_STATE_EMPTY) {
            size_t target = firstDeleted != (size_t) -1 ? firstDeleted : idx;
            GlyphSlot *t = slotAt(font, target);
            (*t).codepoint = codepoint;
            (*t).metrics = *metrics;
            (*t).state = GLYPH_STATE_OCCUPIED;
            (*font).slotCount++;
            return;
        }
        if (st == GLYPH_STATE_DELETED) {
            if (firstDeleted == (size_t) -1)
                firstDeleted = idx;
        } else if (st == GLYPH_STATE_OCCUPIED) {
            if ((*s).codepoint == codepoint) {
                (*s).metrics = *metrics;
                return;
            }
        }
        idx = (idx + 1) & mask;
    }
}

Font *Font_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Failed to open font: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size_t size = (size_t) ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *ttf_buffer = (unsigned char*) Memory_alloc(TYPE_ARRAY, size);
    if (!ttf_buffer) {
        fclose(f);
        return NULL;
    }
    fread(ttf_buffer, 1, size, f);
    fclose(f);
    Font *font = (Font*) Memory_alloc(TYPE_FONT_SINGLETON, sizeof(Font));
    if (font)
        memset(font, 0, sizeof(Font));
    if (!font) {
        Memory_free(ttf_buffer);
        return NULL;
    }
    (*font).ttf_buffer = ttf_buffer;
    // .ttc collections (Helvetica, Menlo, ...) need a face offset; plain
    // .ttf resolves to 0 through the same call. Face 0 is the Regular cut.
    if (!stbtt_InitFont(&(*font).info, (*font).ttf_buffer,
                        stbtt_GetFontOffsetForIndex((*font).ttf_buffer, 0))) {
        Memory_free(ttf_buffer);
        (*font).ttf_buffer = NULL;
        Memory_free(font);
        return NULL;
    }
    if (!allocPage(font, false)) {
        Memory_free(ttf_buffer);
        (*font).ttf_buffer = NULL;
        if ((*font).pages)
            Memory_free((*font).pages);
        (*font).pages = NULL;
        Memory_free(font);
        return NULL;
    }
    (*font).slots = (GlyphSlot*) Memory_alloc(TYPE_ARRAY, GLYPH_CACHE_INIT_CAP * sizeof(GlyphSlot));
    if ((*font).slots) {
        memset((*font).slots, 0, GLYPH_CACHE_INIT_CAP * sizeof(GlyphSlot));
        (*font).slotCap = GLYPH_CACHE_INIT_CAP;
        (*font).slotCount = 0;
    }
    return font;
}

void Font_free(Font *font) {
    if (!font)
        return;
    if ((*font).ttf_buffer)
        Memory_free((*font).ttf_buffer);
    if ((*font).pages) {
        for (size_t i = 0; i < (*font).pageCount; i++) {
            if ((*font).pages[i].atlas_rgba)
                Memory_free((*font).pages[i].atlas_rgba);
        }
        Memory_free((*font).pages);
    }
    if ((*font).slots)
        Memory_free((*font).slots);
    Memory_free(font);
}

float Font_getScaleForPixelHeight(const Font *font, float height) {
    if (!font)
        return 0.0f;
    if ((*font).baked || !(*font).ttf_buffer)
        return (SDF_REF_HEIGHT > 0.0f) ? (height / SDF_REF_HEIGHT) : 0.0f;
    return stbtt_ScaleForPixelHeight(&(*font).info, height);
}

void Font_getVMetrics(const Font *font, float *ascent, float *descent, float *lineGap) {
    if (!font)
        return;
    if ((*font).baked || !(*font).ttf_buffer) {
        if (ascent)
            *ascent = (*font).bakedAscent;
        if (descent)
            *descent = (*font).bakedDescent;
        if (lineGap)
            *lineGap = (*font).bakedLineGap;
        return;
    }
    int a = 0;
    int d = 0;
    int l = 0;
    stbtt_GetFontVMetrics(&(*font).info, &a, &d, &l);
    if (ascent)
        *ascent = (float) a;
    if (descent)
        *descent = (float) d;
    if (lineGap)
        *lineGap = (float) l;
}

// Last page usable for SDF (allocating a fresh SDF page past color pages).
static FontPage *sdfTargetPage(Font *font) {
    if ((*font).pageCount == 0)
        return NULL;
    FontPage *pg = &(*font).pages[(*font).pageCount - 1];
    if ((*pg).isColor) {
        if (!allocPage(font, false))
            return NULL;
        pg = &(*font).pages[(*font).pageCount - 1];
    }
    return pg;
}

// Last page usable for color bitmaps (fresh color page past SDF pages).
static FontPage *colorTargetPage(Font *font) {
    if ((*font).pageCount == 0)
        return NULL;
    FontPage *pg = &(*font).pages[(*font).pageCount - 1];
    if (!(*pg).isColor) {
        if (!allocPage(font, true))
            return NULL;
        pg = &(*font).pages[(*font).pageCount - 1];
    }
    return pg;
}

bool Font_rasterSdfWork(Font *font, uint32_t codepoint, FontSdfWork *outWork) {
    if (!font || !outWork || (*font).baked || !(*font).ttf_buffer)
        return false;
    memset(outWork, 0, sizeof(*outWork));
    if (!Font_coversCodepoint(font, codepoint))
        return false;
    float refScale = stbtt_ScaleForPixelHeight(&(*font).info, SDF_REF_HEIGHT);
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char *sdf = stbtt_GetCodepointSDF(&(*font).info, refScale, (int) codepoint, SDF_PADDING, SDF_ONEDGE, SDF_PIXEL_DIST_SCALE, &w, &h, &xoff, &yoff);
    if (!sdf)
        return false;
    if (w > ATLAS_SIZE || h > ATLAS_SIZE) {
        free(sdf);
        return false;
    }
    int advanceW = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&(*font).info, (int) codepoint, &advanceW, &lsb);
    (*outWork).codepoint = codepoint;
    (*outWork).sdf = sdf;
    (*outWork).w = w;
    (*outWork).h = h;
    (*outWork).xoff = xoff;
    (*outWork).yoff = yoff;
    (*outWork).advanceW = advanceW;
    return true;
}

void Font_freeSdfWork(FontSdfWork *work) {
    if (!work)
        return;
    free((*work).sdf);
    memset(work, 0, sizeof(*work));
}

void Font_indexGlyph(Font *font, uint32_t codepoint, const GlyphMetrics *metrics) {
    if (!font || !metrics)
        return;
    insertGlyph(font, codepoint, (GlyphMetrics*) metrics);
}

bool Font_rasterCoverageWork(Font *font, uint32_t codepoint, FontCovWork *outWork) {
    if (!font || !outWork || (*font).baked || !(*font).ttf_buffer)
        return false;
    memset(outWork, 0, sizeof(*outWork));
    if (!Font_coversCodepoint(font, codepoint))
        return false;
    float refScale = stbtt_ScaleForPixelHeight(&(*font).info, SDF_REF_HEIGHT);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(&(*font).info, (int)codepoint, refScale, refScale,
                                &x0, &y0, &x1, &y1);
    int advanceW = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&(*font).info, (int)codepoint, &advanceW, &lsb);
    (*outWork).codepoint = codepoint;
    (*outWork).x0 = x0;
    (*outWork).y0 = y0;
    (*outWork).advanceW = advanceW;
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0)
        return true; // empty (space): advance valid, no bitmap
    if (w > ATLAS_SIZE || h > ATLAS_SIZE)
        return false;
    uint8_t *bmp = (uint8_t*) malloc((size_t)w * h);
    if (!bmp)
        return false;
    stbtt_MakeCodepointBitmap(&(*font).info, bmp, w, h, w, refScale, refScale, (int)codepoint);
    (*outWork).bmp = bmp;
    (*outWork).w = w;
    (*outWork).h = h;
    return true;
}

void Font_freeCovWork(FontCovWork *work) {
    if (!work)
        return;
    free((*work).bmp);
    memset(work, 0, sizeof(*work));
}

float Font_bakeRefScale(const Font *font) {
    if (!font || (*font).baked || !(*font).ttf_buffer)
        return 0.0f;
    return stbtt_ScaleForPixelHeight(&(*font).info, SDF_REF_HEIGHT);
}

bool Font_packSdfWork(Font *font, const FontSdfWork *work, GlyphMetrics *outBase) {
    if (!font || !work || !(*work).sdf || !outBase)
        return false;
    int w = (*work).w, h = (*work).h;
    if (w <= 0 || h <= 0 || w > ATLAS_SIZE || h > ATLAS_SIZE)
        return false;
    float refScale = stbtt_ScaleForPixelHeight(&(*font).info, SDF_REF_HEIGHT);
    FontPage *pg = sdfTargetPage(font);
    if (!pg)
        return false;
    if ((*pg).current_x + w > ATLAS_SIZE) {
        (*pg).current_x = 0;
        (*pg).current_y = (*pg).bottom_y;
    }
    if ((*pg).current_y + h > ATLAS_SIZE) {
        if (!allocPage(font, false))
            return false;
        pg = &(*font).pages[(*font).pageCount - 1];
        if ((*pg).current_y + h > ATLAS_SIZE)
            return false; // oversized for a fresh page (defensive)
    }
    int pageIndex = (int)((*font).pageCount - 1);
    int destX = (*pg).current_x;
    int destY = (*pg).current_y;
    unsigned char *sdf = (*work).sdf;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int srcIdx = y * w + x;
            int dstIdx = ((destY + y) * ATLAS_SIZE + (destX + x)) * 4;
            unsigned char dist = sdf[srcIdx];
            (*pg).atlas_rgba[dstIdx + 0] = dist;
            (*pg).atlas_rgba[dstIdx + 1] = dist;
            (*pg).atlas_rgba[dstIdx + 2] = dist;
            (*pg).atlas_rgba[dstIdx + 3] = dist;
        }
    }
    uint8_t *tmp = (uint8_t*) Memory_alloc(TYPE_ARRAY, (size_t) (w * h * 4));
    int tmpIsManaged = 1;
    if (!tmp) {
        tmp = (uint8_t*) malloc((size_t) (w * h * 4));
        tmpIsManaged = 0;
    }
    if (tmp) {
        for (int i = 0; i < w * h; i++) {
            unsigned char dist = sdf[i];
            tmp[i * 4 + 0] = dist;
            tmp[i * 4 + 1] = dist;
            tmp[i * 4 + 2] = dist;
            tmp[i * 4 + 3] = dist;
        }
        if ((*pg).textureId >= 0)
            Texture_updateSubRaw((*pg).textureId, tmp, (uint32_t) destX, (uint32_t) destY, (uint32_t) w, (uint32_t) h);
        if (tmpIsManaged)
            Memory_free(tmp);
        else
            free(tmp);
    }
    (*pg).current_x += w + 1;
    if ((*pg).current_y + h > (*pg).bottom_y)
        (*pg).bottom_y = (*pg).current_y + h + 1;
    (*outBase).page = pageIndex;
    (*outBase).color = 0;
    (*outBase).u0 = (float) destX / (float) ATLAS_SIZE;
    (*outBase).v0 = (float) destY / (float) ATLAS_SIZE;
    (*outBase).u1 = (float) (destX + w) / (float) ATLAS_SIZE;
    (*outBase).v1 = (float) (destY + h) / (float) ATLAS_SIZE;
    (*outBase).width = (float) w;
    (*outBase).height = (float) h;
    (*outBase).xOffset = (float) (*work).xoff;
    (*outBase).yOffset = (float) (*work).yoff;
    (*outBase).advance = (float) (*work).advanceW * refScale;
    return true;
}

static bool rasterSdfGlyph(Font *font, uint32_t codepoint, GlyphMetrics *outBase) {
    FontSdfWork work;
    if (!Font_rasterSdfWork(font, codepoint, &work))
        return false;
    bool ok = Font_packSdfWork(font, &work, outBase);
    Font_freeSdfWork(&work);
    return ok;
}

// Platform color cascade (emoji): rastered at COLOR_REF_HEIGHT through the
// OS text stack with family -> color-emoji fallback, packed into color pages.
// Session cache only — re-rastered per launch, never stored in .antifont.
static bool rasterColorGlyph(Font *font, uint32_t codepoint, GlyphMetrics *outBase) {
    if ((*font).family[0] == '\0')
        return false;
    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    float xoff = 0.0f, yoff = 0.0f, advance = 0.0f;
    if (!System_rasterColorGlyph((*font).family, codepoint, (int)COLOR_REF_HEIGHT,
                                 &rgba, &w, &h, &xoff, &yoff, &advance))
        return false;
    bool ok = false;
    if (rgba && w > 0 && h > 0 && w <= ATLAS_SIZE && h <= ATLAS_SIZE) {
        FontPage *pg = colorTargetPage(font);
        if (pg) {
            if ((*pg).current_x + w > ATLAS_SIZE) {
                (*pg).current_x = 0;
                (*pg).current_y = (*pg).bottom_y;
            }
            if ((*pg).current_y + h > ATLAS_SIZE && !allocPage(font, true))
                pg = NULL;
            else if ((*pg).current_y + h > ATLAS_SIZE)
                pg = &(*font).pages[(*font).pageCount - 1];
            if (pg && (*pg).current_y + h <= ATLAS_SIZE) {
                int pageIndex = (int)((*font).pageCount - 1);
                int destX = (*pg).current_x;
                int destY = (*pg).current_y;
                for (int y = 0; y < h; y++) {
                    memcpy(&(*pg).atlas_rgba[((destY + y) * ATLAS_SIZE + destX) * 4],
                           &rgba[(size_t)y * w * 4], (size_t)w * 4);
                }
                if ((*pg).textureId >= 0)
                    Texture_updateSubRaw((*pg).textureId, rgba, (uint32_t) destX, (uint32_t) destY, (uint32_t) w, (uint32_t) h);
                (*pg).current_x += w + 1;
                if ((*pg).current_y + h > (*pg).bottom_y)
                    (*pg).bottom_y = (*pg).current_y + h + 1;
                (*outBase).page = pageIndex;
                (*outBase).color = 1;
                (*outBase).u0 = (float) destX / (float) ATLAS_SIZE;
                (*outBase).v0 = (float) destY / (float) ATLAS_SIZE;
                (*outBase).u1 = (float) (destX + w) / (float) ATLAS_SIZE;
                (*outBase).v1 = (float) (destY + h) / (float) ATLAS_SIZE;
                (*outBase).width = (float) w;
                (*outBase).height = (float) h;
                (*outBase).xOffset = xoff;
                (*outBase).yOffset = yoff;
                (*outBase).advance = advance;
                ok = true;
            }
        }
    }
    free(rgba);
    return ok;
}

bool Font_getGlyph(Font *font, uint32_t codepoint, float pixelHeight, GlyphMetrics *outMetrics) {
    if (!font)
        return false;
    GlyphMetrics baseMetrics = {0};
    if (!findGlyph(font, codepoint, &baseMetrics)) {
        bool cached = false;
        // 1. SDF raster for glyphs the live TTF maps.
        if (!(*font).baked && (*font).ttf_buffer &&
            Font_coversCodepoint(font, codepoint))
            cached = rasterSdfGlyph(font, codepoint, &baseMetrics);
        // 2. Platform color cascade (emoji): anything the TTF does not map,
        //    or any miss on a family-tagged baked handle.
        if (!cached)
            cached = rasterColorGlyph(font, codepoint, &baseMetrics);
        if (!cached)
            return false;
        insertGlyph(font, codepoint, &baseMetrics);
    }
    if (outMetrics) {
        float factor = (pixelHeight > 0.0f) ? (pixelHeight / SDF_REF_HEIGHT) : 1.0f;
        (*outMetrics).u0 = baseMetrics.u0;
        (*outMetrics).v0 = baseMetrics.v0;
        (*outMetrics).u1 = baseMetrics.u1;
        (*outMetrics).v1 = baseMetrics.v1;
        (*outMetrics).width = baseMetrics.width * factor;
        (*outMetrics).height = baseMetrics.height * factor;
        (*outMetrics).xOffset = baseMetrics.xOffset * factor;
        (*outMetrics).yOffset = baseMetrics.yOffset * factor;
        (*outMetrics).advance = baseMetrics.advance * factor;
        (*outMetrics).page = baseMetrics.page;
        (*outMetrics).color = baseMetrics.color;
    }
    return true;
}

int32_t Font_getTextureId(const Font *font) {
    return Font_pageTextureId(font, 0);
}

size_t Font_pageCount(const Font *font) {
    return font ? (*font).pageCount : 0;
}

int32_t Font_pageTextureId(const Font *font, size_t page) {
    if (!font || page >= (*font).pageCount)
        return -1;
    return (*font).pages[page].textureId;
}

bool Font_pageIsColor(const Font *font, size_t page) {
    if (!font || page >= (*font).pageCount)
        return false;
    return (*font).pages[page].isColor;
}

void Font_setFamily(Font *font, const char *familyName) {
    if (!font)
        return;
    memset((*font).family, 0, sizeof((*font).family));
    if (familyName)
        strncpy((*font).family, familyName, sizeof((*font).family) - 1);
}

bool Font_coversCodepoint(const Font *font, uint32_t codepoint) {
    if (!font || (*font).baked || !(*font).ttf_buffer)
        return false;
    if (codepoint < 0x20u || codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
        (codepoint >= 0xFDD0u && codepoint <= 0xFDEFu) ||
        ((codepoint & 0xFFFEu) == 0xFFFEu))
        return false;
    return stbtt_FindGlyphIndex(&(*font).info, (int)codepoint) != 0;
}

float Font_getKerning(const Font *font, uint32_t cp1, uint32_t cp2, float pixelHeight) {
    if (!font)
        return 0.0f;
    if ((*font).baked || !(*font).ttf_buffer)
        return 0.0f;
    float scale = stbtt_ScaleForPixelHeight(&(*font).info, pixelHeight);
    return (float) stbtt_GetCodepointKernAdvance(&(*font).info, (int) cp1, (int) cp2) * scale;
}

extern char* System_getFontPath(const char* familyName);

Font *Font_loadSystem(const char *familyName) {
    char *path = System_getFontPath(familyName);
    if (!path) {
        printf("Font_loadSystem: Could not find font '%s'\n", familyName);
        return NULL;
    }
    Font *f = Font_load(path);
    free(path);
    if (f)
        Font_setFamily(f, familyName);
    return f;
}

void Font_prewarm(Font *font, const char *chars) {
    if (!font || !chars)
        return;
    size_t len = strlen(chars);
    for (size_t i = 0; i < len; ) {
        uint32_t codepoint = 0;
        unsigned char c0 = (unsigned char) chars[i];
        int charLen = 1;
        if (c0 < 0x80) {
            codepoint = c0;
        } else if ((c0 & 0xE0) == 0xC0) {
            if (i + 1 < len)
                codepoint = ((c0 & 0x1F) << 6) | (chars[i + 1] & 0x3F);
            charLen = 2;
        } else if ((c0 & 0xF0) == 0xE0) {
            if (i + 2 < len)
                codepoint = ((c0 & 0x0F) << 12) | (((chars[i + 1] & 0x3F) << 6)) | (chars[i + 2] & 0x3F);
            charLen = 3;
        } else if ((c0 & 0xF8) == 0xF0) {
            if (i + 3 < len)
                codepoint = ((c0 & 0x07) << 18) | (((chars[i + 1] & 0x3F) << 12)) | (((chars[i + 2] & 0x3F) << 6)) | (chars[i + 3] & 0x3F);
            charLen = 4;
        }
        Font_getGlyph(font, codepoint, SDF_REF_HEIGHT, NULL);
        i += charLen;
    }
}

void Font_prewarmAscii(Font *font) {
    if (!font)
        return;
    for (uint32_t cp = 32; cp <= 126; cp++)
        Font_getGlyph(font, cp, SDF_REF_HEIGHT, NULL);
}

bool Font_isBaked(const Font *font) {
    return font && (*font).baked;
}

int Font_atlasDim(const Font *font) {
    (void)font;
    return ATLAS_SIZE;
}

void Font_copyAtlasMonoPage(const Font *font, size_t page, uint8_t *outMono) {
    if (!font || !outMono || page >= (*font).pageCount)
        return;
    uint8_t *rgba = (*font).pages[page].atlas_rgba;
    if (!rgba)
        return;
    for (int i = 0; i < ATLAS_SIZE * ATLAS_SIZE; i++)
        outMono[i] = rgba[i * 4];
}

size_t Font_glyphCount(const Font *font) {
    return font ? (*font).slotCount : 0;
}

size_t Font_copyGlyphs(const Font *font, uint32_t *outCp,
                       GlyphMetrics *outMetrics, size_t cap) {
    if (!font || !(*font).slots)
        return 0;
    size_t total = 0;
    for (size_t i = 0; i < (*font).slotCap; i++) {
        GlyphSlot *s = slotAt((Font*) font, i);
        if ((*s).state != GLYPH_STATE_OCCUPIED)
            continue;
        if (outCp && outMetrics && total < cap) {
            outCp[total] = (*s).codepoint;
            outMetrics[total] = (*s).metrics;
        }
        total++;
    }
    return (outCp && outMetrics) ? (total < cap ? total : cap) : total;
}

Font *Font_createFromBaked(const uint8_t *atlasMono, size_t pageCount,
                           int atlasDim,
                           const uint32_t *codepoints,
                           const GlyphMetrics *metrics, size_t count,
                           float ascent, float descent, float lineGap) {
    if (!atlasMono || pageCount == 0 || (count > 0 && pageCount > count) ||
        atlasDim != ATLAS_SIZE)
        return NULL;
    if (count > 0 && (!codepoints || !metrics))
        return NULL;
    Font *font = (Font*) Memory_alloc(TYPE_FONT_SINGLETON, sizeof(Font));
    if (!font)
        return NULL;
    memset(font, 0, sizeof(Font));
    (*font).ttf_buffer = NULL;
    (*font).baked = true;
    (*font).bakedAscent = ascent;
    (*font).bakedDescent = descent;
    (*font).bakedLineGap = lineGap;
    (*font).pages = (FontPage*) Memory_alloc(TYPE_ARRAY, pageCount * sizeof(FontPage));
    if (!(*font).pages) {
        Memory_free(font);
        return NULL;
    }
    memset((*font).pages, 0, pageCount * sizeof(FontPage));
    (*font).pageCap = pageCount;
    for (size_t p = 0; p < pageCount; p++) {
        uint8_t *rgba = (uint8_t*) Memory_alloc(TYPE_ARRAY, ATLAS_SIZE * ATLAS_SIZE * 4);
        if (!rgba) {
            for (size_t q = 0; q < p; q++)
                Memory_free((*font).pages[q].atlas_rgba);
            Memory_free((*font).pages);
            (*font).pages = NULL;
            Memory_free(font);
            return NULL;
        }
        const uint8_t *src = atlasMono + p * ATLAS_SIZE * ATLAS_SIZE;
        for (int i = 0; i < ATLAS_SIZE * ATLAS_SIZE; i++) {
            uint8_t v = src[i];
            rgba[i * 4 + 0] = v;
            rgba[i * 4 + 1] = v;
            rgba[i * 4 + 2] = v;
            rgba[i * 4 + 3] = v;
        }
        (*font).pages[p].atlas_rgba = rgba;
        (*font).pages[p].textureId = Texture_isReady()
            ? Texture_loadRaw(rgba, ATLAS_SIZE, ATLAS_SIZE) : -1;
        (*font).pages[p].current_x = 0;
        (*font).pages[p].current_y = 0;
        (*font).pages[p].bottom_y = 0;
    }
    (*font).pageCount = pageCount;
    size_t cap = GLYPH_CACHE_INIT_CAP;
    while (cap < count * 2 + 8)
        cap *= 2;
    (*font).slots = (GlyphSlot*) Memory_alloc(TYPE_ARRAY, cap * sizeof(GlyphSlot));
    if (!(*font).slots) {
        for (size_t p = 0; p < pageCount; p++)
            Memory_free((*font).pages[p].atlas_rgba);
        Memory_free((*font).pages);
        (*font).pages = NULL;
        Memory_free(font);
        return NULL;
    }
    memset((*font).slots, 0, cap * sizeof(GlyphSlot));
    (*font).slotCap = cap;
    (*font).slotCount = 0;
    for (size_t i = 0; i < count; i++) {
        if (metrics[i].page < 0 || (size_t)metrics[i].page >= pageCount) {
            for (size_t p = 0; p < pageCount; p++)
                Memory_free((*font).pages[p].atlas_rgba);
            Memory_free((*font).pages);
            (*font).pages = NULL;
            Memory_free((*font).slots);
            (*font).slots = NULL;
            Memory_free(font);
            return NULL;
        }
        insertGlyph(font, codepoints[i], (GlyphMetrics*) &metrics[i]);
    }
    return font;
}
