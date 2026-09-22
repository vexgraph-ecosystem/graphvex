#ifndef VK_GUARD_H
#define VK_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// CLASS: VkGuard — ecosystem the Ecosystem Vulkan Safety Nets Law seam guard (header-only; no struct, no .c)
// LEVEL: L4 Self-Management (the net sits at the bottom, above nothing)
//
// One canonical implementation across graphvex / hotcwap / darling — zero
// per-repo forks, zero loopholes. Every driver-facing (hot) function calls
// VkGuard_check at entry BEFORE touching the driver. Tree-shaken: under
// NDEBUG (release) the check is a macro no-op — zero calls, zero branches,
// unevaluated arguments; release ships the Cold-Strict, Hot-Minimal Validation Law hot-minimal skeleton.
//
// Contract — VkGuard_check(seam, device, queue, deviceLost):
//   * deviceLost  -> returns false (the owning module's latch announced the
//                    true site; read that log first — per the Ecosystem Vulkan Safety Nets Law the
//                    MoltenVK/Metal line names the cause, not this seam)
//   * device null -> returns false + log-once per file (lifecycle bug: a
//                    driver call must never run on a zombie/unrung handle)
//   * queue null  -> returns false + log-once per file (submit/fence/present
//                    seams MUST pass their queue so the whole queue health
//                    chain is covered — the Bounded Wait Law/39: an unwaitable submit
//                    wedges teardown). Device-resource seams (create/destroy/
//                    record/export with no queue touch) are the explicit
//                    opt-out: they call VkGuard_checkResource instead, never
//                    VkGuard_check with a null queue.
//
// Callers degrade exactly like any transient failure: return false, keep
// dirty state, retry next tick (the Bounded Wait Law + the Cold-Strict,
// Hot-Minimal Validation Law). Never crash, never UB,
// never a wedged wait. Deterministic: same input -> same seam -> same log.
// ---------------------------------------------------------------------------
#ifndef NDEBUG

static inline bool VkGuard_check(const char *seam, void *device, void *queue, bool deviceLost) {
    if (deviceLost)
        return false;
    if (device == nullptr) {
        static bool s_guardLogged;
        if (!s_guardLogged) {
            s_guardLogged = true;
            fprintf(stderr, "vk: seam \"%s\" blocked — device is null (lifecycle bug, not device loss; the Ecosystem Vulkan Safety Nets Law)\n", seam);
            fflush(stderr);
        }
        return false;
    }
    if (queue == nullptr) {
        static bool s_queueLogged;
        if (!s_queueLogged) {
            s_queueLogged = true;
            fprintf(stderr, "vk: seam \"%s\" blocked — queue is null (submit/fence/present seam without a queue wedges teardown; resource seams use VkGuard_checkResource; the Ecosystem Vulkan Safety Nets Law)\n", seam);
            fflush(stderr);
        }
        return false;
    }
    return true;
}

// Explicit opt-out for device-resource seams (create/destroy/record/export):
// no queue is touched, so none is required. Submit/fence/present seams must
// never call this — they use VkGuard_check with their live queue.
static inline bool VkGuard_checkResource(const char *seam, void *device, bool deviceLost) {
    if (deviceLost)
        return false;
    if (device == nullptr) {
        static bool s_resLogged;
        if (!s_resLogged) {
            s_resLogged = true;
            fprintf(stderr, "vk: seam \"%s\" blocked — device is null (lifecycle bug, not device loss; the Ecosystem Vulkan Safety Nets Law)\n", seam);
            fflush(stderr);
        }
        return false;
    }
    return true;
}

#else

#define VkGuard_check(seam, device, queue, deviceLost) ((void) 0, true)
#define VkGuard_checkResource(seam, device, deviceLost) ((void) 0, true)

#endif // NDEBUG

#endif // VK_GUARD_H