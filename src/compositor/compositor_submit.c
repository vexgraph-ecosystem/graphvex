#include "compositor/compositor_submit.h"
#include "compositor/compositor_image.h"
#include "image.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/* CompositorSubmit preserves a filtered group's world origin while bridging
 * owned linear-premultiplied CPU output into a borrowed display-list image. */
;;OVERVIEW
/* Public seam: Compositor_record(surface, list, outImage). Conversion owns the
 * new Image; successful recording transfers it to the caller. Invalid inputs
 * and export/append failures leave outImage unchanged. No global backend switch,
 * fitting transform, GPU work or retained compositor-surface borrow occurs. */

// Converts a nonempty world-origin surface to an owned Image and appends it to the display list.
CompositorStatus Compositor_record(const CompositorSurface *surface,
                                   DisplayList *list, Image **outImage) {
    if (!surface || !list || !outImage)
        return COMPOSITOR_INVALID;
    CompositorBounds bounds = CompositorSurface_bounds(surface);
    if (!bounds.width || !bounds.height)
        return COMPOSITOR_INVALID;
    // Rect uses binary32; reject placements which would lose native-pixel origin
    // or extent precision rather than silently shifting the filtered group.
    Rect destination = {(float) bounds.x, (float) bounds.y,
                        (float) bounds.width, (float) bounds.height};
    if ((double) destination.x != bounds.x || (double) destination.y != bounds.y ||
        (double) destination.w != bounds.width || (double) destination.h != bounds.height ||
        (double) (destination.x + destination.w) != (int64_t) bounds.x + bounds.width ||
        (double) (destination.y + destination.h) != (int64_t) bounds.y + bounds.height)
        return COMPOSITOR_LIMIT;
    Image *image = nullptr;
    CompositorStatus status = CompositorSurface_toImage(surface, &image);
    if (status != COMPOSITOR_OK)
        return status;
    size_t count = DisplayList_count(list);
    Rect source = {0, 0, destination.w, destination.h};
    DisplayList_image(list, image, source, destination);
    if (DisplayList_count(list) != count + 1) {
        Image_destroy(image);
        return COMPOSITOR_NO_MEMORY;
    }
    *outImage = image;
    return COMPOSITOR_OK;
}
