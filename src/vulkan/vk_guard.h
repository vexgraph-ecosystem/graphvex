#ifndef VK_GUARD_H
#define VK_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// CLASS: VkGuard — ecosystem Rule 39 seam guard (header-only; no struct, no .c)
// LEVEL: L4 Self-Management (the net sits at the bottom, above nothing)
//
// One canonical implementation across graphvex / hotcwap / darling — zero
// per-repo forks, zero loopholes. Every driver-facing (hot) function calls
// VkGuard_check at entry BEFORE touching the driver. Tree-shaken: under
// NDEBUG (release) the check is a macro no-op — zero calls, zero branches,
// unevaluated arguments; release ships the Rule 35 hot-minimal skeleton.
//
// Contract — VkGuard_check(seam, device, queue, deviceLost):
//   * deviceLost  -> returns false (the owning module's latch announced the
//                    true site; read that log first — per Rule 39.4 the
//                    MoltenVK/Metal line names the cause, not this seam)
//   * device null -> returns false + log-once per file (lifecycle bug: a
//                    driver call must never run on a zombie/unrung handle)
//   * queue null  -> permitted ONLY on device-resource seams (create/destroy/
//                    record); any seam that submits/fences/presents must pass
//                    its queue so the whole queue health chain is covered
//
// Callers degrade exactly like any transient failure: return false, keep
// dirty state, retry next tick (Rules 27 + 35). Never crash, never UB,
// never a wedged wait. Deterministic: same input -> same seam -> same log.
// ---------------------------------------------------------------------------
#ifndef NDEBUG

static inline bool VkGuard_check(const char *seam, void *device, void *queue, bool deviceLost) {
    (void) queue;
    if (deviceLost)
        return false;
    if (device == nullptr) {
        static bool s_guardLogged;
        if (!s_guardLogged) {
            s_guardLogged = true;
            fprintf(stderr, "vk: seam \"%s\" blocked — device is null (lifecycle bug, not device loss; Rule 39)\n", seam);
            fflush(stderr);
        }
        return false;
    }
    return true;
}

#else

#define VkGuard_check(seam, device, queue, deviceLost) ((void) 0, true)

#endif // NDEBUG

#endif // VK_GUARD_H