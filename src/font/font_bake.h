#ifndef ANTI_FONT_BAKE_H
#define ANTI_FONT_BAKE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// font/font_bake.h — baked font store (install-time SDF atlas + dictionary).
//
// The problem this solves: today every launch pays FontBook/CoreText lookup
// (System_getFontPath) plus per-glyph stbtt SDF raster on the render path.
// The store flips that to install-time work:
//
//   install:  FontBake_bakeAllFonts()  ->  ~/anti/fonts/<Family>.antifont
//   runtime:  Font_open("Family")      ->  mmap/fread + Texture_loadRaw
//
// A .antifont file (v2) is a finished multi-page atlas plus its glyph
// dictionary: put a name in, get renderable glyphs out, no FontBook
// round-trip, no TTF processing. The dictionary covers the font's full cmap
// (every codepoint the TTF maps, packed across 2048^2 pages — ~350 SDF
// glyphs per page, with no page ceiling). Codepoints outside the
// dictionary resolve at runtime through the platform color cascade (emoji)
// when the handle carries a family name — chat-bubble emoji works without
// prebaking thousands of glyphs.
//
// Staleness edge case (new fonts installed after the first bake):
// the header records the source TTF's mtime. FontBake_refreshAllFonts()
// re-stats every OS font and rebakes entries that are missing or whose
// source is newer than the baked file — so newly installed fonts get picked
// up, and updated fonts get rebaked. Call it on startup (cheap stat pass)
// and Font_open() heals single stale entries on demand.

typedef struct Font Font;

// Store location: <VexHome_root>/fonts (created on demand).
const char *FontBake_storeDir(void);

// Baked-file path for a family name (<store>/<Sanitized>.antifont).
// Returns true and fills outPath (FILE_PATH_MAX-style, pass 1024+ bytes).
bool FontBake_pathFor(const char *familyName, char *outPath, size_t maxLen);

// Bake one OS-installed family into the store (resolves via System_getFontPath,
// rasters the font's full cmap coverage as SDF, writes the .antifont).
// Overwrites any existing entry (v1 entries are also healed this way).
// True on success.
bool FontBake_bakeOne(const char *familyName);

// Install-time bulk bake: enumerates the OS font installer (Font Book on
// macOS, fontconfig/system dirs on Linux) and bakes every family that has no
// baked file yet. Returns the number of families newly baked. Idempotent:
// a second call with no new fonts installed bakes nothing.
size_t FontBake_bakeAllFonts(void);

// Refresh pass: enumerates OS fonts and bakes entries that are missing OR
// stale (source TTF mtime newer than the baked file / header mismatch).
// This is the "user installed a font after setup" edge case: new installs
// are missing entries, updates are stale entries, both get baked. Returns
// the number of families (re)baked.
size_t FontBake_refreshAllFonts(void);

// OS enumeration passthrough (Font Book / fontconfig): fills up to max
// entries, returns the installed family count. Powers --list and installers.
int FontBake_listInstalled(char families[][256], char paths[][1024], int max);

// --- Installer --------------------------------------------------------------
//
// Curated full-Font-Book install into ~/anti/fonts (an AntiHome-managed dir):
// enumerates every OS font family, bakes each one (full cmap, multi-page),
// verifies the written entry reloads, skips unresolvable families, and keeps
// going past individual failures. Call once at install time, then
// FontBake_refreshAllFonts() on later launches to pick up new/updated fonts.
//
// force=false behaves like refresh (missing + stale only, the normal path);
// force=true rebakes every family regardless of store state.
// progress (nullable) fires after each family: (family, done, total, user).
// Returns a per-run report; failed families are counted, never fatal.

typedef void (*FontBakeProgressFn)(const char *familyName, size_t done,
                                   size_t total, void *user);

typedef struct FontBakeInstallReport {
    size_t total;        // families enumerated from the OS
    size_t bakedNew;     // had no entry, baked + verified now
    size_t rebaked;      // had a stale entry, rebaked + verified now
    size_t alreadyFresh; // verified fresh without work
    size_t failed;       // bake or verify failed (logged, skipped)
} FontBakeInstallReport;

FontBakeInstallReport FontBake_installAll(bool force, FontBakeProgressFn progress,
                                          void *user);

// Store probes (no baking, no FontBook raster — pure stat/header reads).
bool FontBake_hasBaked(const char *familyName);
bool FontBake_isStale(const char *familyName);

// Plug-and-play open: name in, renderable font out.
//  1. Baked entry present and fresh -> Font_createFromBaked (fast path:
//     file read + one Texture_loadRaw, zero FontBook/stbtt work).
//  2. Missing/stale -> bake that one family now, then load it (heals the
//     store for next launch).
//  3. Not installed at all -> falls back to Font_loadSystem (live TTF).
// Returns NULL only when all three fail.
Font *Font_open(const char *familyName);

// Direct baked load with no healing and no fallback. NULL when the entry is
// missing, stale, or corrupt.
Font *Font_openBaked(const char *familyName);

#endif // ANTI_FONT_BAKE_H
