#ifndef GRAPHVEX_COMPOSITOR_SUBMIT_H
#define GRAPHVEX_COMPOSITOR_SUBMIT_H

#include "compositor/compositor.h"
#include "graphics/graphics.h"

// Export a CPU group and record it at its absolute origin, without fitting it
// to a layout/event rectangle. On success, outImage receives a newly owned Image
// borrowed by the list. Keep it alive and unchanged until submission completes.
// Existing outImage ownership is not replaced/freed; caller manages prior values.
// Empty surfaces and null arguments reject. Failure leaves outImage unchanged.
// List append uses the existing DisplayList allocation contract; list contents
// are not promised failure-atomic on its allocation failure. External exclusion
// is required, as for DisplayList/Graphics. This is a CPU-to-image bridge, not
// GPU float filtering or end-to-end linear-light presentation.
CompositorStatus Compositor_record(const CompositorSurface *surface,
                                   DisplayList *list, Image **outImage);

#endif
