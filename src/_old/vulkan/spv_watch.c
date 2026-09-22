#include "vulkan/spv_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: SpvWatch
 * ============================================================================
 * Filesystem monitor and change detector for compiled SPIR-V shader binaries.
 * Tracks timestamp and file size signatures across the resolution path hierarchy
 * for core Vulkan pipelines in compliance with the Unified Graphics Abstraction Law.
 *
 * Provides non-blocking polling primitives enabling seamless shader hot reloading
 * during runtime debugging sessions without disrupting the primary render loop.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SpvWatch (src/vulkan/spv_watch.c)
 * LEVEL: L4 — Self-Management (shader change detector driving reload infra)
 * ============================================================================
 * Phase-2 SPV change detector: mtime snapshots over the loadSpvAny
 * precedence chain for the core pipeline shaders.
 *
 * Migrated from hotcwap/hot/spv_watch.c — SPV reload detection is
 * graphics work, so graphvex owns it. The dead hotcwap resolve leg now
 * points at the canonical blob dir (projects/graphvex/shader/spv/).
 *
 * STRUCT FIELDS (local to this file — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   uint64_t seen[SPV_WATCH_MAX_NAMES];          // mtime+size stamp per shader
 *   bool have[SPV_WATCH_MAX_NAMES];              // true once baseline snapped
 *   char resolved[SPV_WATCH_MAX_NAMES][512];     // resolved path per shader
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   file_id(path)                             : Compute combined mtime and size hash
 *   resolve_one(name, out, cap)               : Search directory precedence for shader
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - SpvWatch_init(void)                     : Allocate and baseline watcher
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - SpvWatch_free(w)                        : Free watcher memory
 *   - SpvWatch_snap(w)                        : Snapshot file modification timestamps
 *   - SpvWatch_changed(w)                     : Check if any watched file changed
 *   - SpvWatch_changedName(w, out, outCap)    : Identify changed shader filename
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
 *   - (none)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

static const char *s_names[SPV_WATCH_MAX_NAMES] = {
    "hello_triangle_vert.spv",
    "hello_triangle_frag.spv",
    "solid_quad_vert.spv",
    "solid_quad_frag.spv",
    "texture_quad_vert.spv",
    "texture_quad_frag.spv",
    "text_sdf_vert.spv",
    "text_sdf_frag.spv",
};

typedef struct SpvWatch {
    uint64_t seen[SPV_WATCH_MAX_NAMES];
    bool have[SPV_WATCH_MAX_NAMES];
    char resolved[SPV_WATCH_MAX_NAMES][512];
} SpvWatch;

static uint64_t file_id(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
#if defined(__APPLE__)
    uint64_t sec = (uint64_t) st.st_mtimespec.tv_sec;
    uint64_t nsec = (uint64_t) st.st_mtimespec.tv_nsec;
#else
    uint64_t sec = (uint64_t) st.st_mtime;
    uint64_t nsec = 0;
#endif
    return sec * 1000000000ULL + nsec + (uint64_t) st.st_size;
}

static bool resolve_one(const char *name, char *out, size_t cap) {
    char candidate[512];
    snprintf(candidate, sizeof(candidate), "spv/%s", name);
    if (access(candidate, R_OK) == 0) {
        strncpy(out, candidate, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }
    snprintf(candidate, sizeof(candidate), "projects/graphvex/shader/spv/%s", name);
    if (access(candidate, R_OK) == 0) {
        strncpy(out, candidate, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }
#ifdef VEX_SPV_DIR
    snprintf(candidate, sizeof(candidate), "%s/%s", VEX_SPV_DIR, name);
    if (access(candidate, R_OK) == 0) {
        strncpy(out, candidate, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }
#endif
    if (access(name, R_OK) == 0) {
        strncpy(out, name, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }
    return false;
}

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

SpvWatch *SpvWatch_init(void) {
    SpvWatch *w = (SpvWatch*) calloc(1, sizeof(SpvWatch));
    if (!w)
        return nullptr;
    SpvWatch_snap(w);
    return w;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void SpvWatch_free(SpvWatch *w) {
    if (!w)
        return;
    free(w);
}

bool SpvWatch_snap(SpvWatch *w) {
    if (!w)
        return false;
    for (uint32_t i = 0; i < SPV_WATCH_MAX_NAMES; i++) {
        char path[512];
        if (resolve_one(s_names[i], path, sizeof(path))) {
            strncpy((*w).resolved[i], path, sizeof((*w).resolved[i]) - 1);
            (*w).seen[i] = file_id(path);
            (*w).have[i] = true;
        } else {
            (*w).have[i] = false;
            (*w).seen[i] = 0;
        }
    }
    return true;
}

bool SpvWatch_changed(SpvWatch *w) {
    if (!w)
        return false;
    for (uint32_t i = 0; i < SPV_WATCH_MAX_NAMES; i++) {
        char path[512];
        bool found = resolve_one(s_names[i], path, sizeof(path));
        if (found != (*w).have[i])
            return true;
        if (found && file_id(path) != (*w).seen[i])
            return true;
    }
    return false;
}

int SpvWatch_changedName(SpvWatch *w, char *out, size_t outCap) {
    if (!w || !out || outCap == 0)
        return -1;
    for (uint32_t i = 0; i < SPV_WATCH_MAX_NAMES; i++) {
        char path[512];
        bool found = resolve_one(s_names[i], path, sizeof(path));
        if (found != (*w).have[i]) {
            strncpy(out, s_names[i], outCap - 1);
            out[outCap - 1] = '\0';
            return (int) i;
        }
        if (found && file_id(path) != (*w).seen[i]) {
            strncpy(out, s_names[i], outCap - 1);
            out[outCap - 1] = '\0';
            return (int) i;
        }
    }
    return -1;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

// (none)
