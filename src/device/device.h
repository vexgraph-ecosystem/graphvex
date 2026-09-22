#ifndef DEVICE_DEVICE_H
#define DEVICE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/device.h"

// device/device.h — the dialect registry (the device half of the language).
//
// A Device is a thin `{ row, state }` wrapper: the row is the dialect's
// function table, the state is the dialect's private struct. Every dialect
// (vulkan/, metal/, d3d/, wgpu/, raster/, null/) exports ONE `const DeviceRow`
// and registers it once at boot; Device_create resolves the row by backend id
// and hands the dialect the Desc. This is the SAME shape as the unified
// Graphics table (lang/graphics.h): one contract, one row per backend, zero
// backend types in caller code.
//
// SLOT RECORD: DeviceRow (owned by the registry — a behaviorless function
// table, the Single Class Per File Law). It carries the dialect's verbs; the
// `Device` class itself lives in device/device.c.
//
// LIFETIME: the registry is a fixed, arena-free table (zero steady-state
// allocation — the Dynamic Scalability & Anti-Hardcoding Law: it grows by
// doubling on demand, never rejects a late dialect).

// SLOT RECORD: one dialect's device verbs. `state` is the dialect's private
// device struct; the registry never inspects it.
typedef struct DeviceRow {
    uint32_t backend;                                     // LANG_BACKEND_*
    const char *name;                                     // dialect name (diagnostics)
    void  *(*createState)(const DeviceDesc *desc);        // dialect device up
    void   (*destroyState)(void *state);                  // dialect device down
    bool   (*present)(void *state);                       // on-demand present
    bool   (*resize)(void *state, uint32_t w, uint32_t h); // native px
    uint32_t (*width)(const void *state);                 // native px
    uint32_t (*height)(const void *state);                // native px
    bool   (*isReady)(const void *state);                 // up and not lost
    void  *(*native)(const void *state);                  // opaque dialect handle
} DeviceRow;

// --- Constructors ---
// Register a dialect row. Idempotent per backend (re-register replaces). The
// row pointer must outlive the process (dialects export static rows). Returns
// false on null row, null verbs, or backend NONE.
bool Device_registerRow(const DeviceRow *row);

// --- Core functions ---
// The row for a backend id, or nullptr when that dialect is not linked/registered.
const DeviceRow *Device_findRow(uint32_t backend);
uint32_t Device_rowCount(void);

// --- Getters ---
const char *Device_backendName(uint32_t backend); // "vulkan"/... or "none"

#endif // DEVICE_DEVICE_H
