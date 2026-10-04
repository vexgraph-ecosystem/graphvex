#ifndef GRAPHVEX_COMPOSITOR_SCOPE_H
#define GRAPHVEX_COMPOSITOR_SCOPE_H

#include "compositor/compositor.h"

/* Cold submission record, not a retained class: every pointer is borrowed only
 * for the duration of scopedScene. priorScene is the exact painter-prefix,
 * before decoration/foreground/later siblings; caller owns that selection.
 * There are no implicit Element attachments or recursive tree discovery. */
typedef struct CompositorScopeDesc {
    const CompositorSurface *priorScene;
    const CompositorSurface *decoration; /* optional panel background/border */
    const CompositorSurface *const *foreground;
    size_t foregroundCount;
    CompositorBounds panelBounds;
    uint32_t radius; /* prototype supports rectangle radius 0 only */
    const FilterToken *backdropFilters;
    size_t backdropFilterCount;
    const FilterToken *foregroundFilters;
    size_t foregroundFilterCount;
    const FilterToken *elementFilters;
    size_t elementFilterCount;
} CompositorScopeDesc;

/* Owned rectangular crop/copy, retaining requested world origin and dimensions.
 * Source pixels outside bounds are discarded; requested pixels outside source
 * are transparent black. No resize/resample. Uses core allocation/size limits.
 * Source must validate, including pixels outside the crop. Empty bounds allowed.
 * Failure leaves out unchanged. All pointers must be live; externally synced. */
CompositorStatus Compositor_crop(const CompositorSurface *source,
                                 CompositorBounds bounds, CompositorSurface **out);

/* Full CPU scene output, same bounds as priorScene. Success transfers new owned
 * surface; failure leaves out and all borrowed sources unchanged. This prototype
 * explicitly requires NONEMPTY, fully opaque priorScene (alpha exactly 1) and
 * a nonempty panel contained in that viewport; translucent prior returns
 * UNSUPPORTED, invalid data INVALID. Nonzero radius UNSUPPORTED. Unknown/pool
 * tokens remain UNSUPPORTED; no implicit truncation, pooling, radius clamping.
 *
 * 1. If backdrop stack is nonempty, snapshot only its symmetric sampling halo
 *    from priorScene, filter it, and crop to panel. Entire halo must fit within
 *    priorScene (otherwise LIMIT): no edge normalization is invented. This
 *    opaque filtered backdrop REPLACES prior pixels inside the panel, not an
 *    extra copy of the full prior scene painted over itself.
 * 2. Isolate foreground in painter order, filter it, THEN rectangularly clip
 *    to panel; clip decoration separately, leaving it out of foreground scope.
 * 3. Assemble optional filtered backdrop + decoration + foreground within the
 *    panel. Filter this entire assembled group with element stack LAST. If
 *    backdrop stack is absent, priorScene is not baked into the element group.
 * 4. Replace covered prior pixels with the filtered group's premultiplied RGB:
 *    result = group + prior*(1-coverage), where alpha gives group coverage on
 *    the explicitly opaque baseline. This preserves a backdrop replacement
 *    while allowing own-group blur to spill outside panel (clipped only to the
 *    scene viewport). Foreground blur was clipped first and cannot spill itself.
 *
 * Rectangular coverage is binary, so premultiplied RGBA is copied unchanged
 * inside, zero outside. Rounded/antialiased masks, transparent prior-scene
 * replacement, GPU runtime, retained hot-path reuse are explicitly deferred.
 * Core pixel/filter/source/scatter limits apply; cold intermediate allocations.
 */
CompositorStatus Compositor_scopedScene(const CompositorScopeDesc *desc,
                                        CompositorSurface **out);

#endif
