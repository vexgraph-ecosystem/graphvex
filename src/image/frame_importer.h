#ifndef IMAGE_FRAME_IMPORTER_H
#define IMAGE_FRAME_IMPORTER_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "image/image.h"

// image/frame_importer.h — the FrameImporter class (R1.5 decode seam).
//
// Pure bytes→Image upload: takes caller-decoded RGBA8 frames and lands
// them in a backend-agnostic Image. Owns NO decoder, spawns NOTHING —
// the CALLER runs the external decoder binary via the vexspoke
// ProcessSpawn shape and hands raw frames here. No popen, no libav*
// include/link, no threads, never called from tick/render paths.

typedef struct FrameImporter {
    uint32_t lastWidth;   // width of the last accepted frame (0 = none)
    uint32_t lastHeight;  // height of the last accepted frame (0 = none)
    uint64_t typeId;      // block-header type id (TYPE_FRAME_IMPORTER_SINGLETON)
} FrameImporter;

// Empty importer (no frame accepted yet).
FrameImporter *FrameImporter_0(void);

#define FrameImporter(...) CONSTRUCTOR_DISPATCH(FrameImporter, __VA_ARGS__)

// Release the importer block (null-safe no-op; never frees dest Images).
void FrameImporter_free(FrameImporter *self);

// Copy w*h*4 RGBA8 bytes into dest via Image_upload (dest-last per
// the Dest-Last Law). Records lastWidth/lastHeight on success. False on NULL
// self/bytes/dest or zero dims. Pure upload — decode happened before,
// in the caller's ProcessSpawn-driven binary.
bool FrameImporter_rgbaToTexture(FrameImporter *self, uint32_t w, uint32_t h,
                                 const uint8_t *bytes, Image *dest);

// Symmetric accessors (the Symmetric Getter/Setter Completeness Law; null-safe).
void FrameImporter_setLastWidth(FrameImporter *self, uint32_t w);
uint32_t FrameImporter_getLastWidth(const FrameImporter *self);
void FrameImporter_setLastHeight(FrameImporter *self, uint32_t h);
uint32_t FrameImporter_getLastHeight(const FrameImporter *self);

#endif
