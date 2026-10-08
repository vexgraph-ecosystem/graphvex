#include "compositor/compositor_scope.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

#include <float.h>
#include <string.h>

;;DEFINITION
/* Explicit CPU scope scheduler: backdrop reads only a borrowed painter-prefix;
 * foreground excludes decoration and is filtered before panel clipping; whole
 * element filters run after assembly and may spill outside the panel. Owned
 * private intermediates make every failure atomic at the public out seam.
 * An opaque prior-scene contract permits exact coverage replacement without
 * compositing a complete scene twice. No opaque backend handles or UI classes. */
;;OVERVIEW
/* No retained class fields. CompositorScopeDesc is a borrowed cold submission
 * record described by compositor_scope.h. Public: crop/scopedScene. Private:
 * contains, sceneCheck, replacementCopy. Integer world origins survive crops,
 * group assembly and final viewport clipping. Fixed scatter support is symmetric
 * so filterBounds determines the backdrop snapshot's cumulative sample halo.
 * Rectangle-only binary masks; nonzero radii/translucent baseline unsupported. */

static bool contains(CompositorBounds outer, CompositorBounds inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        (int64_t) inner.x + inner.width <= (int64_t) outer.x + outer.width &&
        (int64_t) inner.y + inner.height <= (int64_t) outer.y + outer.height;
}

CompositorStatus Compositor_crop(const CompositorSurface *source,
                                 CompositorBounds bounds, CompositorSurface **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = CompositorSurface_validate(source);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *crop = nullptr;
    status = CompositorSurface_create(bounds, &crop);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds s = CompositorSurface_bounds(source);
    int64_t x0 = s.x > bounds.x ? s.x : bounds.x;
    int64_t y0 = s.y > bounds.y ? s.y : bounds.y;
    int64_t sx1 = (int64_t) s.x + s.width, dx1 = (int64_t) bounds.x + bounds.width;
    int64_t sy1 = (int64_t) s.y + s.height, dy1 = (int64_t) bounds.y + bounds.height;
    int64_t x1 = sx1 < dx1 ? sx1 : dx1, y1 = sy1 < dy1 ? sy1 : dy1;
    const float *sp = CompositorSurface_constPixels(source);
    float *dp = CompositorSurface_pixels(crop);
    if (x1 > x0) {
        size_t Bytes = (size_t) (x1 - x0) * 4 * sizeof(float);
        for (int64_t y = y0; y < y1; ++y) {
            const float *row = sp + 4 * ((size_t) (y - s.y) * s.width + (size_t) (x0 - s.x));
            float *dest = dp + 4 * ((size_t) (y - bounds.y) * bounds.width + (size_t) (x0 - bounds.x));
            memcpy(dest, row, Bytes);
        }
    }
    *out = crop;
    return COMPOSITOR_OK;
}

static CompositorStatus sceneCheck(const CompositorScopeDesc *desc) {
    if (!desc || ((*desc).foregroundCount && !(*desc).foreground))
        return COMPOSITOR_INVALID;
    if ((*desc).radius)
        return COMPOSITOR_UNSUPPORTED;
    if ((*desc).foregroundCount > COMPOSITOR_MAX_SOURCES)
        return COMPOSITOR_LIMIT;
    CompositorStatus status = CompositorSurface_validate((*desc).priorScene);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds prior = CompositorSurface_bounds((*desc).priorScene);
    CompositorBounds panel = (*desc).panelBounds;
    if (!prior.width || !panel.width || !panel.height)
        return COMPOSITOR_INVALID;
    const float *pixels = CompositorSurface_constPixels((*desc).priorScene);
    for (size_t i = 0; i < (size_t) prior.width * prior.height; ++i)
        if (pixels[4 * i + 3] != 1)
            return COMPOSITOR_UNSUPPORTED;
    CompositorBounds support;
    status = Compositor_filterBounds(panel, nullptr, 0, &support);
    if (status != COMPOSITOR_OK)
        return status;
    if (!contains(prior, panel))
        return COMPOSITOR_LIMIT;
    status = Compositor_filterBounds(panel, (*desc).backdropFilters,
                                     (*desc).backdropFilterCount, &support);
    if (status != COMPOSITOR_OK)
        return status;
    if (!contains(prior, support))
        return COMPOSITOR_LIMIT;
    /* Validate even an empty foreground's stack; apply performs source-specific
     * support/work checks after isolation. Element starts at panel bounds. */
    status = Compositor_filterBounds((CompositorBounds) {0}, (*desc).foregroundFilters,
                                     (*desc).foregroundFilterCount, &support);
    if (status != COMPOSITOR_OK)
        return status;
    return Compositor_filterBounds(panel, (*desc).elementFilters,
                                    (*desc).elementFilterCount, &support);
}

