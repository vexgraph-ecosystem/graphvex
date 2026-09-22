#ifndef VK_IOSURFACE_H
#define VK_IOSURFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#if defined(__APPLE__)
// libdispatch first: the Xcode SDK's IOSurface -> xpc headers need
// dispatch_queue_t, and plain C translation units never pull Foundation.
#include <dispatch/dispatch.h>
#endif
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>

typedef struct VkIOSurface VkIOSurface;
//
// Lets a VkImage be exported as an IOSurfaceRef (for AppKit compositing) or
// an IOSurfaceRef be imported as a VkImage (for Vulkan rendering into a
// surface AppKit owns). Zero copy — same GPU memory.

// Per-scene offscreen canvases (VkSceneCanvas) — see vk_scene.h for the
// full API. The bridge below is the Vulkan ↔ IOSurface handoff.

// Initialize the bridge with Vulkan instance/device. Call once after
// VkInit succeeds. Returns false if VK_EXT_metal_objects is unavailable.
bool VkIOSurface_initModule(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                            VkPhysicalDevice phys, VkDevice device);

// Create an IOSurface-backed VkImage. The IOSurface is allocated at the given
// size with BGRA8 format (safe for both Vulkan and AppKit). The outImage
// can be used as a color attachment, transfer source, etc.
//
// Returns nullptr on failure. The returned VkIOSurface owns both the IOSurface
// and the VkImage; call VkIOSurface_free to release.
VkIOSurface *VkIOSurface_create(uint32_t width, uint32_t height);

// Wrap an existing IOSurfaceRef as a VkImage. The IOSurface must have been
// created with BGRA8 format (kCVPixelFormatType_32BGRA). Does NOT take ownership
// of the IOSurfaceRef — caller must keep it alive. Call VkIOSurface_free to
// release the wrapper (but not the IOSurfaceRef).
VkIOSurface *VkIOSurface_wrap(void *ioSurface, uint32_t width, uint32_t height);

// After Vulkan renders into the image, export the IOSurface for AppKit
// compositing. Call before setting layer.contents = VkIOSurface_getSurface().
bool VkIOSurface_export(VkIOSurface *surf);

// Free the VkIOSurface. Releases the VkImage and (if owned) the IOSurface.
void VkIOSurface_free(VkIOSurface *surf);

// The IOSurface backing. Set this as a CALayer's contents for AppKit
// compositing: layer.contents = VkIOSurface_getSurface(surf);
void *VkIOSurface_getSurface(const VkIOSurface *surf); // IOSurfaceRef

// The VkImage. Use in framebuffers, blits, etc.
void *VkIOSurface_getImage(const VkIOSurface *surf);   // VkImage

// Current size.
uint32_t VkIOSurface_width(const VkIOSurface *surf);
uint32_t VkIOSurface_height(const VkIOSurface *surf);

// Create a framebuffer for rendering into this IOSurface's VkImage.
// The render pass must be compatible (BGRA8, color attachment, loadOp=LOAD).
// Returns VK_NULL_HANDLE on failure. Caller must destroy the framebuffer.
VkFramebuffer VkIOSurface_createFramebuffer(const VkIOSurface *surf, VkRenderPass pass);

#endif
