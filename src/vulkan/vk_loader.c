#include "vulkan/vk_loader.h"
#include "vulkan/vk_context.h"
#include "vulkan/vk.h"
#include "vulkan/vk_mac.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "annotation/overview.h"
#include "annotation/incomplete.h"
#include "struct/chunked_list.h"

;;OVERVIEW
/**
 * ============================================================================
 *  * MODULE: VkLoader (src/vulkan/vk_loader.c — migrated from hotcwap/hot/vk_loader.c; graphics owns its module loading. DORMANT, see ;;INCOMPLETE)
 *  * LEVEL: L4 — Self-Management (module hot-reload shim over graphvex Vulkan)
 *  * ============================================================================
 *  * vk_loader.c is the thin module hot-reload adapter. It does NOT create a
 *  * VkInstance or VkDevice — those are owned entirely by graphvex (vk_instance.c
 *  * via the Vk_* seam). vk_loader.c's job:
 *  *   1. Load a Vulkan .dylib module via dlopen
 *  *   2. Extract the module's manifest + trampoline table
 *  *   3. Verify ABI compatibility (type IDs match frozen contracts)
 *  *   4. Atomically swap function pointers via the trampoline table
 *  *   5. Retire old dylibs safely across reload generations
 *  *   6. Persist + restore pipeline cache (VkPipelineCache handle obtained
 *  *      from graphvex, not created locally)
 *
 *  * All Vulkan handles flow from graphvex's Vk_* accessors:
 *  *   Vk_getInstance() / Vk_getGpa()  — for instance-level loader calls
 *  *   Vk_getDevice() / Vk_getGdpa()   — for device-level loader calls
 *  *   Vk_getQueue() / Vk_getQueueFamily()
 *
 *  * STRUCT FIELDS (local to this file):
 *  * ----------------------------------------------------------------------------
 *  *   Trampoline (one stable row per exported symbol, held in a ChunkedList):
 *  *     _Atomic(void*) ptr;                    // current generation target
 *  *     _Atomic(void*) fallback_ptr;           // prior generation (mid-swap cover)
 *  *     char name[64];                         // export symbol name
 *  *   The list replaces the old fixed 64-row array (the Dynamic Scalability &
 *  *   Anti-Hardcoding Law) and its count-then-check create path, which leaked
 *  *   the counter past the ceiling on overflow. Rows never move, so
 *  *   hot_vk_get_symbol stays a lock-free reader while registration runs on
 *  *   the loader thread (the ChunkedList single-writer contract): a reader may
 *  *   transiently miss a row whose name is still being written during a load
 *  *   and fails closed to nullptr.
 *  *
 *  *   VkRetiredHandle (one parked dylib):
 *  *     void *handle;                          // retired dylib (nullptr = free slot)
 *  *     uint32_t generation;                   // reload generation when retired
 *  *   Parking lot: VkRetiredHandle *s_vk_retired over [0, s_vk_retiredCap),
 *  *   cap starting at VK_RETIRED_INIT 16 and doubling via vkRetiredGrow()
 *  *   (the Dynamic Scalability & Anti-Hardcoding Law); index-safe, OOM
 *  *   falls back to oldest-entry eviction.
 *  *
 *  *   Module statics:
 *  *     void *s_module_handle;                // currently loaded dylib
 *  *     bool s_initialized;                   // module ready
 *  *     VkPipelineCache s_cache;              // pipeline cache (from graphvex)
 *  *     PFN_vkCreatePipelineCache s_createCache;  // resolved via gdpa
 *  *     PFN_vkDestroyPipelineCache s_destroyCache;
 *  *     PFN_vkGetPipelineCacheData s_getCacheData;
 *
 *  * FUNCTION REGISTRY:
 *  * ----------------------------------------------------------------------------
 *  * Core Functions:
 *  *   - hot_vk_init_loader(void)
 *  *   - hot_vk_load_module(path)
 *  *   - hot_vk_shutdown(void)
 *  *   - vk_retire_handle(handle)
 *  *   - vk_advance_generation(void)
 *  *
 *  * Getters:
 *  *   - hot_vk_get_symbol(name)
 *  * ============================================================================
 */

