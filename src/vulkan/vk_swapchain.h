#ifndef VULKAN_VK_SWAPCHAIN_H
#define VULKAN_VK_SWAPCHAIN_H

#include "vulkan/vk_device.h"

// Backend-private, single-thread/owner-affine. The Device and its window surface
// must outlive this object. No submit or present is permitted in this milestone:
// callers may inspect the borrowed image only; they may not queue work using it.
typedef struct VkSwapchain {
    const Device *owner;
    VkDevice native;
    VkSwapchainKHR handle;
    VkFence acquireFence;
    VkImage *images;
    uint32_t count;
    uint32_t index;
    VkFormat format;
    VkExtent2D extent;
    bool acquired;
    bool acquirePending;
    bool fencePending;
    bool poisoned;
    PFN_vkDestroySwapchainKHR destroySwapchain;
    PFN_vkDestroyFence destroyFence;
    PFN_vkWaitForFences waitFences;
    PFN_vkResetFences resetFences;
    PFN_vkAcquireNextImageKHR acquireNextImage;
    PFN_vkReleaseSwapchainImagesEXT releaseImages;
} VkSwapchain;

// width/height are native hardware pixels, not logical window points.
VkSwapchain *VkSwapchain_new(const Device *device, uint32_t width, uint32_t height);
// Refuses while acquisition ownership is live. On a rebuild failure the old
// swapchain has been destroyed; the empty object may be retried or destroyed.
// Never resize after any external GPU use of its images.
bool VkSwapchain_resize(VkSwapchain *self, uint32_t width, uint32_t height);
// Fence-only acquisition: no binary semaphore with a stranded signal.
// On a timeout or uncertain driver result the object is poisoned and retained,
// not freed: do not destroy the borrowed Device while it remains live.
bool VkSwapchain_acquire(VkSwapchain *self, VkImage *image, VkFormat *format, uint32_t *index);
// Wait at most 100ms for acquisition signal, then release with maintenance1.
// False retains ownership and requires a later retry.
bool VkSwapchain_release(VkSwapchain *self);
// Returns false without freeing when ownership or fence status is uncertain.
bool VkSwapchain_destroy(VkSwapchain *self);
VkExtent2D VkSwapchain_getExtent(const VkSwapchain *self);
uint32_t VkSwapchain_getImageCount(const VkSwapchain *self);
bool VkSwapchain_isAcquired(const VkSwapchain *self);

#endif
