#include "device/device.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Device
 * ============================================================================
 * The first word of the graphics language. A Device is a thin { row, state }
 * wrapper: the row is a dialect's function table, the state is the dialect's
 * private device struct (VkDevice, MTLDevice, ID3D12Device, WGPUDevice, a CPU
 * raster context, or a no-op). Callers name a backend id and a window handle
 * and receive an opaque Device — they never spell a backend type.
 *
 * The registry resolves the dialect by backend id at construction; every verb
 * forwards through the row. This is deliberately the SAME shape as the unified
 * Graphics table: one contract, one row per backend, so a dialect can be
 * dropped in (or hot-swapped later) without touching a single caller.
 *
 * Lifetime: the wrapper is heap-allocated once (cold path, the Cold-Strict,
 * Hot-Minimal Validation Law); the dialect state is created by the dialect's
 * createState and destroyed by its destroyState, top-down per the Teardown
 * Order Law. Zero steady-state allocation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Device (device/device.c)
 * LEVEL: L4 — Self-Management (owns the GPU device across the process)
 * ============================================================================
 * SUMMARY:
 *   Backend-agnostic device wrapper over a dialect row. Device_create resolves
 *   the row by LANG_BACKEND_* id, asks the dialect to create its state, and
 *   stores both. Every Device_* verb forwards through the row — the wrapper
 *   itself holds no backend logic.
 *
 * STRUCT FIELDS (Mirroring lang/device.h incomplete tag — completed here):
 * ----------------------------------------------------------------------------
 *   const DeviceRow *row;   // dialect verb table (static, process-lifetime)
 *   void *state;            // dialect-private device struct (opaque here)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   registryGrow(void)      : double the dialect table (Anti-Hardcoding Law)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Device_0(void)
 *   - Device_2(backend, window)
 *   - Device_new(desc)
 *   - Device_registerRow(row)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Device_destroy(device)
 *   - Device_present(device)
 *   - Device_resize(device, width, height)
 *   - Device_findRow(backend)
 *   - Device_rowCount(void)
 *
 * Private Core Functions: (.c static)
 *   - registryGrow(void)
 *
 * Public Getters: (.h)
 *   - Device_backend(device)
 *   - Device_isValid(device)
 *   - Device_isReady(device)
 *   - Device_width(device)
 *   - Device_height(device)
 *   - Device_native(device)
 *   - Device_backendName(backend)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

struct Device {
    const DeviceRow *row;   // dialect verb table (static, process-lifetime)
    void *state;            // dialect-private device struct (opaque here)
};

// --- Dialect registry (fixed table, doubling growth) ---
static const DeviceRow **s_rows = nullptr;
static uint32_t s_rowCount = 0;
static uint32_t s_rowCap = 0;

// Grow the dialect table so a late registration never rejects (the Dynamic
// Scalability & Anti-Hardcoding Law). OOM leaves the cap untouched: the
// caller drop-degrades (the Cold-Strict, Hot-Minimal Validation Law).
static bool registryGrow(void) {
    if (s_rowCount < s_rowCap)
        return true;
    uint32_t newCap = (s_rowCap == 0) ? 8 : s_rowCap * 2;
    const DeviceRow **nb = (const DeviceRow**) realloc((void*) s_rows,
                                                       (size_t) newCap * sizeof(*nb));
    if (nb == nullptr)
        return false;
    s_rows = nb;
    s_rowCap = newCap;
    return true;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

bool Device_registerRow(const DeviceRow *row) {
    if (row == nullptr || (*row).backend == LANG_BACKEND_NONE)
        return false;
    if ((*row).createState == nullptr || (*row).destroyState == nullptr)
        return false;
    // Idempotent per backend: re-register replaces the row in place.
    for (uint32_t i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).backend == (*row).backend) {
            s_rows[i] = row;
            return true;
        }
    }
    if (!registryGrow())
        return false;
    s_rows[s_rowCount++] = row;
    return true;
}

const DeviceRow *Device_findRow(uint32_t backend) {
    if (backend == LANG_BACKEND_NONE)
        return nullptr;
    for (uint32_t i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).backend == backend)
            return s_rows[i];
    }
    return nullptr;
}

uint32_t Device_rowCount(void) {
    return s_rowCount;
}

static Device *deviceCreate(const DeviceDesc *desc) {
    if (desc == nullptr || (*desc).backend == LANG_BACKEND_NONE)
        return nullptr;
    const DeviceRow *row = Device_findRow((*desc).backend);
    if (row == nullptr || (*row).createState == nullptr)
        return nullptr;
    void *state = (*row).createState(desc);
    if (state == nullptr)
        return nullptr;
    Device *device = (Device*) calloc(1, sizeof(Device));
    if (device == nullptr) {
        (*row).destroyState(state);
        return nullptr;
    }
    (*device).row = row;
    (*device).state = state;
    return device;
}

Device *Device_new(const DeviceDesc *desc) {
    return deviceCreate(desc);
}

Device *Device_0(void) {
    return nullptr; // a device must name its dialect: NONE is fail-closed.
}

Device *Device_2(uint32_t backend, void *window) {
    return deviceCreate(&(DeviceDesc){ .backend = backend, .window = window });
}

void Device_destroy(Device *device) {
    if (device == nullptr)
        return;
    if ((*device).row != nullptr && (*device).state != nullptr)
        (*device).row->destroyState((*device).state);
    free(device);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool Device_present(Device *device) {
    if (device == nullptr || (*device).state == nullptr)
        return false;
    if ((*device).row->present == nullptr)
        return false;
    return (*device).row->present((*device).state);
}

bool Device_resize(Device *device, uint32_t width, uint32_t height) {
    if (device == nullptr || (*device).state == nullptr || width == 0 || height == 0)
        return false;
    if ((*device).row->resize == nullptr)
        return false;
    return (*device).row->resize((*device).state, width, height);
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t Device_backend(const Device *device) {
    if (device == nullptr || (*device).row == nullptr)
        return LANG_BACKEND_NONE;
    return (*device).row->backend;
}

;;GETTER
bool Device_isValid(const Device *device) {
    return device != nullptr && (*device).row != nullptr && (*device).state != nullptr;
}

;;GETTER
bool Device_isReady(const Device *device) {
    if (device == nullptr || (*device).state == nullptr)
        return false;
    if ((*device).row->isReady == nullptr)
        return false;
    return (*device).row->isReady((*device).state);
}

;;GETTER
uint32_t Device_width(const Device *device) {
    if (device == nullptr || (*device).state == nullptr || (*device).row->width == nullptr)
        return 0;
    return (*device).row->width((*device).state);
}

;;GETTER
uint32_t Device_height(const Device *device) {
    if (device == nullptr || (*device).state == nullptr || (*device).row->height == nullptr)
        return 0;
    return (*device).row->height((*device).state);
}

;;GETTER
void *Device_native(const Device *device) {
    if (device == nullptr || (*device).state == nullptr || (*device).row->native == nullptr)
        return nullptr;
    return (*device).row->native((*device).state);
}

;;GETTER
const char *Device_backendName(uint32_t backend) {
    const DeviceRow *row = Device_findRow(backend);
    if (row == nullptr || (*row).name == nullptr)
        return "none";
    return (*row).name;
}
