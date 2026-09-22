#include "null/null_device.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: NullDevice (null/null_device.c)
 * ============================================================================
 * The null dialect of the Device contract — the no-op device. Every verb is
 * accepted and does nothing observable: present counts and returns true, resize
 * records the extent, isReady is always true, native is null. It lets tests and
 * deterministic-teardown proofs drive the full device lifecycle with zero GPU,
 * zero window, and zero side effects, and lets the registry hold more than one
 * dialect without a real backend.
 *
 * native() answers null on purpose: a no-op device has no dialect handle to hand
 * out, and a caller that reaches for one gets the honest answer.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: NullDevice (null/null_device.c)
 * LEVEL: L4 — Self-Management (a no-op device for tests/teardown proofs)
 * ============================================================================
 * SUMMARY:
 *   Null DeviceRow. Every verb is a safe no-op; present counts and succeeds.
 *
 * STRUCT FIELDS: none — the state is the private NullState helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   NullState
 *     uint32_t width, height;  // recorded extent, native px
 *     uint64_t presents;       // present call count (observable no-op)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - nullCreateState(desc) / nullDestroyState(state)
 *   - nullPresent(state) / nullResize(state, w, h)
 *   - nullWidth(state) / nullHeight(state) / nullIsReady(state) / nullNative(state)
 * ============================================================================
 */

// SLOT RECORD state for LANG_BACKEND_NULL (owned by the row's createState).
typedef struct NullState {
    uint32_t width;      // recorded extent, native px
    uint32_t height;
    uint64_t presents;   // present call count (observable no-op)
} NullState;

static void *nullCreateState(const DeviceDesc *desc) {
    NullState *state = (NullState*) calloc(1, sizeof(NullState));
    if (state == nullptr)
        return nullptr;
    if (desc != nullptr) {
        (*state).width = (*desc).width;
        (*state).height = (*desc).height;
    }
    return state;
}

static void nullDestroyState(void *state) {
    free(state);
}

static bool nullPresent(void *state) {
    if (state == nullptr)
        return false;
    (*((NullState*) state)).presents++;
    return true;
}

static bool nullResize(void *state, uint32_t width, uint32_t height) {
    if (state == nullptr)
        return false;
    (*((NullState*) state)).width = width;
    (*((NullState*) state)).height = height;
    return true;
}

static uint32_t nullWidth(const void *state) {
    return state ? (*((const NullState*) state)).width : 0u;
}

static uint32_t nullHeight(const void *state) {
    return state ? (*((const NullState*) state)).height : 0u;
}

static bool nullIsReady(const void *state) {
    return state != nullptr;
}

static void *nullNative(const void *state) {
    (void) state;
    return nullptr; // a no-op device has no dialect handle.
}

static const DeviceRow kNullRow = {
    .backend = LANG_BACKEND_NULL,
    .name = "null",
    .createState = nullCreateState,
    .destroyState = nullDestroyState,
    .present = nullPresent,
    .resize = nullResize,
    .width = nullWidth,
    .height = nullHeight,
    .isReady = nullIsReady,
    .native = nullNative,
};

const DeviceRow *Null_row(void) {
    return &kNullRow;
}
