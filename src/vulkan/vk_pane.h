#ifndef VULKAN_VK_PANE_H
#define VULKAN_VK_PANE_H

#include <stdbool.h>
#include <stdint.h>

// vulkan/vk_pane.h — per-CAMetalLayer swapchain registry ("panes of glass").
//
// Every visible child (Scene3D anchored panes today) owns a real CAMetalLayer
// with its OWN Vulkan surface + swapchain. The board swapchain stays the
// window compositor's; each pane presents independently, so a live window
// resize only EVER moves layer frames — WindowServer composites the stack,
// and the panes never rebuild unless their own size changes.
//
// THREAD CONTRACT: register/resize/unregister run on thread 0 (window
// attach/composite path). presentAll runs on the Kernel tick thread. The
// registry is a fixed array — zero steady-state allocation (Rule: no malloc
// in tick/rendering paths).

typedef void (*VkPaneRenderFn)(void *cmdBuffer, int w, int h, void *owner);

// Register a CAMetalLayer pane of the given pixel size. Returns chain index
// or -1. `owner` is an opaque handle the renderer callback receives back
// (the darling Panel* the pane renders).
int VkPane_register(void *layer, int width, int height, void *owner);

// Unregister + tear down a pane chain. Returns false if the index is stale.
// Thread 0 only (teardown / detach path).
bool VkPane_unregister(int index);

// Resize a pane chain. No-op (returns true) when the size is unchanged —
// the whole point: a fixed-size anchored pane NEVER rebuilds on window
// resize. Rebuilds the swapchain only when the pane's own size drifts.
// Thread 0 only.
bool VkPane_resize(int index, int width, int height);

// Present every registered pane chain. Each chain acquires its own image,
// records the renderer callback into it, and presents — paced by the
// display (FIFO). Drops frames on bounded timeouts; never stalls. Returns
// true if at least one pane presented.
bool VkPane_presentAll(void);

bool VkPane_ready(void);
int VkPane_count(void);

// Lifetime diagnostics per chain (stale index answers 0): successful
// presents and fence-poll/clean skips (stale drawable kept). Read by the
// ANTI_VK_TRACE probe; zero hot-path logging.
uint64_t VkPane_presentCount(int index);
uint64_t VkPane_skipCount(int index);

// Per-chain repaint demand (slot-record bit, the Single Class Per File Law — no new class).
// Setter takes (index, dirty): selector first, value last (the Dest-Last Law dest-last
// idiom, cf. Panel_setSize). Boolean getter answers the symmetric probe
// (the Symmetric Getter/Setter Completeness Law). Register/resize set demand; the present walk clears it after a
// successful present; thread 0 (or the preFrame bridge) sets it when the
// panel tree dirties. A clean chain is skipped after its fence poll.
void VkPane_markDirty(int index, bool dirty);
bool VkPane_isDirty(int index);

// Flight probe for texture-retire safety (the Ecosystem Vulkan Safety Nets Law net): true only when no
// pane submit is pending anywhere in the registry — i.e. no pane CB that
// sampled bindless descriptors is still executing (FreeMemory under a flying
// pane Submit is the GPU-page-fault defect). Non-blocking GetFenceStatus
// poll, never waits/allocs; a busy answer safely defers texture destruction.
bool VkPane_flightIdle(void);

// Teardown: destroy all pane surfaces/swapchains. MUST run before the
// instance dies (the Teardown Order Law: destroy top-down, free last).
void VkPane_shutdown(void);

// Global renderer hook set by the UI compositor (darling). Invoked per pane
// with the pane's own command buffer + size + owner handle.
void VkPane_setRenderer(VkPaneRenderFn fn);

#endif