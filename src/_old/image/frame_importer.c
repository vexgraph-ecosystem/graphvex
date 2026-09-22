#include "image/frame_importer.h"

#include <stdlib.h>

#include "nio/mem.h"
#include "../graphics/type.h"
#include "annotation/definition.h"
#include "annotation/intention.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: FrameImporter
 * ============================================================================
 * Media decoding ingress seam bridging externally decoded raw video or animation
 * RGBA8 byte frames directly into backend-agnostic Image containers. Decouples
 * pixel ingestion from third-party media libraries (such as libavformat or libavcodec)
 * by delegating media decoding to external child processes managed via ProcessSpawn.
 *
 * Importer instances execute zero steady-state heap allocations during playback,
 * passing raw frame byte buffers to Image_upload under strict dest-last convention.
 * The struct itself is allocated via the vexspoke typed memory arena
 * (TYPE_FRAME_IMPORTER_SINGLETON) with fallback to calloc for standalone environments.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: FrameImporter (image/frame_importer.c)
 * LEVEL: L2 — Behavior (R1.5 decode seam: pure bytes to Image upload)
 * ============================================================================
 * SUMMARY:
 *   Pure bytes to Image upload seam for media presentation layers. Accepts
 *   caller-decoded RGBA8 frames and commits them into a backend-agnostic Image
 *   via Image_upload. Owns no decoders and spawns no child threads: external
 *   decoders run asynchronously outside the steady-state render cadence.
 *
 * STRUCT FIELDS (Mirroring image/frame_importer.h):
 * ----------------------------------------------------------------------------
 *   uint32_t lastWidth;   // width of the last accepted frame (0 = none)
 *   uint32_t lastHeight;  // height of the last accepted frame (0 = none)
 *   uint64_t typeId;      // block-header type id (TYPE_FRAME_IMPORTER_SINGLETON)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - FrameImporter_0(void)                               : Empty frame importer instance
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - FrameImporter_free(self)                            : Release importer block memory
 *   - FrameImporter_rgbaToTexture(self, w, h, bytes, dest): Upload decoded bytes into destination Image
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - FrameImporter_setLastWidth(self, w)                 : Mutate recorded last frame width
 *   - FrameImporter_setLastHeight(self, h)                : Mutate recorded last frame height
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - FrameImporter_getLastWidth(self)                    : Query recorded last frame width
 *   - FrameImporter_getLastHeight(self)                   : Query recorded last frame height
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */
;;INTENTION("decode via binary spawn, no libav link: caller decodes with an external binary through ProcessSpawn, this class only uploads raw RGBA8")

// CONSTRUCTORS (PUBLIC & PRIVATE)

FrameImporter *FrameImporter_0(void) {
    FrameImporter *self = (FrameImporter*) Memory_alloc(TYPE_FRAME_IMPORTER_SINGLETON, sizeof(FrameImporter));
    if (!self)
        self = (FrameImporter*) calloc(1, sizeof(FrameImporter));
    if (!self)
        return nullptr;
    (*self).lastWidth = 0;
    (*self).lastHeight = 0;
    (*self).typeId = TYPE_FRAME_IMPORTER_SINGLETON;
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void FrameImporter_free(FrameImporter *self) {
    if (!self)
        return;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

bool FrameImporter_rgbaToTexture(FrameImporter *self, uint32_t w, uint32_t h,
                                 const uint8_t *bytes, Image *dest) {
    if (!self || !bytes || !dest)
        return false;
    if (w == 0 || h == 0)
        return false;
    if (!Image_upload(bytes, w, h, dest))
        return false;
    (*self).lastWidth = w;
    (*self).lastHeight = h;
    return true;
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void FrameImporter_setLastWidth(FrameImporter *self, uint32_t w) {
    if (!self)
        return;
    (*self).lastWidth = w;
}

;;SETTER
void FrameImporter_setLastHeight(FrameImporter *self, uint32_t h) {
    if (!self)
        return;
    (*self).lastHeight = h;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t FrameImporter_getLastWidth(const FrameImporter *self) {
    if (!self)
        return 0;
    return (*self).lastWidth;
}

;;GETTER
uint32_t FrameImporter_getLastHeight(const FrameImporter *self) {
    if (!self)
        return 0;
    return (*self).lastHeight;
}
