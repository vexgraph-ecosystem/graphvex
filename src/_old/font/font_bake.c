#include "font/font_bake.h"
#include "vulkan/sdf_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "font/font.h"
#include "io/vexhome.h"
#include "io/file.h"
#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: FontBake (install-time bake state, local structs)
 * LEVEL: L3 — Module Code (install-time bake business logic)
 * ============================================================================
 * Baked font store: install-time SDF atlas + glyph dictionary writer that
 * turns OS-installed TTFs into mmap-ready .antifont files, with CPU and
 * GPU bake flows plus staleness healing on refresh.
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   BakedEntry {           // On-disk .antifont dictionary entry (v2)
 *     uint32_t codepoint;  // Unicode codepoint key
 *     uint32_t page;       // Atlas page index owning this glyph
 *     float u0, v0, u1, v1; // UV bounds within the glyph's atlas page
 *     float width, height; // Pixel size of the glyph quad
 *     float xOffset, yOffset; // Drawing offset from cursor baseline
 *     float advance;       // Cursor X advance
 *   }
 *   BakeChunk {            // CPU SDF raster worker slice descriptor
 *     Font *font;          // Source live font (shared fontinfo, read-only)
 *     const uint32_t *cps; // Coverage list being rastered
 *     FontSdfWork *works;  // Output work buffers (one per codepoint)
 *     size_t count;        // Coverage list length
 *     int worker;          // This worker's strided index
 *     int workers;         // Total worker count (stride)
 *   }
 *   CovChunk {             // GPU-flow coverage raster worker slice descriptor
 *     Font *font;          // Source live font (shared fontinfo, read-only)
 *     const uint32_t *cps; // Coverage list being rastered
 *     FontCovWork *works;  // Output coverage buffers (one per codepoint)
 *     size_t count;        // Coverage list length
 *     int worker;          // This worker's strided index
 *     int workers;         // Total worker count (stride)
 *   }
 *   GpuCell {              // One packed cell in the GPU coverage page build
 *     uint32_t cp;         // Codepoint placed at this cell
 *     int ox, oy;          // Cell origin in the coverage page
 *     int inkW, inkH;      // Ink bitmap dimensions
 *     int x0, y0;          // Ink origin (== SDF xoff/yoff pre-pad)
 *     int adv;             // Raw advance units
 *   }
 *   GpuPageBuild {         // Coverage page under construction (GPU flow)
 *     uint8_t *cov;        // dim*dim coverage bytes under construction
 *     GpuCell *cells;      // Placed cells in this page
 *     size_t cellCount, cellCap; // Cell count and capacity
 *     int cursorX, cursorY, rowH; // Tight-pack cursor and row height
 *   }
 *   GpuEntry {             // GPU-flow dictionary entry accumulator
 *     uint32_t cp;         // Codepoint key
 *     GlyphMetrics m;      // Derived SDF-rect metrics for the file
 *   }
 *   GpuBuild {             // GPU-flow file accumulation (live Font stays empty)
 *     uint8_t **pageSdf;   // dim*dim mono SDF bytes per file page
 *     size_t pageCount, pageCap; // Page count and capacity
 *     GpuEntry *entries;   // Dictionary entries to write
 *     size_t entryCount, entryCap; // Entry count and capacity
 *     float ascent, descent, lineGap; // Cached vertical metrics
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - FontBake_storeDir(void)
 *   - FontBake_pathFor(familyName, outPath, maxLen)
 *   - FontBake_bakeOne(familyName)
 *   - FontBake_bakeAllFonts(void)
 *   - FontBake_refreshAllFonts(void)
 *   - FontBake_listInstalled(families[][256], paths[][1024], max)
 *   - FontBake_installAll(force, progress, user)
 *   - Font_open(familyName)
 *   - Font_openBaked(familyName)
 *
 * Getters:
 *   - FontBake_hasBaked(familyName)
 *   - FontBake_isStale(familyName)
 * ============================================================================
 */


// font/font_bake.c — baked font store implementation.
//
// File layout (.antifont v2, host-endian, same-machine install+run):
//   header:
//     magic u32 = 0x544E4641 ("AFNT"), version u32 = 2
//     atlasDim u32, pageCount u32, glyphCount u32, sourceMtime i64
//     ascent/descent/lineGap f32 (font units, cached for baked handles)
//     family[128] (NUL-padded UTF-8)
//   entries[glyphCount]: { codepoint u32, page u32, 9x f32 metrics }
//   atlases: pageCount packed atlasDim*atlasDim single-channel SDF pages
//
// Big coverages spill across pages: one 2048^2 page holds ~350 SDF glyphs at
// the 128px ref height, so a 10k-glyph coder font lands on ~29 pages. Each
// glyph names its page; each page uploads as its own bindless texture.

#define BAKE_MAGIC 0x544E4641u // "AFNT"
#define BAKE_VERSION 2u
#define BAKE_FAMILY_MAX 128
#define BAKE_ENUM_MAX 1024
#define BAKE_NAME_MAX 256
#define BAKE_PATH_MAX 1024
#define BAKE_GLYPH_MAX 65536

typedef struct BakedEntry {
    uint32_t codepoint;
    uint32_t page;
    float u0, v0, u1, v1;
    float width, height;
    float xOffset, yOffset;
    float advance;
} BakedEntry;

// One raster worker: strided slice of the coverage list into the parallel
// works array (stride spreads complex script blocks across all workers;
// contiguous chunks left CJK ranges on one core). Reads only shared
// fontinfo (thread-safe); packing happens later on the main thread.
typedef struct BakeChunk {
    Font *font;
    const uint32_t *cps;
    FontSdfWork *works;
    size_t count;
    int worker;
    int workers;
} BakeChunk;

static void *bakeChunkMain(void *arg) {
    BakeChunk *ch = (BakeChunk*) arg;
    for (size_t i = (size_t)(*ch).worker; i < (*ch).count; i += (size_t)(*ch).workers) {
        FontSdfWork *w = &(*ch).works[i];
        memset(w, 0, sizeof(*w));
        Font_rasterSdfWork((*ch).font, (*ch).cps[i], w);
    }
    return NULL;
}

// Coverage raster worker (GPU flow): fast bitmaps, no SDF math.
typedef struct CovChunk {
    Font *font;
    const uint32_t *cps;
    FontCovWork *works;
    size_t count;
    int worker;
    int workers;
} CovChunk;

static void *covChunkMain(void *arg) {
    CovChunk *ch = (CovChunk*) arg;
    for (size_t i = (size_t)(*ch).worker; i < (*ch).count; i += (size_t)(*ch).workers) {
        FontCovWork *w = &(*ch).works[i];
        memset(w, 0, sizeof(*w));
        Font_rasterCoverageWork((*ch).font, (*ch).cps[i], w);
    }
    return NULL;
}

static int bakeWorkerCount(size_t glyphs) {
    if (glyphs < 512)
        return 1;
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online < 1)
        online = 1;
    if (online > 32)
        online = 32;
    return (int)online;
}

