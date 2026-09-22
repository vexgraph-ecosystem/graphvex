#ifndef VULKAN_VK_LAYER_H
#define VULKAN_VK_LAYER_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

// vulkan/vk_layer.h — retained offscreen render-target registry ("layers").
//
// Every scene is COMPOSITED (the Present-On-Demand Law): a scene renders into
// a fixed pixel-size offscreen flight target on the present worker
// (VkLayer_visit), then the canvas painter samples its last-published image
// as a textured quad (VkLayer_composite) into the board pass at the scene's
// anchor rect. One seam canvas total — no per-scene CAMetalLayer surfaces,
// never a pane swapchain (the Single-Seam Canvas Law).
//
// structural invariant (the Present-On-Demand Law): composite != render — visit() invokes the
// scene's render handler into its retained target; composite() only copies
// published pixels. The canvas can never re-invoke a scene render.
//
// THREAD CONTRACT: register/resize/unregister run on thread 0 (window
// attach/composite path). visit/composite run on the present worker. The
// registry is a fixed array — zero steady-state allocation (Rule: no malloc
// in tick/rendering paths). Same-queue ordering makes visit-then-composite
// safe: layer submits land before the collaging read in queue order, so a
// published slot is always finished before the composite samples it.

typedef void (*VkLayerRenderFn)(void *cmdBuffer, int w, int h, void *owner);

// Register a retained offscreen target of the given pixel size. Returns the
// slot index or -1. `owner` is an opaque handle the renderer callback (and
// VkLayer_find) receive back — the darling Panel* the layer renders.
int VkLayer_register(int width, int height, void *owner);

// Unregister + tear down a layer flight target. Returns false if the index
// is stale. Thread 0 only (teardown / detach path).
bool VkLayer_unregister(int index);

// Resize a layer flight target. No-op (returns true) when the size is
// unchanged — the whole point: a fixed-size layer NEVER rebuilds on window
// resize (the Single-Seam Canvas Law). Rebuilds the offscreen targets only on true drift.
// Thread 0 only.
bool VkLayer_resize(int index, int width, int height);

// Render every registered dirty layer into its offscreen flight target on
// the present worker's queue — the same queue whose board pass composites
// them. Non-blocking fence polls (the Bounded Wait Law; drop-degrade keeps the last
// published image); clean layers rest on their last render (the Present-On-Demand Law).
// Returns true if at least one layer was (re)rendered.
bool VkLayer_visit(void);

// Sample one layer's last-published flight image into an ALREADY-begun render
// pass at the given canvas rect (the scene's anchor, in canvas pixels). Tint
// is applied by the composite shader; the quad is drawn in place (collaged).
// Returns false when the layer is unknown or has not published a frame yet.
bool VkLayer_composite(void *cmdBuffer, float surfaceW, float surfaceH,
                       int index, float x, float y, float w, float h,
                       float r, float g, float b, float a);

// Slot lookup for an owner handle (the darling Panel*). Returns the layer
// index or -1. The composite pass resolves child -> index here.
int VkLayer_find(void *owner);

// Current fixed pixel extent of a layer chain (diagnostic probe for the
// board/child resize logging in the darling bridge). Bounds-checked: a
// stale, out-of-range, or inactive index answers zero-extent. Lock-free
// read like the other diagnostic getters.
VkExtent2D VkLayer_extent(int index);

bool VkLayer_ready(void);
int VkLayer_count(void);

// Registry-wide repaint-demand probe (the Present-On-Demand Law): true when
// ANY active layer chain is dirty. The GfxLoop demand probe calls this every
// pass so retained boards/COMPOSITED scenes re-present on demand while the
// loop rests.
bool VkLayer_hasDemand(void);

// Lifetime diagnostics per layer (stale index answers 0): successful renders
// and fence-poll/clean skips. Read by the ANTI_VK_TRACE probe; zero hot-path
// logging.
uint64_t VkLayer_presentCount(int index);
uint64_t VkLayer_skipCount(int index);

// Registry-wide publish generation: bumps on every layer publish (any
// chain). Demand probes snapshot it and re-arm on delta, so a frame
// published after the last composite still summons one more present —
// the publish itself is demand (closes the empty-settle deadlock where
// dirt was consumed before the composite sampled the fresh frame).
uint64_t VkLayer_publishGeneration(void);

// Per-layer repaint demand (slot-record bit, the Single Class Per File Law — no new class).
// Setter takes (index, dirty): selector first, value last (the Dest-Last Law dest-last
// idiom, cf. Panel_setSize). Boolean getter answers the symmetric probe
// (the Symmetric Getter/Setter Completeness Law). Register/resize set demand; the visit walk clears it after a
// successful render; thread 0 (or the preFrame bridge) sets it when the
// scene tree dirties. A clean layer is skipped after its fence poll.
void VkLayer_markDirty(int index, bool dirty);
bool VkLayer_isDirty(int index);

// Flight probe for texture-retire safety (the Ecosystem Vulkan Safety Nets Law net): true only when no
// layer submit is pending anywhere in the registry — no offscreen CB that
// sampled bindless descriptors is still executing. Non-blocking
// GetFenceStatus poll, never waits/allocs; a busy answer safely defers
// texture destruction.
bool VkLayer_flightIdle(void);

// Teardown: destroy all layer targets + composite pipeline + sampler.
// MUST run before the instance dies (the Teardown Order Law), called by Vk_shutdown.
void VkLayer_shutdown(void);

// Global renderer hook set by the UI compositor (darling). Invoked per layer
// with the layer's own command buffer + size + owner handle, inside the
// layer's offscreen render pass.
void VkLayer_setRenderer(VkLayerRenderFn fn);

#endif