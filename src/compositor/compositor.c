#include "compositor/compositor.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/* CPU reference compositor: own linear-premultiplied float
 * surfaces at absolute integer origins, isolate ordered source-over groups,
 * and execute validated ordered pointwise/scatter filters. Explicit statuses
 * and private temporaries preserve outputs on failure. Prototype cold
 * allocations/work caps, no GPU or steady-state allocation claim. */
;;OVERVIEW
/* CompositorSurface fields: bounds then owned packed RGBA pixels.
 * Public: create/arity constructors/zero/destroy, bounded string projections,
 * bounds/pixels/constPixels/validate, filterBounds,
 * compose, sourceOver. Private: boundsCheck, decode, unionBounds, surfaceString,
 * apply and pointwise (alpha-preserving linear color operations).
 * Identity/gain keep support; blur expands each side and scatters weighted
 * source taps into the expanded target, then resolves alpha rounding. Group
 * filters run once AFTER painter-order assembly, not separately per child.
 * Pointwise brightness/contrast/invert clamp straight RGB to [0,1]; weighted
 * and channel grayscale preserve HDR. B&W compares linear Rec.709 luminance.
 * These are cold CPU references with status-based rejection, not GPU/hot paths.
 * Compositor_* are stateless operations, not a second object/class. */

struct CompositorSurface {
    CompositorBounds bounds;
    float *pixels;
};

static CompositorStatus boundsCheck(CompositorBounds b) {
    if ((!b.width) != (!b.height))
        return COMPOSITOR_INVALID;
    if ((int64_t) b.x + b.width > INT32_MAX ||
        (int64_t) b.y + b.height > INT32_MAX)
        return COMPOSITOR_LIMIT;
    if (b.width && ((uint64_t) b.width * b.height > COMPOSITOR_MAX_PIXELS ||
        (uint64_t) b.width * b.height > SIZE_MAX / (4 * sizeof(float))))
        return COMPOSITOR_LIMIT;
    return COMPOSITOR_OK;
}

CompositorStatus CompositorSurface_create(CompositorBounds b,
                                          CompositorSurface **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = boundsCheck(b);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *surface = calloc(1, sizeof *surface);
    if (!surface)
        return COMPOSITOR_NO_MEMORY;
    (*surface).bounds = b;
    if (b.width) {
        (*surface).pixels = calloc((size_t) b.width * b.height * 4, sizeof(float));
        if (!(*surface).pixels) {
            free(surface);
            return COMPOSITOR_NO_MEMORY;
        }
    }
    *out = surface;
    return COMPOSITOR_OK;
}

CompositorSurface *CompositorSurface_0(void) {
    return CompositorSurface_1((CompositorBounds) {0});
}
CompositorSurface *CompositorSurface_1(CompositorBounds bounds) {
    CompositorSurface *surface = NULL;
    CompositorSurface_create(bounds, &surface);
    return surface;
}
CompositorSurface *CompositorSurface_zero(void) {
    return CompositorSurface_0();
}

static void surfaceString(const CompositorSurface *surface, bool structure,
                           char *dest, size_t cap, bool *outTruncated) {
    if (!dest || !cap) {
        if (outTruncated)
            *outTruncated = true;
        return;
    }
    int written;
    if (!surface) {
        written = snprintf(dest, cap, "nullptr");
    } else {
        CompositorBounds b = (*surface).bounds;
        if (structure) {
            written = snprintf(dest, cap,
                "CompositorSurface{bounds={x=%d,y=%d,width=%u,height=%u},pixels=%p}",
                b.x, b.y, b.width, b.height, (void*) (*surface).pixels);
        } else {
            written = snprintf(dest, cap, "CompositorSurface(%u x %u at %d,%d)",
                               b.width, b.height, b.x, b.y);
        }
    }
    if (outTruncated)
        *outTruncated = written < 0 || (size_t) written >= cap;
}
void CompositorSurface_toString(const CompositorSurface *surface, char *dest,
                                size_t cap, bool *outTruncated) {
    surfaceString(surface, false, dest, cap, outTruncated);
}
void CompositorSurface_toStringStruct(const CompositorSurface *surface, char *dest,
                                      size_t cap, bool *outTruncated) {
    surfaceString(surface, true, dest, cap, outTruncated);
}

void CompositorSurface_destroy(CompositorSurface *surface) {
    if (!surface)
        return;
    free((*surface).pixels);
    free(surface);
}
CompositorBounds CompositorSurface_bounds(const CompositorSurface *surface) {
    return surface ? (*surface).bounds : (CompositorBounds) {0};
}
float *CompositorSurface_pixels(CompositorSurface *surface) {
    return surface ? (*surface).pixels : NULL;
}
const float *CompositorSurface_constPixels(const CompositorSurface *surface) {
    return surface ? (*surface).pixels : NULL;
}