// Threaded CPU flow: SDF raster across cores, serial tight pack.
// Used headless and as the fallback when GPU bake fails.
static void cpuBakeFlow(Font *font, const uint32_t *cov, size_t covCount,
                        const char *familyName) {
    FontSdfWork *works = (FontSdfWork*) calloc(covCount, sizeof(FontSdfWork));
    if (!works)
        return;
    int workers = bakeWorkerCount(covCount);
    if (workers > 1) {
        pthread_t *threads = (pthread_t*) malloc((size_t)workers * sizeof(pthread_t));
        BakeChunk *chunks = (BakeChunk*) malloc((size_t)workers * sizeof(BakeChunk));
        if (threads && chunks) {
            int launched = 0;
            for (int wI = 0; wI < workers; wI++) {
                chunks[launched] = (BakeChunk){ font, cov, works, covCount, wI, workers };
                if (pthread_create(&threads[launched], NULL, bakeChunkMain, &chunks[launched]) == 0)
                    launched++;
                else
                    bakeChunkMain(&chunks[launched]); // thread failed: run inline
            }
            for (int wI = 0; wI < launched; wI++)
                pthread_join(threads[wI], NULL);
        } else {
            workers = 1;
        }
        free(threads);
        free(chunks);
    }
    if (workers <= 1) {
        for (size_t i = 0; i < covCount; i++)
            Font_rasterSdfWork(font, cov[i], &works[i]); // false leaves sdf NULL
    }
    // Serial pack in codepoint order + dictionary index.
    for (size_t i = 0; i < covCount; i++) {
        if (works[i].sdf) {
            GlyphMetrics base;
            memset(&base, 0, sizeof(base));
            if (Font_packSdfWork(font, &works[i], &base)) {
                Font_indexGlyph(font, cov[i], &base);
            }
            Font_freeSdfWork(&works[i]);
        }
        if ((i & 0xFFF) == 0)
            printf("FontBake: '%s' ... %zu/%zu packed, %zu pages (%d workers, cpu)\n",
                   familyName, i, covCount, Font_pageCount(font), workers);
    }
    free(works);
}

// GPU flow: coverage bitmaps (threaded, cheap) packed with gutters on CPU,
// whole pages jump-flooded on GPU, metrics derived arithmetically. Returns
// false on any failure so the caller falls back to cpuBakeFlow.
#define GPU_GUTTER 16 // 2 * SDF padding: JFA bleed meets only at clamp edge
#define GPU_SDF_PAD 8

typedef struct GpuCell {
    uint32_t cp;
    int ox, oy;   // cell origin in the coverage page
    int inkW, inkH;
    int x0, y0;   // ink origin (== SDF xoff/yoff pre-pad)
    int adv;
} GpuCell;

typedef struct GpuPageBuild {
    uint8_t *cov;      // dim*dim coverage under construction
    GpuCell *cells;
    size_t cellCount, cellCap;
    int cursorX, cursorY, rowH;
} GpuPageBuild;

static void gpuPageFree(GpuPageBuild *pg) {
    if (!pg)
        return;
    free((*pg).cov);
    free((*pg).cells);
    memset(pg, 0, sizeof(*pg));
}

// GPU flow accumulation: SDF page bytes + dictionary entries, written by
// gpuWriteFile (the live Font stays empty — metrics index file pages).
typedef struct GpuEntry {
    uint32_t cp;
    GlyphMetrics m;
} GpuEntry;

typedef struct GpuBuild {
    uint8_t **pageSdf;   // dim*dim mono bytes per file page
    size_t pageCount, pageCap;
    GpuEntry *entries;
    size_t entryCount, entryCap;
    float ascent, descent, lineGap;
} GpuBuild;

static void gpuBuildFree(GpuBuild *b) {
    if (!b)
        return;
    for (size_t p = 0; p < (*b).pageCount; p++)
        free((*b).pageSdf[p]);
    free((*b).pageSdf);
    free((*b).entries);
    memset(b, 0, sizeof(*b));
}

