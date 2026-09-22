#ifndef VULKAN_VK_SCENE_H
#define VULKAN_VK_SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <vulkan/vulkan_core.h>

// vulkan/vk_scene.h — per-scene offscreen canvases (the scene's own buffer).
//
// A scene child owns a FIXED-size offscreen image — its private canvas. The
// scene pipeline renders into that canvas with a full-canvas viewport, so
// scene geometry never remaps against window size. The collage then stamps
// the canvas onto the monitor cache as a 1:1 blit CUT: shrinking the window
// crops the scene, it can never squish.
//
// Canvases are keyed by caller-chosen keys (a scene panel's pointer identity
// works) and created/resized lazily on first acquire. Size drift recreates.
//
// ISOLATION CONTRACT: Vulkan calls only — no window.h, no darling. Callers
// speak keys, pixel extents, command buffers.
//
// CADENCE NOTE (phase roadmap): today the canvas records inside the collage's
// present submission. Flip-buffers and an independent scene clock land later;
// this module's API is deliberately shaped so neither changes callers.

typedef struct VkSceneCanvas VkSceneCanvas;

// One-time loader plumbing (same pattern as VkView_refreshAll).
bool VkSceneCanvas_initModule(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                              VkPhysicalDevice phys, VkDevice device);

// Get-or-create the canvas bound to `key` at exactly width x height pixels.
// Size drift swaps in a fresh buffer pair IMMEDIATELY (the collage tracks
// geometry every tick); the old FRONT image survives as the canvas's STALE
// bridge — see VkSceneCanvas_staleImage — while the rest of the old pair is
// generation-retired and dies at the next completed batch harvest, the
// same discipline the swapchain graveyard uses. nullptr on failure/exhaustion.
VkSceneCanvas *VkSceneCanvas_acquire(uintptr_t key, uint32_t width, uint32_t height);

// Free every retired buffer pair. Call exactly once per completed batch
// harvest: the fence proof covers anything retired before it.
void VkSceneCanvas_flushRetired(void);

uint32_t VkSceneCanvas_width(const VkSceneCanvas *canvas);
uint32_t VkSceneCanvas_height(const VkSceneCanvas *canvas);

// The FINISHED image (last flipped back buffer), ready as a TRANSFER_SRC
// blit source. VK_NULL_HANDLE until the first scene pass completes — and
// again while a resize swap is pending its first flip.
VkImage  VkSceneCanvas_frontImage(const VkSceneCanvas *canvas);

// The STALE BRIDGE: the front image of the PREVIOUS geometry, kept alive
// across a resize so the collage never shows a hole mid-drag. Valid only
// while VkSceneCanvas_frontImage returns VK_NULL_HANDLE (callers check
// front first); the next flip retires it through the graveyard. Reports
// its own pixel size via out params — it does NOT match width/height.
VkImage  VkSceneCanvas_staleImage(const VkSceneCanvas *canvas,
                                  uint32_t *outWidth, uint32_t *outHeight);

// Record the BACK buffer pass: clears the entire back image to (r,g,b,a),
// leaves the pass open for inline draws. Full-canvas viewport/scissor are
// the caller's job (dynamic state). Pipelines built against any VkView's
// renderpass execute here unchanged.
bool VkSceneCanvas_beginBackPass(VkSceneCanvas *canvas, VkCommandBuffer cb,
                                 float r, float g, float b, float a);

// Close the back pass; the back image becomes TRANSFER_SRC_OPTIMAL.
bool VkSceneCanvas_endBackPass(VkSceneCanvas *canvas, VkCommandBuffer cb);

// Caller-side lifecycle for the phase-2 hand-off: the caller owns the batch
// fence and time; these carry the state across frames. Flips apply only when
// the canvas generation still matches the one recorded into the batch —
// resized canvases refuse stale flips.
bool VkSceneCanvas_needsRender(const VkSceneCanvas *canvas, uint64_t nowNs,
                               int64_t minGapNs); // !pending && clock due
uint32_t VkSceneCanvas_generation(const VkSceneCanvas *canvas);
void VkSceneCanvas_markSubmitted(VkSceneCanvas *canvas, uint64_t nowNs);
void VkSceneCanvas_flip(VkSceneCanvas *canvas);   // back becomes front

void VkSceneCanvas_shutdownModule(void);

#endif