static CompositorStatus decode(FilterToken token, float *gain, uint32_t *radius) {
    uint64_t payload = Filter_payload(token);
    switch (Filter_id(token)) {
    case FILTER_IDENTITY:
        return payload ? COMPOSITOR_INVALID : COMPOSITOR_OK;
    case FILTER_GAIN: {
        if (payload >> 32)
            return COMPOSITOR_INVALID;
        uint32_t bits = (uint32_t) payload;
        memcpy(gain, &bits, sizeof bits);
        return isfinite(*gain) && *gain >= 0 ? COMPOSITOR_OK : COMPOSITOR_INVALID;
    }
    case BRIGHTNESS_ID:
    case CONTRAST_ID:
    case BLACK_AND_WHITE_ID: {
        if (payload >> 32)
            return COMPOSITOR_INVALID;
        uint32_t bits = (uint32_t) payload;
        memcpy(gain, &bits, sizeof bits);
        if (!isfinite(*gain))
            return COMPOSITOR_INVALID;
        uint16_t id = Filter_id(token);
        if (id == BRIGHTNESS_ID)
            return *gain >= -1 && *gain <= 1 ? COMPOSITOR_OK : COMPOSITOR_INVALID;
        if (id == BLACK_AND_WHITE_ID)
            return *gain >= 0 && *gain <= 1 ? COMPOSITOR_OK : COMPOSITOR_INVALID;
        return *gain >= 0 ? COMPOSITOR_OK : COMPOSITOR_INVALID;
    }
    case GRAYSCALE_ID:
    case GRAYSCALE_RED_ID:
    case GRAYSCALE_GREEN_ID:
    case GRAYSCALE_BLUE_ID:
    case INVERT_ID:
        return payload ? COMPOSITOR_INVALID : COMPOSITOR_OK;
    case FILTER_SCATTER_BLUR:
        if (payload > FILTER_SCATTER_MAX_RADIUS)
            return COMPOSITOR_INVALID;
        *radius = (uint32_t) payload;
        return COMPOSITOR_OK;
    default: return COMPOSITOR_UNSUPPORTED;
    }
}

CompositorStatus Compositor_filterBounds(CompositorBounds source,
                                        const FilterToken *filters, size_t count,
                                        CompositorBounds *out) {
    if (!out || (count && !filters))
        return COMPOSITOR_INVALID;
    if (count > COMPOSITOR_MAX_FILTERS)
        return COMPOSITOR_LIMIT;
    CompositorStatus status = boundsCheck(source);
    if (status != COMPOSITOR_OK)
        return status;
    for (size_t i = 0; i < count; ++i) {
        float gain = 1;
        uint32_t radius = 0;
        status = decode(filters[i], &gain, &radius);
        if (status != COMPOSITOR_OK)
            return status;
        if (!source.width || !radius)
            continue;
        uint32_t diameter = 2 * radius + 1;
        if ((uint64_t) source.width * source.height * diameter * diameter >
            COMPOSITOR_MAX_SCATTER_TAPS)
            return COMPOSITOR_LIMIT;
        if ((int64_t) source.x - radius < INT32_MIN ||
            (int64_t) source.y - radius < INT32_MIN ||
            (uint64_t) source.width + 2 * radius > UINT32_MAX ||
            (uint64_t) source.height + 2 * radius > UINT32_MAX)
            return COMPOSITOR_LIMIT;
        source.x = (int32_t) ((int64_t) source.x - radius);
        source.y = (int32_t) ((int64_t) source.y - radius);
        source.width += 2 * radius;
        source.height += 2 * radius;
        status = boundsCheck(source);
        if (status != COMPOSITOR_OK)
            return status;
    }
    *out = source;
    return COMPOSITOR_OK;
}

CompositorStatus CompositorSurface_validate(const CompositorSurface *surface) {
    if (!surface)
        return COMPOSITOR_INVALID;
    CompositorBounds bounds = (*surface).bounds;
    size_t n = (size_t) bounds.width * bounds.height;
    for (size_t i = 0; i < n; ++i) {
        const float *p = (*surface).pixels + 4 * i;
        for (size_t c = 0; c < 4; ++c)
            if (!isfinite(p[c]) || p[c] < 0)
                return COMPOSITOR_INVALID;
        if (p[3] > 1 || (p[3] == 0 && (p[0] != 0 || p[1] != 0 || p[2] != 0)))
            return COMPOSITOR_INVALID;
    }
    return COMPOSITOR_OK;
}

