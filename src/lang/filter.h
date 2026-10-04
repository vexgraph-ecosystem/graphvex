#ifndef GRAPHVEX_LANG_FILTER_H
#define GRAPHVEX_LANG_FILTER_H

#include <stdint.h>
#include <string.h>
#include <float.h>

/* Numeric ABI, not a native byte-stream format: ID in bits 63..48, payload
 * in bits 47..0. Serialize explicitly in a chosen endian order. No pointers.
 * The plain compositor rejects pooled IDs without context; FilterPool_compose
 * resolves them. Constructors pack values; validation belongs at submission. */
typedef uint64_t FilterToken;
// One canonical operation-ID table. Pool payload layout belongs to filter_pool.h.
enum {
    FILTER_IDENTITY = 0,
    FILTER_GAIN = 1,
    FILTER_SCATTER_BLUR = 2,
    FILTER_POOL_ID = 0x8000
};
#define FILTER_PAYLOAD_MASK UINT64_C(0x0000ffffffffffff)
#define FILTER_SCATTER_MAX_RADIUS 16u

_Static_assert(sizeof(FilterToken) == 8, "filter token ABI");
_Static_assert(sizeof(float) == 4, "gain payload requires binary32");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "gain payload requires IEEE binary32");

static inline uint16_t Filter_id(FilterToken token) {
    return (uint16_t) (token >> 48);
}
static inline uint64_t Filter_payload(FilterToken token) {
    return token & FILTER_PAYLOAD_MASK;
}
static inline FilterToken Filter_identity(void) { return 0; }
/* Gain scales linear premultiplied RGB, preserves alpha, and does not clamp.
 * Valid gain: finite, nonnegative. Upper payload bits must remain zero. */
static inline FilterToken Filter_gain(float gain) {
    uint32_t bits;
    memcpy(&bits, &gain, sizeof bits);
    return ((uint64_t) FILTER_GAIN << 48) | bits;
}
/* Fixed uniform square scatter kernel, radius in native integer pixels.
 * Transparent outside, normalized by the full (2r+1)^2 kernel, not by surviving
 * source weight. Radius zero is identity. Out-of-range radius packs unchanged
 * so submission rejects rather than truncates or silently clamps it. */
static inline FilterToken Filter_scatterBlur(uint32_t radius) {
    return ((uint64_t) FILTER_SCATTER_BLUR << 48) | radius;
}

#endif