;;INCOMPLETE // Vk_getDevice/Gdpa/Instance/Phys/Queue/QueueFamily accessors predate the current Vk seam (undefined until rewired); the loader compiles into the archive but no caller may link it until then.

// src/vulkan/vk_loader.c — thin module hot-reload adapter over graphvex Vulkan.
//
// The VkDevice is owned by graphvex (Vk_init in vk_instance.c). vk_loader.c
// only manages dylib loading, trampoline table atomics, and pipeline cache
// persistence. All Vulkan handles are obtained via Vk_get*() accessors.

// Module handle
static void *s_module_handle = nullptr;
static bool s_initialized = false;

// Pipeline cache state — handle obtained from graphvex, functions resolved
// through Vk_getGdpa(). We don't create our own device.
static VkPipelineCache s_cache = VK_NULL_HANDLE;
static PFN_vkCreatePipelineCache s_createCache = nullptr;
static PFN_vkDestroyPipelineCache s_destroyCache = nullptr;
static PFN_vkGetPipelineCacheData s_getCacheData = nullptr;

// Function pointers from the module
static VkModuleInitFn s_module_init = nullptr;
static VkModuleShutdownFn s_module_shutdown = nullptr;
static VkModuleGetTrampolinesFn s_module_get_trampolines = nullptr;
static VkModuleGetManifestFn s_module_get_manifest = nullptr;

// Trampoline table: one stable row per exported symbol, held in a ChunkedList
// (the Dynamic Scalability & Anti-Hardcoding Law — no row ceiling, and no
// leaked counter: the old count-then-check create path incremented past its
// fixed array on overflow and pinned every later create to failure).
// The row layout is explicit because the Struct registry does not know it;
// elementClass 0 tags the chunk blocks as unregistered rows. At 80 bytes per
// row on the 128-byte default budget each row gets a chunk to itself, so one
// row's atomics never share chunk bytes with a neighbour row.
typedef struct {
    _Atomic(void*) ptr;
    _Atomic(void*) fallback_ptr;
    char name[64];
} Trampoline;

static ChunkedList *s_trampolines = nullptr;

// Loader-thread-only: create the table on first registration. Registration
// (find-or-create plus patching) runs on the loader thread; hot_vk_get_symbol
// is the lock-free reader.
static bool trampolineEnsure(void) {
    if (!s_trampolines)
        s_trampolines = ChunkedList_3(0u, sizeof(Trampoline), VEX_CHUNKED_BYTES_DEFAULT);
    return s_trampolines != nullptr;
}

static Trampoline *trampolineRow(int idx) {
    if (idx < 0 || !s_trampolines)
        return nullptr;
    return (Trampoline*) ChunkedList_slot(s_trampolines, (uint32_t)idx);
}

