#ifndef ANTI_FONT_H
#define ANTI_FONT_H

#include <stdint.h>
#include <stdbool.h>

// Represents a loaded TrueType font and its dynamic SDF atlas.
typedef struct Font Font;

// A baked glyph's metrics and atlas coordinates.
typedef struct GlyphMetrics {
    float u0, v0, u1, v1;   // UV bounds within the glyph's atlas page
    float width, height;    // Pixel size of the glyph quad
    float xOffset, yOffset; // Drawing offset from cursor baseline
    float advance;          // How much to advance the cursor X
    int32_t page;           // Atlas page index (see Font_pageTextureId)
    int32_t color;          // 0 = SDF glyph (Vk_drawSDFText), 1 = color glyph
                            // (runtime emoji via Vk_drawColorGlyph)
} GlyphMetrics;

// Loads a TTF file from the given path.
Font *Font_load(const char *path);

// Destroys the font and its atlas.
void Font_free(Font *font);

// Gets the glyph metrics for a codepoint.
// SDF glyphs raster on demand from the live TTF; anything the TTF does not
// map falls back to the platform color cascade (emoji) when the handle
// carries a family name (see Font_setFamily). Baked handles serve their
// dictionary first and cascade the same way. The 'pixelHeight' scales the
// 128px reference metrics; gm.color selects the draw call (SDF vs color).
bool Font_getGlyph(Font *font, uint32_t codepoint, float pixelHeight, GlyphMetrics *outMetrics);

// Returns the bindless texture ID for this font's atlas.
int32_t Font_getTextureId(const Font *font);

// Multi-page atlas: big coverages (CJK-scale fonts) spill past one 2048^2
// page, so each glyph names its page and each page owns a texture. Pages
// grow on demand without a ceiling (the Dynamic Scalability & Anti-Hardcoding Law).

// Number of allocated atlas pages (>= 1 once loaded).
size_t Font_pageCount(const Font *font);

// Bindless texture ID for a page, or -1 for a bad index. Page 0 is what
// Font_getTextureId returns.
int32_t Font_pageTextureId(const Font *font, size_t page);

// True when a page holds straight-alpha color bitmaps (emoji) instead of
// SDF distances. Render those glyphs with Vk_drawColorGlyph.
bool Font_pageIsColor(const Font *font, size_t page);

// Remembers the family name for platform color-glyph cascade
// (emoji fallback at runtime). Set by Font_loadSystem/Font_openBaked.
void Font_setFamily(Font *font, const char *familyName);

// Returns the line height metrics (ascent, descent, line gap) unscaled.
void Font_getVMetrics(const Font *font, float *ascent, float *descent, float *lineGap);

// Gets the scale factor for a target pixel height.
float Font_getScaleForPixelHeight(const Font *font, float height);

float Font_getKerning(const Font *font, uint32_t cp1, uint32_t cp2, float pixelHeight);

Font *Font_loadSystem(const char *familyName);

void Font_prewarm(Font *font, const char *chars);
void Font_prewarmAscii(Font *font);

// --- Baked-store support (see font/font_bake.h) -----------------------------
// A baked font carries its atlas + glyph dictionary and needs no TTF at
// runtime: glyph lookup is pure dictionary, so there is no FontBook /
// CoreText / stbtt work on the render path. Missing codepoints return false
// instead of baking on demand (the TTF buffer is NULL).
#define FONT_ATLAS_DIM 2048

// True when this handle came from a baked file (no TTF, dictionary-only).
bool Font_isBaked(const Font *font);

// Atlas dimension of a live handle (always FONT_ATLAS_DIM).
int Font_atlasDim(const Font *font);

// Copies one page's atlas to outMono as atlasDim*atlasDim single-channel
// bytes (the R channel; the live texture is RGBA with the SDF replicated).
void Font_copyAtlasMonoPage(const Font *font, size_t page, uint8_t *outMono);

// Dictionary iteration over baked/present glyphs. Returns total count when
// outCp/outMetrics are NULL; otherwise fills up to cap entries and returns
// the number written.
size_t Font_glyphCount(const Font *font);
size_t Font_copyGlyphs(const Font *font, uint32_t *outCp,
                       GlyphMetrics *outMetrics, size_t cap);

// True when the live TTF covers a codepoint (cmap probe for the baker).
// Always false on baked handles (no TTF to probe).
bool Font_coversCodepoint(const Font *font, uint32_t codepoint);

// --- Parallel SDF bake ------------------------------------------------------
//
// Raster (pure compute) and pack (mutating) are split so the baker can run
// raster workers across all cores while packing stays serial (one mutex or,
// as the baker does, a serial pack pass — which also keeps atlas layout
// deterministic regardless of core count). Raster only reads the shared
// fontinfo, so concurrent Font_rasterSdfWork calls are safe; pack and every
// other Font_* call must be externally serialized against each other.
typedef struct FontSdfWork {
    uint32_t codepoint;
    unsigned char *sdf; // stbtt buffer (free with Font_freeSdfWork)
    int w, h, xoff, yoff;
    int advanceW;
} FontSdfWork;

// SDF raster for a covered codepoint. False when uncovered/failed (sdf NULL).
bool Font_rasterSdfWork(Font *font, uint32_t codepoint, FontSdfWork *outWork);

// Packs a rastered glyph (SDF page spill included). False when the glyph is
// oversized or a page allocation fails. Fills outBase on success.
bool Font_packSdfWork(Font *font, const FontSdfWork *work, GlyphMetrics *outBase);

// Frees a work buffer (NULL-safe, zeroes the struct).
void Font_freeSdfWork(FontSdfWork *work);

// Indexes a packed glyph into the dictionary (used by the baker's serial
// pack pass; the runtime path goes through Font_getGlyph instead).
void Font_indexGlyph(Font *font, uint32_t codepoint, const GlyphMetrics *metrics);

// --- GPU-bake coverage ------------------------------------------------------
//
// Fast coverage bitmaps (no SDF math) for the jump-flood page baker: the
// baker packs coverage with gutters on CPU, floods whole pages on GPU, and
// derives SDF-rect metrics arithmetically (ink box + SDF_PADDING). Like the
// SDF split, raster is thread-safe, packing/indexing is caller-serialized.
typedef struct FontCovWork {
    uint32_t codepoint;
    uint8_t *bmp; // coverage 0..255, visual-top-first (free with Font_freeCovWork)
    int w, h;     // ink size (0x0 when empty — advance still valid)
    int x0, y0;   // ink origin in SDF-ref px (matches SDF xoff/yoff pre-pad)
    int advanceW; // raw advance units (caller scales by 128px ref scale)
} FontCovWork;

bool Font_rasterCoverageWork(Font *font, uint32_t codepoint, FontCovWork *outWork);
void Font_freeCovWork(FontCovWork *work);

// 128px reference scale (SDF_REF_HEIGHT equivalent for coverage metrics).
float Font_bakeRefScale(const Font *font);

// Constructs a dictionary-only handle from baked payloads. Takes ownership
// of nothing: atlasMono holds pageCount packed dim*dim pages and the metrics
// arrays are copied (each metrics.page indexes into the pages), then one
// texture per page is uploaded via Texture_loadRaw.
Font *Font_createFromBaked(const uint8_t *atlasMono, size_t pageCount,
                           int atlasDim,
                           const uint32_t *codepoints,
                           const GlyphMetrics *metrics, size_t count,
                           float ascent, float descent, float lineGap);

#endif // ANTI_FONT_H