/* Destination is a private opaque prior copy. No full-scene source-over call:
 * each covered pixel is replaced using the filtered panel's alpha as coverage.
 * On this opaque baseline this also gives the correct front-content over rule.
 * Failure need not roll back a private copy, which is destroyed by the caller. */
static CompositorStatus replacementCopy(const CompositorSurface *group,
                                        CompositorSurface *scene) {
    CompositorStatus status = CompositorSurface_validate(group);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds s = CompositorSurface_bounds(group), d = CompositorSurface_bounds(scene);
    int64_t x0 = s.x > d.x ? s.x : d.x, y0 = s.y > d.y ? s.y : d.y;
    int64_t sx1 = (int64_t) s.x + s.width, dx1 = (int64_t) d.x + d.width;
    int64_t sy1 = (int64_t) s.y + s.height, dy1 = (int64_t) d.y + d.height;
    int64_t x1 = sx1 < dx1 ? sx1 : dx1, y1 = sy1 < dy1 ? sy1 : dy1;
    const float *sp = CompositorSurface_constPixels(group);
    float *dp = CompositorSurface_pixels(scene);
    for (int64_t y = y0; y < y1; ++y) {
        for (int64_t x = x0; x < x1; ++x) {
            const float *p = sp + 4 * ((size_t) (y - s.y) * s.width + (size_t) (x - s.x));
            float *q = dp + 4 * ((size_t) (y - d.y) * d.width + (size_t) (x - d.x));
            for (unsigned c = 0; c < 3; ++c) {
                double value = p[c] + q[c] * (1.0 - p[3]);
                if (value > FLT_MAX)
                    return COMPOSITOR_LIMIT;
                q[c] = (float) value;
            }
            q[3] = 1;
        }
    }
    return COMPOSITOR_OK;
}

CompositorStatus Compositor_scopedScene(const CompositorScopeDesc *desc,
                                        CompositorSurface **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = sceneCheck(desc);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *snapshot = nullptr, *backdrop = nullptr, *panel = nullptr;
    CompositorSurface *foreground = nullptr, *clippedForeground = nullptr;
    CompositorSurface *clippedDecoration = nullptr, *filteredPanel = nullptr, *scene = nullptr;
    status = CompositorSurface_create((*desc).panelBounds, &panel);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    if ((*desc).backdropFilterCount) {
        CompositorBounds halo;
        status = Compositor_filterBounds((*desc).panelBounds, (*desc).backdropFilters,
                                         (*desc).backdropFilterCount, &halo);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        status = Compositor_crop((*desc).priorScene, halo, &snapshot);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        const CompositorSurface *sources[] = {snapshot};
        status = Compositor_compose(sources, 1, (*desc).backdropFilters,
                                    (*desc).backdropFilterCount, &backdrop);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        CompositorSurface_destroy(snapshot);
        snapshot = nullptr;
        status = Compositor_crop(backdrop, (*desc).panelBounds, &snapshot);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        /* Prior snapshot is opaque and includes all taps. Fixed scatter's
         * alpha sum can round slightly below one; enforce exact coverage 1
         * in the proven fully supported crop, not edge renormalization. */
        float *p = CompositorSurface_pixels(snapshot);
        CompositorBounds b = (*desc).panelBounds;
        for (size_t i = 0; i < (size_t) b.width * b.height; ++i)
            p[4 * i + 3] = 1;
        CompositorSurface_destroy(panel);
        panel = snapshot;
        snapshot = nullptr;
    }
    if ((*desc).decoration) {
        status = Compositor_crop((*desc).decoration, (*desc).panelBounds, &clippedDecoration);
        if (status != COMPOSITOR_OK)
            goto cleanup;
        status = Compositor_sourceOver(clippedDecoration, panel);
        if (status != COMPOSITOR_OK)
            goto cleanup;
    }
    status = Compositor_compose((*desc).foreground, (*desc).foregroundCount,
                                (*desc).foregroundFilters, (*desc).foregroundFilterCount,
                                &foreground);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = Compositor_crop(foreground, (*desc).panelBounds, &clippedForeground);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = Compositor_sourceOver(clippedForeground, panel);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    const CompositorSurface *sources[] = {panel};
    status = Compositor_compose(sources, 1, (*desc).elementFilters,
                                (*desc).elementFilterCount, &filteredPanel);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = Compositor_crop((*desc).priorScene,
                             CompositorSurface_bounds((*desc).priorScene), &scene);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    status = replacementCopy(filteredPanel, scene);
    if (status != COMPOSITOR_OK)
        goto cleanup;
    *out = scene;
    scene = nullptr;
cleanup:
    CompositorSurface_destroy(snapshot);
    CompositorSurface_destroy(backdrop);
    CompositorSurface_destroy(panel);
    CompositorSurface_destroy(foreground);
    CompositorSurface_destroy(clippedForeground);
    CompositorSurface_destroy(clippedDecoration);
    CompositorSurface_destroy(filteredPanel);
    CompositorSurface_destroy(scene);
    return status;
}