// Find or create a trampoline (loader thread only)
static int trampoline_find(const char *name) {
    if (!name || !s_trampolines)
        return -1;
    uint32_t count = ChunkedList_size(s_trampolines);
    for (uint32_t i = 0; i < count; i++) {
        Trampoline *row = (Trampoline*) ChunkedList_slot(s_trampolines, i);
        if (!row || (*row).name[0] == '\0')
            continue;
        if (strcmp((*row).name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int trampoline_create(const char *name) {
    if (!name || !trampolineEnsure())
        return -1;
    uint8_t *slot = ChunkedList_addSlot(s_trampolines);
    if (!slot)
        return -1;
    Trampoline *row = (Trampoline*) slot;
    strncpy((*row).name, name, 63);
    (*row).name[63] = '\0';
    // ptr/fallback_ptr arrive zeroed from addSlot; the commit already published
    // the row, and the name store above completes it on this thread.
    uint32_t count = ChunkedList_size(s_trampolines);
    if (count == 0u || count > (uint32_t)INT32_MAX)
        return -1;
    return (int)(count - 1u);
}

void *hot_vk_get_symbol(const char *name) {
    int idx = trampoline_find(name);
    Trampoline *row = trampolineRow(idx);
    if (!row)
        return nullptr;
    void *ptr = atomic_load(&(*row).ptr);
    if (!ptr) {
        for (int retry = 0; retry < 4 && !ptr; retry++) {
            #if defined(__aarch64__)
            __asm__ volatile("yield");
            #endif
            ptr = atomic_load(&(*row).ptr);
        }
        if (!ptr) {
            ptr = atomic_load(&(*row).fallback_ptr);
        }
    }
    return ptr;
}

// Generational handle retirement: keeps old dylib handles alive across
// reload generations to prevent race conditions during module reload.
#define VK_RETIRED_GENERATIONS 4
#define VK_RETIRED_INIT 16

typedef struct {
    void *handle;
    uint32_t generation;
} VkRetiredHandle;

static VkRetiredHandle *s_vk_retired = NULL;
static size_t s_vk_retiredCap = 0;
static uint32_t s_vk_generation = 0;

// Grow the parking lot on demand (the Dynamic Scalability & Anti-Hardcoding
// Law). Rows are index-accessed only, so realloc's move is invisible. OOM
// leaves the cap untouched: the caller falls back to oldest-entry eviction
// (drop-degrade per the Cold-Strict, Hot-Minimal Validation Law).
static bool vkRetiredGrow(void) {
    size_t newCap = (s_vk_retiredCap == 0) ? VK_RETIRED_INIT : s_vk_retiredCap * 2;
    VkRetiredHandle *nb = (VkRetiredHandle*) realloc(s_vk_retired, newCap * sizeof(VkRetiredHandle));
    if (!nb)
        return false;
    memset(nb + s_vk_retiredCap, 0, (newCap - s_vk_retiredCap) * sizeof(VkRetiredHandle));
    s_vk_retired = nb;
    s_vk_retiredCap = newCap;
    return true;
}

static void vk_retire_handle(void *handle) {
    if (!handle) return;

    // Reap old generations
    for (size_t i = 0; i < s_vk_retiredCap; i++) {
        if (s_vk_retired[i].handle && (s_vk_generation - s_vk_retired[i].generation >= VK_RETIRED_GENERATIONS)) {
            dlclose(s_vk_retired[i].handle);
            s_vk_retired[i].handle = nullptr;
        }
    }

    // Find a free slot
    size_t slot = s_vk_retiredCap;
    for (size_t i = 0; i < s_vk_retiredCap; i++) {
        if (!s_vk_retired[i].handle) {
            slot = i;
            break;
        }
    }

    if (slot == s_vk_retiredCap && vkRetiredGrow())
        slot = s_vk_retiredCap - 1; // the freshly appended, zeroed slot

    if (slot == s_vk_retiredCap) {
        // Evict oldest if all slots full
        size_t oldest_idx = 0;
        uint32_t oldest_gen = UINT32_MAX;
        for (size_t i = 0; i < s_vk_retiredCap; i++) {
            if (s_vk_retired[i].generation < oldest_gen) {
                oldest_gen = s_vk_retired[i].generation;
                oldest_idx = i;
            }
        }
        dlclose(s_vk_retired[oldest_idx].handle);
        slot = oldest_idx;
    }

    s_vk_retired[slot].handle = handle;
    s_vk_retired[slot].generation = s_vk_generation;
}

static void vk_advance_generation(void) {
    s_vk_generation++;
    for (size_t i = 0; i < s_vk_retiredCap; i++) {
        if (s_vk_retired[i].handle && (s_vk_generation - s_vk_retired[i].generation >= VK_RETIRED_GENERATIONS)) {
            dlclose(s_vk_retired[i].handle);
            s_vk_retired[i].handle = nullptr;
        }
    }
}

// Resolve pipeline cache function pointers from graphvex's gdpa accessor.
// Called once during init; cached as statics for hot path efficiency.
static bool resolveCacheFns(void) {
    if (!s_createCache) {
        PFN_vkGetDeviceProcAddr gdpa = Vk_getGdpa();
        if (!gdpa) return false;
        s_createCache = (PFN_vkCreatePipelineCache)gdpa(Vk_getDevice(), "vkCreatePipelineCache");
        s_destroyCache = (PFN_vkDestroyPipelineCache)gdpa(Vk_getDevice(), "vkDestroyPipelineCache");
        s_getCacheData = (PFN_vkGetPipelineCacheData)gdpa(Vk_getDevice(), "vkGetPipelineCacheData");
    }
    return s_createCache && s_destroyCache && s_getCacheData;
}

// Initialize the Vulkan module loader shim.
// Vk_init() must have been called first (by the caller) so the device exists.
bool hot_vk_init_loader(void) {
    if (!Vk_ready())
        return false;

    // Grab the device handle from graphvex.
    VkDevice dev = Vk_getDevice();
    if (dev == VK_NULL_HANDLE)
        return false;

    // Resolve cache function pointers from graphvex's seam.
    if (!resolveCacheFns())
        return false;

    // Create pipeline cache, seeded from disk when a prior run saved one.
    // The cache blob is driver-versioned: vkCreatePipelineCache rejects
    // stale data itself, so a corrupt/mismatched file just falls back to
    // an empty cache — never a fatal error.
    uint8_t *cache_data = nullptr;
    size_t cache_size = 0;
    FILE *cache_in = fopen("hot/.pipeline_cache", "rb");
    if (cache_in) {
        fseek(cache_in, 0, SEEK_END);
        long cache_len = ftell(cache_in);
        fseek(cache_in, 0, SEEK_SET);
        if (cache_len > 0 && cache_len < 16 * 1024 * 1024) {
            cache_data = (uint8_t*) malloc((size_t) cache_len);
            if (cache_data) {
                if (fread(cache_data, 1, (size_t) cache_len, cache_in) == (size_t) cache_len)
                    cache_size = (size_t) cache_len;
                else {
                    free(cache_data);
                    cache_data = nullptr;
                }
            }
        }
        fclose(cache_in);
    }
    VkPipelineCacheCreateInfo cache_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = cache_size,
        .pInitialData = cache_data,
    };
    s_createCache(dev, &cache_ci, nullptr, &s_cache);
    if (cache_data)
        free(cache_data);

    // The trampoline table lives as long as the loader: rows persist across
    // reloads so a reload patches familiar rows instead of re-creating them.
    if (!trampolineEnsure())
        return false;

    s_initialized = true;
    printf("[vk_loader] initialized (device=%p, cache=%p)\n",
           (void*) Vk_getDevice(), (void*) s_cache);
    return true;
}

// Load the Vulkan module from a dylib
bool hot_vk_load_module(const char *path) {
    if (s_module_handle) {
        // Shutdown old module first
        if (s_module_shutdown) s_module_shutdown();
        vk_retire_handle(s_module_handle);
        vk_advance_generation();
        s_module_handle = nullptr;
        s_initialized = false;
    }

    s_module_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!s_module_handle) {
        fprintf(stderr, "[vk_loader] dlopen failed: %s\n", dlerror());
        return false;
    }

    // Get module functions
    s_module_init = (VkModuleInitFn)dlsym(s_module_handle, "VkModuleInit");
    s_module_shutdown = (VkModuleShutdownFn)dlsym(s_module_handle, "VkModuleShutdown");
    s_module_get_trampolines = (VkModuleGetTrampolinesFn)dlsym(s_module_handle, "VkModuleGetTrampolines");
    s_module_get_manifest = (VkModuleGetManifestFn)dlsym(s_module_handle, "VkModuleGetManifest");

    if (!s_module_init || !s_module_get_trampolines || !s_module_get_manifest) {
        fprintf(stderr, "[vk_loader] missing required exports\n");
        dlclose(s_module_handle);
        s_module_handle = nullptr;
        return false;
    }

    // Get manifest and verify ABI
    const VkModuleManifest *manifest = s_module_get_manifest();
    printf("[vk_loader] loading %s v%s\n", (*manifest).name, (*manifest).version);

    // Build the context from graphvex's seam — all handles flow from here.
    VkHotContext context = {
        .instance = Vk_getInstance(),
        .physical_device = Vk_getPhys(),
        .device = Vk_getDevice(),
        .queue = Vk_getQueue(),
        .queue_family = Vk_getQueueFamily(),
        .pipeline_cache = s_cache,
        .pipeline_cache_path = "hot/.pipeline_cache",
        .vulkan_api_version = VK_API_VERSION_1_2,
        .pipeline_cache_size = 0,
        .texture_registry = nullptr,
    };

    // Initialize the module
    if (!s_module_init(&context)) {
        fprintf(stderr, "[vk_loader] module init failed\n");
        dlclose(s_module_handle);
        s_module_handle = nullptr;
        return false;
    }

    // Register trampolines
    uint32_t trampoline_count = 0;
    const VkTrampolineEntry *trampolines = s_module_get_trampolines(&trampoline_count);
    for (uint32_t i = 0; i < trampoline_count; i++) {
        int idx = trampoline_find(trampolines[i].name);
        if (idx < 0) idx = trampoline_create(trampolines[i].name);
        Trampoline *row = trampolineRow(idx);
        if (row) {
            void *old = atomic_load(&(*row).ptr);
            if (old && old != trampolines[i].function) {
                atomic_store(&(*row).fallback_ptr, old);
            }
            atomic_store(&(*row).ptr, trampolines[i].function);
        }
    }

    s_initialized = true;
    printf("[vk_loader] module loaded (%u trampolines)\n", trampoline_count);
    return true;
}

// Shutdown the Vulkan module loader shim
void hot_vk_shutdown(void) {
    if (s_module_shutdown) s_module_shutdown();
    if (s_module_handle) {
        dlclose(s_module_handle);
        s_module_handle = nullptr;
    }
    for (size_t i = 0; i < s_vk_retiredCap; i++) {
        if (s_vk_retired[i].handle) {
            dlclose(s_vk_retired[i].handle);
            s_vk_retired[i].handle = nullptr;
        }
    }
    free(s_vk_retired);
    s_vk_retired = NULL;
    s_vk_retiredCap = 0;
    s_initialized = false;

    // The table requires quiescence like every ChunkedList teardown: no
    // get_symbol readers may be in flight (shutdown already closed the
    // modules they resolve through).
    if (s_trampolines) {
        ChunkedList_free(s_trampolines);
        s_trampolines = nullptr;
    }

    // Persist + destroy pipeline cache. vkGetPipelineCacheData sizes the
    // blob; a zero size or error simply skips the write. All handles come
    // from graphvex's seam.
    if (s_cache && s_getCacheData) {
        size_t data_size = 0;
        VkDevice dev = Vk_getDevice();
        if (s_getCacheData(dev, s_cache, &data_size, nullptr) == VK_SUCCESS && data_size > 0) {
            uint8_t *data = (uint8_t*) malloc(data_size);
            if (data) {
                if (s_getCacheData(dev, s_cache, &data_size, data) == VK_SUCCESS) {
                    FILE *cache_out = fopen("hot/.pipeline_cache", "wb");
                    if (cache_out) {
                        fwrite(data, 1, data_size, cache_out);
                        fclose(cache_out);
                    }
                }
                free(data);
            }
        }
        if (s_destroyCache)
            s_destroyCache(Vk_getDevice(), s_cache, nullptr);
        s_cache = VK_NULL_HANDLE;
    }

    printf("[vk_loader] shutdown\n");
}