CompositorStatus Compositor_sourceOver(const CompositorSurface *source,
                                       CompositorSurface *dest) {
    if (source == dest)
        return COMPOSITOR_INVALID;
    CompositorStatus status = CompositorSurface_validate(source);
    if (status != COMPOSITOR_OK)
        return status;
    status = CompositorSurface_validate(dest);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds s = (*source).bounds, d = (*dest).bounds;
    int64_t x0 = s.x > d.x ? s.x : d.x, y0 = s.y > d.y ? s.y : d.y;
    int64_t sx1 = (int64_t) s.x + s.width, dx1 = (int64_t) d.x + d.width;
    int64_t sy1 = (int64_t) s.y + s.height, dy1 = (int64_t) d.y + d.height;
    int64_t x1 = sx1 < dx1 ? sx1 : dx1, y1 = sy1 < dy1 ? sy1 : dy1;
    /* Two passes: overflow proof before the first destination write. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (int64_t y = y0; y < y1; ++y) {
            for (int64_t x = x0; x < x1; ++x) {
                const float *sp = (*source).pixels + 4 *
                    ((size_t) (y - s.y) * s.width + (size_t) (x - s.x));
                float *dp = (*dest).pixels + 4 *
                    ((size_t) (y - d.y) * d.width + (size_t) (x - d.x));
                double remain = 1.0 - sp[3];
                for (unsigned c = 0; c < 4; ++c) {
                    double value = sp[c] + dp[c] * remain;
                    if (!pass && value > FLT_MAX)
                        return COMPOSITOR_LIMIT;
                    if (pass)
                        dp[c] = (float) value;
                }
            }
        }
    }
    return COMPOSITOR_OK;
}

static CompositorStatus unionBounds(CompositorBounds a, CompositorBounds b,
                                     CompositorBounds *out) {
    if (!a.width) {
        *out = b;
        return COMPOSITOR_OK;
    }
    if (!b.width) {
        *out = a;
        return COMPOSITOR_OK;
    }
    int32_t x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    int64_t ax1 = (int64_t) a.x + a.width, bx1 = (int64_t) b.x + b.width;
    int64_t ay1 = (int64_t) a.y + a.height, by1 = (int64_t) b.y + b.height;
    int64_t width = (ax1 > bx1 ? ax1 : bx1) - x;
    int64_t height = (ay1 > by1 ? ay1 : by1) - y;
    if (width > UINT32_MAX || height > UINT32_MAX)
        return COMPOSITOR_LIMIT;
    CompositorBounds result = {x, y, (uint32_t) width, (uint32_t) height};
    CompositorStatus status = boundsCheck(result);
    if (status == COMPOSITOR_OK)
        *out = result;
    return status;
}

/* Rec.709 coefficients are fixed colorimetry, not configurable style defaults.
 * Premultiplied formulas avoid dividing by tiny/zero alpha. Double intermediates
 * keep finite HDR input and extreme valid contrast from overflowing a float.
 * This helper receives only cold-validated pixels and token parameters. */
static const double LUMA_RED = 0.2126;
static const double LUMA_GREEN = 0.7152;
static const double LUMA_BLUE = 0.0722;
static const double CONTRAST_PIVOT = 0.5;

static void pointwise(uint16_t id, float amount, float *p) {
    double alpha = p[3];
    if (id == BRIGHTNESS_ID || id == CONTRAST_ID || id == INVERT_ID) {
        for (unsigned c = 0; c < 3; ++c) {
            double value = p[c];
            if (id == BRIGHTNESS_ID)
                value += amount * alpha;
            else if (id == CONTRAST_ID)
                value = (value - CONTRAST_PIVOT * alpha) * amount + CONTRAST_PIVOT * alpha;
            else
                value = alpha - value;
            p[c] = (float) fmin(alpha, fmax(0, value));
        }
        return;
    }
    double gray;
    if (id == GRAYSCALE_RED_ID)
        gray = p[0];
    else if (id == GRAYSCALE_GREEN_ID)
        gray = p[1];
    else if (id == GRAYSCALE_BLUE_ID)
        gray = p[2];
    else {
        gray = p[0] * LUMA_RED + p[1] * LUMA_GREEN + p[2] * LUMA_BLUE;
        if (id == BLACK_AND_WHITE_ID)
            gray = gray >= amount * alpha ? alpha : 0;
        else
            gray = fmin(gray, FLT_MAX); /* convex-sum roundoff at HDR maximum */
    }
    p[0] = p[1] = p[2] = (float) gray;
}

static CompositorStatus apply(CompositorSurface **surface, FilterToken token) {
    float gain = 1;
    uint32_t radius = 0;
    CompositorStatus status = decode(token, &gain, &radius);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *src = *surface;
    CompositorBounds b = (*src).bounds;
    size_t count = (size_t) b.width * b.height;
    uint16_t id = Filter_id(token);
    if (id == BRIGHTNESS_ID || id == CONTRAST_ID || id == BLACK_AND_WHITE_ID ||
        id == GRAYSCALE_ID || id == GRAYSCALE_RED_ID || id == GRAYSCALE_GREEN_ID ||
        id == GRAYSCALE_BLUE_ID || id == INVERT_ID) {
        for (size_t i = 0; i < count; ++i)
            pointwise(id, gain, (*src).pixels + 4 * i);
        return COMPOSITOR_OK;
    }
    if (Filter_id(token) == FILTER_GAIN) {
        for (size_t i = 0; i < count; ++i)
            for (unsigned c = 0; c < 3; ++c)
                if ((double) (*src).pixels[4 * i + c] * gain > FLT_MAX)
                    return COMPOSITOR_LIMIT;
        for (size_t i = 0; i < count; ++i)
            for (unsigned c = 0; c < 3; ++c)
                (*src).pixels[4 * i + c] *= gain;
        return COMPOSITOR_OK;
    }
    if (!radius || !count)
        return COMPOSITOR_OK;
    uint32_t diameter = radius * 2 + 1;
    if ((uint64_t) count * diameter * diameter > COMPOSITOR_MAX_SCATTER_TAPS)
        return COMPOSITOR_LIMIT;
    CompositorBounds expanded;
    status = Compositor_filterBounds(b, &token, 1, &expanded);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *dst = NULL;
    status = CompositorSurface_create(expanded, &dst);
    if (status != COMPOSITOR_OK)
        return status;
    /* Scatter first: each source pixel emits its premultiplied RGBA into all
     * fixed taps in the expanded destination. No gather/separable substitute.
     * Accumulate weighted contributions; resolve clamps alpha rounding only.
     * Transparent-outside uses the FULL denominator, including absent pixels. */
    double weight = 1.0 / ((double) diameter * diameter);
    for (uint32_t y = 0; y < b.height; ++y) {
        for (uint32_t x = 0; x < b.width; ++x) {
            const float *p = (*src).pixels + 4 * ((size_t) y * b.width + x);
            for (uint32_t ky = 0; ky < diameter; ++ky) {
                for (uint32_t kx = 0; kx < diameter; ++kx) {
                    float *q = (*dst).pixels + 4 *
                        ((size_t) (y + ky) * expanded.width + x + kx);
                    for (unsigned c = 0; c < 4; ++c) {
                        double value = q[c] + p[c] * weight;
                        if (value > FLT_MAX) {
                            CompositorSurface_destroy(dst);
                            return COMPOSITOR_LIMIT;
                        }
                        q[c] = (float) value;
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < (size_t) expanded.width * expanded.height; ++i) {
        float *p = (*dst).pixels + 4 * i;
        if (p[3] > 1)
            p[3] = 1;
        if (p[3] == 0)
            p[0] = p[1] = p[2] = 0;
    }
    CompositorSurface_destroy(src);
    *surface = dst;
    return COMPOSITOR_OK;
}

CompositorStatus Compositor_compose(const CompositorSurface *const *sources,
                                    size_t sourceCount,
                                    const FilterToken *filters, size_t filterCount,
                                    CompositorSurface **out) {
    if (!out || (sourceCount && !sources) || (filterCount && !filters))
        return COMPOSITOR_INVALID;
    if (sourceCount > COMPOSITOR_MAX_SOURCES)
        return COMPOSITOR_LIMIT;
    CompositorBounds bounds = {0};
    for (size_t i = 0; i < sourceCount; ++i) {
        CompositorStatus status = CompositorSurface_validate(sources[i]);
        if (status != COMPOSITOR_OK)
            return status;
        const CompositorSurface *source = sources[i];
        status = unionBounds(bounds, (*source).bounds, &bounds);
        if (status != COMPOSITOR_OK)
            return status;
    }
    CompositorBounds finalBounds;
    CompositorStatus status = Compositor_filterBounds(bounds, filters, filterCount,
                                                       &finalBounds);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorSurface *group = NULL;
    status = CompositorSurface_create(bounds, &group);
    if (status != COMPOSITOR_OK)
        return status;
    for (size_t i = 0; i < sourceCount; ++i) {
        status = Compositor_sourceOver(sources[i], group);
        if (status != COMPOSITOR_OK)
            break;
    }
    for (size_t i = 0; status == COMPOSITOR_OK && i < filterCount; ++i)
        status = apply(&group, filters[i]);
    if (status != COMPOSITOR_OK) {
        CompositorSurface_destroy(group);
        return status;
    }
    *out = group;
    return COMPOSITOR_OK;
}
