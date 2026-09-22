#ifndef VK_MAC_H
#define VK_MAC_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "annotation/platform_exclusive.h"
#include "annotation/intention.h"

// vulkan/vk_mac.h — macOS-specific Vulkan backend functions.
;;PLATFORM_EXCLUSIVE("Mac")
;;INTENTION("MoltenVK loader, CAMetalLayer surface creation, and device accessors for macOS.")

// Accessors for cross-platform state owned by vk_instance.c (formerly vulkan.c).
extern VkDevice Vk_getDevice(void);
extern VkQueue Vk_getQueue(void);
extern VkCommandBuffer Vk_getCmdBuffer(void);
extern VkPipeline Vk_getTriPipeline(void);
extern VkPipelineLayout Vk_getTriLayout(void);
extern uint64_t Vk_getAnimStartNanos(void);
extern PFN_vkGetDeviceProcAddr Vk_getGdpa(void);
extern VkInstance Vk_getInstance(void);
extern PFN_vkGetInstanceProcAddr Vk_getGpa(void);
extern VkPhysicalDevice Vk_getPhys(void);
extern uint32_t Vk_getQueueFamily(void);

// Load a Vulkan device function pointer (void return, no error check).
#define MAC_LOAD_DEVICE(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = (PFN_vk##name)Vk_getGdpa()(Vk_getDevice(), "vk" #name); \

// Load the Vulkan loader library (MoltenVK on macOS, Khronos loader fallback).
void *VkMac_loadLib(void);

// Create a VkSurfaceKHR from the window's CAMetalLayer. `window` is an opaque
// handle whose metalLayer extraction is wired through Vk_setWindowSeam.
bool VkMac_createSurface(void *window, VkInstance instance,
                         PFN_vkGetInstanceProcAddr gpa, VkSurfaceKHR *outSurface);

// Create a VkSurfaceKHR from ANY CAMetalLayer (the single seam canvas host).
// The layer must already exist; its drawableSize is the surface extent.
bool VkMac_createSurfaceForLayer(void *layer, VkInstance instance,
                                 PFN_vkGetInstanceProcAddr gpa, VkSurfaceKHR *outSurface);

// Ensure the BGRA8 offscreen/IOSurface render pass exists.
bool VkMac_ensureIOSurfacePass(void);

// Get the BGRA8 offscreen/IOSurface render pass (call VkMac_ensureIOSurfacePass first).
VkRenderPass VkMac_getIOSurfacePass(void);

// Resize render trampoline (no-op on macOS; Vulkan has its own worker thread).
void VkMac_resizeRenderTrampoline(void *userdata);

#endif
