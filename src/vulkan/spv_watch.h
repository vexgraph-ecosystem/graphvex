#ifndef GRAPHVEX_SPV_WATCH_H
#define GRAPHVEX_SPV_WATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annotation/intention.h"

// vulkan/spv_watch.h — SPV change detector (no Vulkan dependency).
//
// Shaders are data: loadSpvAny resolves each name through a precedence
// chain (bundle -> exe/spv -> CWD -> VEX_SPV_DIR). This watcher mirrors
// that chain for the core pipeline names, snapshots mtimes, and reports
// when a rebuild is due. The consumer (pipeline recreation on device-idle)
// calls Vk_reloadShaders; here detection is the seam.

#define SPV_WATCH_MAX_NAMES 8
;;INTENTION("the core pipeline shader catalog is a fixed, hand-maintained set of 8 bundled blobs; new core stages extend the list in source, never at runtime")

typedef struct SpvWatch SpvWatch;

SpvWatch *SpvWatch_init(void);
void SpvWatch_free(SpvWatch *w);

// Snapshot current mtimes. Returns false on OOM (watch unusable).
bool SpvWatch_snap(SpvWatch *w);

// True when any watched file changed since the last snap. After handling,
// call SpvWatch_snap again to re-baseline.
bool SpvWatch_changed(SpvWatch *w);

// Copy the changed name into out (up to outCap), or -1 when none.
int SpvWatch_changedName(SpvWatch *w, char *out, size_t outCap);

#endif