static bool gpuBuildAddPage(GpuBuild *b, uint8_t *sdf) {
    if ((*b).pageCount >= (*b).pageCap) {
        size_t next = (*b).pageCap == 0 ? 4 : (*b).pageCap * 2;
        uint8_t **grown = (uint8_t**) realloc((*b).pageSdf, next * sizeof(uint8_t*));
        if (!grown)
            return false;
        (*b).pageSdf = grown;
        (*b).pageCap = next;
    }
    (*b).pageSdf[(*b).pageCount++] = sdf;
    return true;
}

static bool gpuBuildAddEntry(GpuBuild *b, uint32_t cp, const GlyphMetrics *m) {
    if ((*b).entryCount >= (*b).entryCap) {
        size_t next = (*b).entryCap == 0 ? 1024 : (*b).entryCap * 2;
        GpuEntry *grown = (GpuEntry*) realloc((*b).entries, next * sizeof(GpuEntry));
        if (!grown)
            return false;
        (*b).entries = grown;
        (*b).entryCap = next;
    }
    (*b).entries[(*b).entryCount].cp = cp;
    (*b).entries[(*b).entryCount].m = *m;
    (*b).entryCount++;
    return true;
}

// Writes a GPU-built font (explicit pages + entries, no live atlas).
static bool gpuWriteFile(const char *bakedPath, const char *familyName,
                         int64_t srcMtime, const GpuBuild *b, int dim) {
    if ((*b).entryCount == 0 || (*b).entryCount > BAKE_GLYPH_MAX)
        return false;
    // Every page holds at least one glyph, so pages can never outnumber
    // entries — the data-derived bound that replaces an arbitrary ceiling.
    if ((*b).pageCount == 0 || (*b).pageCount > (*b).entryCount)
        return false;
    FILE *f = fopen(bakedPath, "wb");
    if (!f)
        return false;
    uint32_t magic = BAKE_MAGIC, version = BAKE_VERSION;
    uint32_t atlasDim = (uint32_t)dim, pageCount = (uint32_t)(*b).pageCount;
    uint32_t glyphCount = (uint32_t)(*b).entryCount;
    char family[BAKE_FAMILY_MAX];
    memset(family, 0, sizeof(family));
    strncpy(family, familyName, sizeof(family) - 1);
    bool ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(&version, 4, 1, f) == 1 &&
              fwrite(&atlasDim, 4, 1, f) == 1 && fwrite(&pageCount, 4, 1, f) == 1 &&
              fwrite(&glyphCount, 4, 1, f) == 1 &&
              fwrite(&srcMtime, 8, 1, f) == 1 &&
              fwrite(&(*b).ascent, 4, 1, f) == 1 &&
              fwrite(&(*b).descent, 4, 1, f) == 1 &&
              fwrite(&(*b).lineGap, 4, 1, f) == 1 &&
              fwrite(family, 1, sizeof(family), f) == sizeof(family);
    for (size_t i = 0; ok && i < (*b).entryCount; i++) {
        BakedEntry e;
        e.codepoint = (*b).entries[i].cp;
        e.page = (uint32_t)(*b).entries[i].m.page;
        e.u0 = (*b).entries[i].m.u0; e.v0 = (*b).entries[i].m.v0;
        e.u1 = (*b).entries[i].m.u1; e.v1 = (*b).entries[i].m.v1;
        e.width = (*b).entries[i].m.width; e.height = (*b).entries[i].m.height;
        e.xOffset = (*b).entries[i].m.xOffset; e.yOffset = (*b).entries[i].m.yOffset;
        e.advance = (*b).entries[i].m.advance;
        ok = fwrite(&e, sizeof(e), 1, f) == 1;
    }
    size_t pageBytes = (size_t)dim * dim;
    for (size_t p = 0; ok && p < (*b).pageCount; p++)
        ok = fwrite((*b).pageSdf[p], 1, pageBytes, f) == pageBytes;
    fclose(f);
    return ok;
}

// Bakes one finished coverage page: JFA -> kept SDF bytes + entries.
// Returns false when the GPU step fails (caller runs the CPU fallback).
static bool gpuBakeFinishedPage(Font *font, GpuPageBuild *pg, float refScale,
                                const char *familyName, GpuBuild *build) {
    (void)font;
    int dim = SdfGpu_pageDim();
    uint8_t *sdf = (uint8_t*) malloc((size_t)dim * dim);
    if (!sdf)
        return false;
    if (!SdfGpu_bakePage((*pg).cov, dim, sdf)) {
        printf("FontBake: '%s' gpu page failed, falling back to cpu\n", familyName);
        free(sdf);
        return false;
    }
    for (size_t i = 0; i < (*pg).cellCount; i++) {
        GpuCell *c = &(*pg).cells[i];
        int rx = (*c).ox + GPU_GUTTER - GPU_SDF_PAD;
        int ry = (*c).oy + GPU_GUTTER - GPU_SDF_PAD;
        int rw = (*c).inkW + 2 * GPU_SDF_PAD;
        int rh = (*c).inkH + 2 * GPU_SDF_PAD;
        GlyphMetrics m;
        memset(&m, 0, sizeof(m));
        m.page = (int32_t)(*build).pageCount; // file page order
        m.color = 0;
        m.u0 = (float)rx / dim;
        m.v0 = (float)ry / dim;
        m.u1 = (float)(rx + rw) / dim;
        m.v1 = (float)(ry + rh) / dim;
        m.width = (float)rw;
        m.height = (float)rh;
        m.xOffset = (float)((*c).x0 - GPU_SDF_PAD);
        m.yOffset = (float)((*c).y0 - GPU_SDF_PAD);
        m.advance = (float)(*c).adv * refScale;
        if (!gpuBuildAddEntry(build, (*c).cp, &m)) {
            free(sdf);
            return false;
        }
    }
    if (!gpuBuildAddPage(build, sdf)) {
        free(sdf);
        return false;
    }
    return true;
}

