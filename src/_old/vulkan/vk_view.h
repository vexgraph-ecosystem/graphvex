#ifndef VULKAN_VK_VIEW_H
#define VULKAN_VK_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <vulkan/vulkan_core.h>

// vulkan/vk_view.h — the per-monitor render cache (the compositor view).
//
// One VkView per attached monitor, keyed off system/discovery's
// DisplayMonitor list. Each view owns a GIANT off-screen image the size of
// that monitor's native screen resolution — the cache. Everything renders
// into absolute desktop coordinates on the cache; a window is nothing but a
// scissored blit region of it.
//
// ISOLATION CONTRACT: this file talks to Vulkan + system/DisplayInfo ONLY.
// No window.h, no darling. That keeps it debuggable in isolation: bootstrap
// views from DisplayInfo, clear, draw, blit — nothing else knows it exists.

typedef struct VkView VkView;

// (Re)enumerate monitors from DisplayInfo and build one view per monitor.
// Safe to call again on monitor changes: existing views whose monitor still
// exists are kept, new monitors get views, gone monitors are destroyed.
bool     VkView_refreshAll(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                           VkPhysicalDevice phys, VkDevice device);

size_t   VkView_count(void);
VkView  *VkView_at(size_t index);

// The view whose desktop rect contains global point (x, y) in AppKit points,
// or nullptr when the point falls outside every monitor (then callers pick at(0)).
VkView  *VkView_forPoint(float x, float y);

// The view mirroring this display id — the same CGDirectDisplayID that
// Window_getMonitorId and DisplayMonitor_getId speak. nullptr when discovery
// has not produced a view for it yet (fresh hotplug).
VkView  *VkView_forMonitor(uint32_t displayId);

// Desktop origin of this monitor in global AppKit points.
float    VkView_getOriginX(const VkView *view);
float    VkView_getOriginY(const VkView *view);

// Cache extent: the ACTIVE pixel mode of the monitor (what the window server
// actually composites — cache pixels are glass pixels, blits never resample),
// falling back to the native panel grid, then points. Never point-derived by
// choice; retina scaling is read off the mode, not multiplied in.
int32_t  VkView_getWidth(const VkView *view);
int32_t  VkView_getHeight(const VkView *view);

// The monitor's logical point extent (for px-per-point conversion).
int32_t  VkView_getPointWidth(const VkView *view);
int32_t  VkView_getPointHeight(const VkView *view);

// The render pass targeting this view's cache framebuffer. Pipelines built
// against any view's pass execute on all of them (same format, single subpass).
VkRenderPass VkView_renderPass(const VkView *view);

// The raw cache image — the source of every window blit.
VkImage   VkView_image(const VkView *view);

// Begin the cache pass: clears the ENTIRE cache to (r,g,b,a) — every pixel,
// even regions no window will ever sample. Frame is complete by construction.
bool     VkView_beginPass(VkView *view, VkCommandBuffer cb,
                          float r, float g, float b, float a);

// End the cache pass and barrier the image to TRANSFER_SRC_OPTIMAL so a blit
// can sample it.
bool     VkView_endPass(VkView *view, VkCommandBuffer cb);

void     VkView_shutdown(void);

#endif