static bool gpuBakeFlow(Font *font, const uint32_t *cov, size_t covCount,
                        const char *familyName, GpuBuild *build) {
    int dim = SdfGpu_pageDim();
    size_t pageBytes = (size_t)dim * dim;
    float refScale = Font_bakeRefScale(font);
    if (refScale <= 0.0f)
        return false;

    FontCovWork *works = (FontCovWork*) calloc(covCount, sizeof(FontCovWork));
    if (!works)
        return false;
    int workers = bakeWorkerCount(covCount);
    if (workers > 1) {
        pthread_t *threads = (pthread_t*) malloc((size_t)workers * sizeof(pthread_t));
        CovChunk *chunks = (CovChunk*) malloc((size_t)workers * sizeof(CovChunk));
        if (threads && chunks) {
            int launched = 0;
            for (int wI = 0; wI < workers; wI++) {
                chunks[launched] = (CovChunk){ font, cov, works, covCount, wI, workers };
                if (pthread_create(&threads[launched], NULL, covChunkMain, &chunks[launched]) == 0)
                    launched++;
                else
                    covChunkMain(&chunks[launched]);
            }
            for (int wI = 0; wI < launched; wI++)
                pthread_join(threads[wI], NULL);
        }
        free(threads);
        free(chunks);
    } else {
        for (size_t i = 0; i < covCount; i++)
            Font_rasterCoverageWork(font, cov[i], &works[i]);
    }

    // Serial gutter pack; each finished page bakes immediately (streaming:
    // peak is one coverage page + coverage backlog, not all pages).
    GpuPageBuild page;
    memset(&page, 0, sizeof(page));
    for (size_t i = 0; i < covCount; i++) {
        FontCovWork *w = &works[i];
        if ((*w).w <= 0 || (*w).h <= 0 || !(*w).bmp) {
            Font_freeCovWork(w); // empty (space): same skip as the CPU path
            continue;
        }
        int cellW = (*w).w + 2 * GPU_GUTTER;
        int cellH = (*w).h + 2 * GPU_GUTTER;
        if (cellW > dim || cellH > dim) {
            Font_freeCovWork(w);
            continue;
        }
        // Row wrap within the page.
        if (page.cov && page.cursorX + cellW > dim) {
            page.cursorX = 0;
            page.cursorY += page.rowH;
            page.rowH = 0;
        }
        // Page overflow (or first cell): bake the finished page, open fresh.
        if (!page.cov || page.cursorY + cellH > dim) {
            if (page.cov && page.cellCount > 0) {
                if (!gpuBakeFinishedPage(font, &page, refScale, familyName, build)) {
                    gpuPageFree(&page);
                    for (size_t j = i; j < covCount; j++)
                        Font_freeCovWork(&works[j]);
                    free(works);
                    return false;
                }
            }
            gpuPageFree(&page);
            page.cov = (uint8_t*) calloc(pageBytes, 1);
            page.cells = NULL;
            page.cellCount = page.cellCap = 0;
            page.cursorX = page.cursorY = page.rowH = 0;
            if (!page.cov) {
                for (size_t j = i; j < covCount; j++)
                    Font_freeCovWork(&works[j]);
                free(works);
                return false;
            }
        }
        int ox = page.cursorX, oy = page.cursorY;
        for (int y = 0; y < (*w).h; y++) {
            memcpy(&page.cov[((size_t)(oy + GPU_GUTTER + y) * dim) + ox + GPU_GUTTER],
                   &(*w).bmp[(size_t)y * (*w).w], (size_t)(*w).w);
        }
        if (page.cellCount >= page.cellCap) {
            size_t next = page.cellCap == 0 ? 256 : page.cellCap * 2;
            GpuCell *grown = (GpuCell*) realloc(page.cells, next * sizeof(GpuCell));
            if (!grown) {
                Font_freeCovWork(w);
                gpuPageFree(&page);
                for (size_t j = i + 1; j < covCount; j++)
                    Font_freeCovWork(&works[j]);
                free(works);
                return false;
            }
            page.cells = grown;
            page.cellCap = next;
        }
        page.cells[page.cellCount++] = (GpuCell){
            cov[i], ox, oy, (*w).w, (*w).h, (*w).x0, (*w).y0, (*w).advanceW
        };
        page.cursorX += cellW;
        if (cellH > page.rowH)
            page.rowH = cellH;
        Font_freeCovWork(w);
        if ((i & 0xFFF) == 0)
            printf("FontBake: '%s' ... %zu/%zu coverage packed, %zu file pages (%d workers, gpu)\n",
                   familyName, i, covCount, (*build).pageCount, workers);
    }
    free(works);
    if (page.cov && page.cellCount > 0) {
        if (!gpuBakeFinishedPage(font, &page, refScale, familyName, build)) {
            gpuPageFree(&page);
            return false;
        }
    }
    gpuPageFree(&page);
    return true;
}

extern char *System_getFontPath(const char *familyName);
// Implemented per-OS next to the FontBook knowledge:
//   Apple  -> src/objc/font_cocoa.m (NSFontManager families + CTFont URL)
//   others -> Linux enumerator below (fontconfig, then font-dir scan).
extern int System_listInstalledFonts(char families[][BAKE_NAME_MAX],
                                     char paths[][BAKE_PATH_MAX], int max);

static void sanitizeFamily(const char *family, char *out, size_t maxLen) {
    if (!family || !out || maxLen == 0)
        return;
    size_t j = 0;
    for (size_t i = 0; family[i] != '\0' && j + 1 < maxLen && j < 96; i++) {
        char c = family[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        out[j++] = ok ? c : '_';
    }
    if (j == 0 && maxLen > 1) {
        out[j++] = '_';
    }
    out[j] = '\0';
}

const char *FontBake_storeDir(void) {
    return VexHome_fonts();
}

bool FontBake_pathFor(const char *familyName, char *outPath, size_t maxLen) {
    if (!familyName || !familyName[0] || !outPath || maxLen == 0)
        return false;
    char safe[128];
    sanitizeFamily(familyName, safe, sizeof(safe));
    int n = snprintf(outPath, maxLen, "%s/%s.antifont", FontBake_storeDir(), safe);
    return n > 0 && (size_t)n < maxLen;
}

static bool sourceMtime(const char *path, int64_t *outMtime) {
    struct stat st;
    if (!path || stat(path, &st) != 0)
        return false;
    if (outMtime)
        *outMtime = (int64_t)st.st_mtime;
    return true;
}

static bool readHeader(const char *bakedPath, uint32_t *outGlyphCount,
                       int64_t *outSourceMtime) {
    FILE *f = fopen(bakedPath, "rb");
    if (!f)
        return false;
    uint32_t magic = 0, version = 0, atlasDim = 0, pageCount = 0, glyphCount = 0;
    int64_t srcMtime = 0;
    bool ok = fread(&magic, 4, 1, f) == 1 && fread(&version, 4, 1, f) == 1 &&
              fread(&atlasDim, 4, 1, f) == 1 && fread(&pageCount, 4, 1, f) == 1 &&
              fread(&glyphCount, 4, 1, f) == 1 &&
              fread(&srcMtime, 8, 1, f) == 1;
    fclose(f);
    // v1 files fail the version check on purpose: they read as missing/stale
    // so the next bakeAll/refresh pass rebakes them into v2.
    if (!ok || magic != BAKE_MAGIC || version != BAKE_VERSION ||
        atlasDim != (uint32_t)Font_atlasDim(NULL) ||
        pageCount == 0 || pageCount > glyphCount ||
        glyphCount > BAKE_GLYPH_MAX)
        return false;
    if (outGlyphCount)
        *outGlyphCount = glyphCount;
    if (outSourceMtime)
        *outSourceMtime = srcMtime;
    return true;
}

bool FontBake_hasBaked(const char *familyName) {
    char path[BAKE_PATH_MAX];
    if (!FontBake_pathFor(familyName, path, sizeof(path)))
        return false;
    uint32_t count = 0;
    return readHeader(path, &count, NULL);
}

bool FontBake_isStale(const char *familyName) {
    char bakedPath[BAKE_PATH_MAX];
    if (!FontBake_pathFor(familyName, bakedPath, sizeof(bakedPath)))
        return true;
    int64_t headerMtime = 0;
    if (!readHeader(bakedPath, NULL, &headerMtime))
        return true; // missing or corrupt -> treat as stale so refresh heals it
    char *srcPath = System_getFontPath(familyName);
    if (!srcPath)
        return false; // source vanished: keep the baked copy, not stale
    int64_t liveMtime = 0;
    bool haveLive = sourceMtime(srcPath, &liveMtime);
    free(srcPath);
    if (!haveLive)
        return false;
    if (liveMtime != headerMtime)
        return true;
    struct stat bst;
    if (stat(bakedPath, &bst) == 0 && liveMtime > (int64_t)bst.st_mtime)
        return true;
    return false;
}

static bool writeBakedFile(const char *bakedPath, const char *familyName,
                           int64_t srcMtime, Font *font) {
    float ascent = 0.0f, descent = 0.0f, lineGap = 0.0f;
    Font_getVMetrics(font, &ascent, &descent, &lineGap);

    size_t glyphTotal = Font_glyphCount(font);
    size_t pageTotal = Font_pageCount(font);
    if (glyphTotal == 0 || glyphTotal > BAKE_GLYPH_MAX)
        return false;
    if (pageTotal == 0 || pageTotal > glyphTotal)
        return false;
    uint32_t *cps = (uint32_t*) malloc(glyphTotal * sizeof(uint32_t));
    GlyphMetrics *ms = (GlyphMetrics*) malloc(glyphTotal * sizeof(GlyphMetrics));
    if (!cps || !ms) {
        free(cps);
        free(ms);
        return false;
    }
    size_t got = Font_copyGlyphs(font, cps, ms, glyphTotal);

    int dim = Font_atlasDim(font);
    size_t pageBytes = (size_t)dim * (size_t)dim;
    uint8_t *mono = (uint8_t*) malloc(pageTotal * pageBytes);
    if (!mono) {
        free(cps);
        free(ms);
        return false;
    }
    for (size_t p = 0; p < pageTotal; p++)
        Font_copyAtlasMonoPage(font, p, mono + p * pageBytes);

    FILE *f = fopen(bakedPath, "wb");
    if (!f) {
        free(cps);
        free(ms);
        free(mono);
        return false;
    }
    uint32_t magic = BAKE_MAGIC, version = BAKE_VERSION;
    uint32_t atlasDim = (uint32_t)dim, pageCount = (uint32_t)pageTotal;
    uint32_t glyphCount = (uint32_t)got;
    char family[BAKE_FAMILY_MAX];
    memset(family, 0, sizeof(family));
    strncpy(family, familyName, sizeof(family) - 1);

    bool ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(&version, 4, 1, f) == 1 &&
              fwrite(&atlasDim, 4, 1, f) == 1 && fwrite(&pageCount, 4, 1, f) == 1 &&
              fwrite(&glyphCount, 4, 1, f) == 1 &&
              fwrite(&srcMtime, 8, 1, f) == 1 && fwrite(&ascent, 4, 1, f) == 1 &&
              fwrite(&descent, 4, 1, f) == 1 && fwrite(&lineGap, 4, 1, f) == 1 &&
              fwrite(family, 1, sizeof(family), f) == sizeof(family);
    for (size_t i = 0; ok && i < got; i++) {
        BakedEntry e;
        e.codepoint = cps[i];
        e.page = (uint32_t)ms[i].page;
        e.u0 = ms[i].u0; e.v0 = ms[i].v0; e.u1 = ms[i].u1; e.v1 = ms[i].v1;
        e.width = ms[i].width; e.height = ms[i].height;
        e.xOffset = ms[i].xOffset; e.yOffset = ms[i].yOffset;
        e.advance = ms[i].advance;
        ok = fwrite(&e, sizeof(e), 1, f) == 1;
    }
    if (ok)
        ok = fwrite(mono, 1, pageTotal * pageBytes, f) == pageTotal * pageBytes;
    fclose(f);
    free(cps);
    free(ms);
    free(mono);
    return ok;
}

bool FontBake_bakeOne(const char *familyName) {
    if (!familyName || !familyName[0])
        return false;
    char *srcPath = System_getFontPath(familyName);
    if (!srcPath) {
        printf("FontBake: no OS font for '%s'\n", familyName);
        return false;
    }
    int64_t srcMtime = 0;
    sourceMtime(srcPath, &srcMtime);

    Font *font = Font_load(srcPath);
    free(srcPath);
    if (!font) {
        printf("FontBake: failed to load '%s'\n", familyName);
        return false;
    }
    // 1. Coverage list: every codepoint the TTF's cmap maps (serial cmap
    //    probes are cheap; the SDF math below is what gets threaded).
    //    Surrogates skipped.
    size_t covCap = 4096, covCount = 0;
    uint32_t *cov = (uint32_t*) malloc(covCap * sizeof(uint32_t));
    if (!cov) {
        Font_free(font);
        return false;
    }
    for (uint32_t cp = 0x20; cp <= 0x10FFFFu; cp++) {
        if (cp >= 0xD800u && cp <= 0xDFFFu)
            continue;
        if (!Font_coversCodepoint(font, cp))
            continue;
        if (covCount >= covCap) {
            size_t next = covCap * 2;
            uint32_t *grown = (uint32_t*) realloc(cov, next * sizeof(uint32_t));
            if (!grown)
                break;
            cov = grown;
            covCap = next;
        }
        cov[covCount++] = cp;
    }
    if (covCount == 0) {
        printf("FontBake: '%s' maps no codepoints, skipping\n", familyName);
        free(cov);
        Font_free(font);
        return false;
    }
    // 2. Fast path: GPU jump-flood when Vulkan is up (install-time engine);
    //    headless processes and any GPU failure take the threaded CPU flow.
    bool useGpu = SdfGpu_available();
    GpuBuild build;
    memset(&build, 0, sizeof(build));
    if (useGpu) {
        Font_getVMetrics(font, &build.ascent, &build.descent, &build.lineGap);
        printf("FontBake: '%s' baking on gpu (%zu glyphs)\n", familyName, covCount);
        if (!gpuBakeFlow(font, cov, covCount, familyName, &build)) {
            printf("FontBake: '%s' gpu flow failed, falling back to cpu\n", familyName);
            gpuBuildFree(&build);
            useGpu = false;
        }
    }
    if (!useGpu)
        cpuBakeFlow(font, cov, covCount, familyName);
    free(cov);

    File_mkdirs(FontBake_storeDir());
    char bakedPath[BAKE_PATH_MAX];
    if (!FontBake_pathFor(familyName, bakedPath, sizeof(bakedPath))) {
        gpuBuildFree(&build);
        Font_free(font);
        return false;
    }
    bool ok;
    if (useGpu) {
        ok = gpuWriteFile(bakedPath, familyName, srcMtime, &build, SdfGpu_pageDim());
        printf("FontBake: %s '%s' -> %s (%zu glyphs, %zu pages, gpu)\n", ok ? "baked" : "FAILED",
               familyName, bakedPath, build.entryCount, build.pageCount);
        gpuBuildFree(&build);
    } else {
        ok = writeBakedFile(bakedPath, familyName, srcMtime, font);
        printf("FontBake: %s '%s' -> %s (%zu glyphs, %zu pages, cpu)\n", ok ? "baked" : "FAILED",
               familyName, bakedPath, Font_glyphCount(font), Font_pageCount(font));
    }
    Font_free(font);
    return ok;
}

Font *Font_openBaked(const char *familyName) {
    char bakedPath[BAKE_PATH_MAX];
    if (!FontBake_pathFor(familyName, bakedPath, sizeof(bakedPath)))
        return NULL;
    FILE *f = fopen(bakedPath, "rb");
    if (!f)
        return NULL;
    uint32_t magic = 0, version = 0, atlasDim = 0, pageCount = 0, glyphCount = 0;
    int64_t srcMtime = 0;
    float ascent = 0.0f, descent = 0.0f, lineGap = 0.0f;
    char family[BAKE_FAMILY_MAX];
    bool ok = fread(&magic, 4, 1, f) == 1 && fread(&version, 4, 1, f) == 1 &&
              fread(&atlasDim, 4, 1, f) == 1 && fread(&pageCount, 4, 1, f) == 1 &&
              fread(&glyphCount, 4, 1, f) == 1 &&
              fread(&srcMtime, 8, 1, f) == 1 && fread(&ascent, 4, 1, f) == 1 &&
              fread(&descent, 4, 1, f) == 1 && fread(&lineGap, 4, 1, f) == 1 &&
              fread(family, 1, sizeof(family), f) == sizeof(family);
    (void)srcMtime;
    if (!ok || magic != BAKE_MAGIC || version != BAKE_VERSION ||
        atlasDim != (uint32_t)Font_atlasDim(NULL) ||
        pageCount == 0 || glyphCount == 0 || pageCount > glyphCount ||
        glyphCount > BAKE_GLYPH_MAX) {
        fclose(f);
        return NULL;
    }
    uint32_t *cps = (uint32_t*) Memory_alloc(TYPE_ARRAY, glyphCount * sizeof(uint32_t));
    GlyphMetrics *ms = (GlyphMetrics*) Memory_alloc(TYPE_ARRAY, glyphCount * sizeof(GlyphMetrics));
    size_t pageBytes = (size_t)atlasDim * atlasDim;
    uint8_t *mono = (uint8_t*) Memory_alloc(TYPE_ARRAY, (size_t)pageCount * pageBytes);
    if (!cps || !ms || !mono) {
        if (cps) Memory_free(cps);
        if (ms) Memory_free(ms);
        if (mono) Memory_free(mono);
        fclose(f);
        return NULL;
    }
    for (uint32_t i = 0; i < glyphCount; i++) {
        BakedEntry e;
        if (fread(&e, sizeof(e), 1, f) != 1) {
            ok = false;
            break;
        }
        cps[i] = e.codepoint;
        if (e.page >= pageCount) {
            ok = false;
            break;
        }
        ms[i].page = (int32_t)e.page;
        ms[i].u0 = e.u0; ms[i].v0 = e.v0; ms[i].u1 = e.u1; ms[i].v1 = e.v1;
        ms[i].width = e.width; ms[i].height = e.height;
        ms[i].xOffset = e.xOffset; ms[i].yOffset = e.yOffset;
        ms[i].advance = e.advance;
    }
    if (ok)
        ok = fread(mono, 1, (size_t)pageCount * pageBytes, f) == (size_t)pageCount * pageBytes;
    fclose(f);
    Font *font = NULL;
    if (ok)
        font = Font_createFromBaked(mono, pageCount, (int)atlasDim, cps, ms, glyphCount,
                                    ascent, descent, lineGap);
    if (font)
        Font_setFamily(font, family[0] ? family : familyName); // enables runtime color cascade
    Memory_free(cps);
    Memory_free(ms);
    Memory_free(mono);
    return font;
}

Font *Font_open(const char *familyName) {
    if (!familyName || !familyName[0])
        return NULL;
    if (FontBake_hasBaked(familyName) && !FontBake_isStale(familyName)) {
        Font *baked = Font_openBaked(familyName);
        if (baked)
            return baked;
    }
    if (!FontBake_hasBaked(familyName) || FontBake_isStale(familyName)) {
        if (FontBake_bakeOne(familyName)) {
            Font *baked = Font_openBaked(familyName);
            if (baked)
                return baked;
        }
    }
    return Font_loadSystem(familyName);
}

int FontBake_listInstalled(char families[][256], char paths[][1024], int max) {
    if (!families || !paths || max <= 0)
        return 0;
    if (max > BAKE_ENUM_MAX)
        max = BAKE_ENUM_MAX;
    static char fam[BAKE_ENUM_MAX][BAKE_NAME_MAX];
    static char pth[BAKE_ENUM_MAX][BAKE_PATH_MAX];
    int n = System_listInstalledFonts(fam, pth, max);
    for (int i = 0; i < n; i++) {
        strncpy(families[i], fam[i], 255);
        families[i][255] = '\0';
        strncpy(paths[i], pth[i], 1023);
        paths[i][1023] = '\0';
    }
    return n;
}

typedef enum BakeSweepMode {
    BAKE_SWEEP_MISSING_ONLY,     // bakeAllFonts: fill gaps, never touch fresh/stale
    BAKE_SWEEP_MISSING_OR_STALE, // refresh/install: heal new installs + updates
    BAKE_SWEEP_FORCE_ALL,        // install(force): rebake everything
} BakeSweepMode;

static FontBakeInstallReport bakeSweep(BakeSweepMode mode,
                                       FontBakeProgressFn progress, void *user) {
    FontBakeInstallReport report;
    memset(&report, 0, sizeof(report));
    static char families[BAKE_ENUM_MAX][BAKE_NAME_MAX];
    static char paths[BAKE_ENUM_MAX][BAKE_PATH_MAX];
    VexHome_ensure();
    File_mkdirs(FontBake_storeDir());
    int n = System_listInstalledFonts(families, paths, BAKE_ENUM_MAX);
    if (n <= 0)
        return report;
    report.total = (size_t)n;
    for (int i = 0; i < n; i++) {
        bool missing = !FontBake_hasBaked(families[i]);
        bool stale = !missing && FontBake_isStale(families[i]);
        bool want = missing;
        if (mode == BAKE_SWEEP_MISSING_OR_STALE)
            want = missing || stale;
        else if (mode == BAKE_SWEEP_FORCE_ALL)
            want = true;
        // Curation: fresh entries are header-verified, never rebaked unless
        // forced; missing entries cover fonts installed after setup; stale
        // entries cover fonts updated since bake. Failures count and skip.
        if (!want) {
            if (FontBake_hasBaked(families[i])) {
                report.alreadyFresh++;
            } else {
                report.failed++;
            }
        } else if (FontBake_bakeOne(families[i]) && FontBake_hasBaked(families[i])) {
            if (missing)
                report.bakedNew++;
            else
                report.rebaked++;
        } else {
            printf("FontBake: install: skipping '%s' (bake or verify failed)\n",
                   families[i]);
            report.failed++;
        }
        if (progress)
            progress(families[i], (size_t)(i + 1), (size_t)n, user);
    }
    printf("FontBake: install: %zu new, %zu rebaked, %zu fresh, %zu failed / %zu installed\n",
           report.bakedNew, report.rebaked, report.alreadyFresh,
           report.failed, report.total);
    return report;
}

size_t FontBake_bakeAllFonts(void) {
    FontBakeInstallReport r = bakeSweep(BAKE_SWEEP_MISSING_ONLY, NULL, NULL);
    return r.bakedNew;
}

size_t FontBake_refreshAllFonts(void) {
    FontBakeInstallReport r = bakeSweep(BAKE_SWEEP_MISSING_OR_STALE, NULL, NULL);
    return r.bakedNew + r.rebaked;
}

FontBakeInstallReport FontBake_installAll(bool force, FontBakeProgressFn progress,
                                          void *user) {
    return bakeSweep(force ? BAKE_SWEEP_FORCE_ALL : BAKE_SWEEP_MISSING_OR_STALE,
                     progress, user);
}

// --- Non-Apple OS font enumeration (fontconfig, then font-dir scan) ---------
#ifndef __APPLE__
// No platform color raster off-Apple yet: color cascade misses return false.
bool System_rasterColorGlyph(const char *familyName, uint32_t codepoint,
                             int pixelHeight, uint8_t **outRGBA,
                             int *outW, int *outH,
                             float *outXOff, float *outYOff,
                             float *outAdvance) {
    (void)familyName; (void)codepoint; (void)pixelHeight; (void)outRGBA;
    (void)outW; (void)outH; (void)outXOff; (void)outYOff; (void)outAdvance;
    return false;
}

static int dedupeAdd(char families[][BAKE_NAME_MAX], char paths[][BAKE_PATH_MAX],
                     int count, const char *family, const char *path) {
    if (!family || !family[0] || !path || !path[0])
        return count;
    if (count >= BAKE_ENUM_MAX)
        return count;
    for (int i = 0; i < count; i++) {
        if (strncmp(families[i], family, BAKE_NAME_MAX) == 0)
            return count;
    }
    strncpy(families[count], family, BAKE_NAME_MAX - 1);
    families[count][BAKE_NAME_MAX - 1] = '\0';
    strncpy(paths[count], path, BAKE_PATH_MAX - 1);
    paths[count][BAKE_PATH_MAX - 1] = '\0';
    return count + 1;
}

static void trim(char *s) {
    if (!s)
        return;
    while (*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

static int scanFontDir(const char *dir, char families[][BAKE_NAME_MAX],
                       char paths[][BAKE_PATH_MAX], int count) {
    DIR *d = opendir(dir);
    if (!d)
        return count;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < BAKE_ENUM_MAX) {
        if ((*ent).d_name[0] == '.')
            continue;
        char full[BAKE_PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", dir, (*ent).d_name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            count = scanFontDir(full, families, paths, count);
            continue;
        }
        const char *dot = strrchr((*ent).d_name, '.');
        if (!dot)
            continue;
        if (strcasecmp(dot, ".ttf") != 0 && strcasecmp(dot, ".otf") != 0 &&
            strcasecmp(dot, ".ttc") != 0)
            continue;
        char family[BAKE_NAME_MAX];
        size_t baseLen = (size_t)(dot - (*ent).d_name);
        if (baseLen >= sizeof(family))
            baseLen = sizeof(family) - 1;
        memcpy(family, (*ent).d_name, baseLen);
        family[baseLen] = '\0';
        count = dedupeAdd(families, paths, count, family, full);
    }
    closedir(d);
    return count;
}

int System_listInstalledFonts(char families[][BAKE_NAME_MAX],
                              char paths[][BAKE_PATH_MAX], int max) {
    if (!families || !paths || max <= 0)
        return 0;
    int count = 0;
    int cap = max < BAKE_ENUM_MAX ? max : BAKE_ENUM_MAX;
    FILE *fc = popen("fc-list : family file 2>/dev/null", "r");
    if (fc) {
        char line[2048];
        while (fgets(line, sizeof(line), fc) && count < cap) {
            char *colon = strchr(line, ':');
            if (!colon)
                continue;
            *colon = '\0';
            char *file = colon + 1;
            trim(line);
            trim(file);
            if (!line[0] || !file[0])
                continue;
            char *comma = strchr(line, ',');
            if (comma)
                *comma = '\0';
            trim(line);
            count = dedupeAdd(families, paths, count, line, file);
        }
        pclose(fc);
    }
    if (count > 0)
        return count;
    const char *dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        NULL,
    };
    for (int i = 0; dirs[i] && count < cap; i++)
        count = scanFontDir(dirs[i], families, paths, count);
    const char *home = getenv("HOME");
    if (home && home[0]) {
        char u1[BAKE_PATH_MAX], u2[BAKE_PATH_MAX];
        snprintf(u1, sizeof(u1), "%s/.fonts", home);
        snprintf(u2, sizeof(u2), "%s/.local/share/fonts", home);
        if (count < cap)
            count = scanFontDir(u1, families, paths, count);
        if (count < cap)
            count = scanFontDir(u2, families, paths, count);
    }
    return count;
}
#endif
