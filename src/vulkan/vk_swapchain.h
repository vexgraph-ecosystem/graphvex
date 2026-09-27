#ifndef VULKAN_VK_SWAPCHAIN_H
#define VULKAN_VK_SWAPCHAIN_H

#include "vulkan/vk_device.h"

// Backend-private, single-thread/owner-affine. The Device and its window surface
// must outlive this object. All calls, including source borrowing, are owner-affine.
typedef struct VkSwapchain {
    const Device *owner;
    VkDevice native;
    VkSwapchainKHR handle;
    VkFence acquireFence;
    VkFence submitFence;
    VkFence presentFence;
    VkSemaphore renderDone;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkQueue queue;
    uint64_t presentId;
    bool submitted;
    bool presented;
    bool presentPending;
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
    PFN_vkQueueSubmit queueSubmit;
    PFN_vkQueuePresentKHR queuePresent;
    PFN_vkCreateCommandPool createPool;
    PFN_vkDestroyCommandPool destroyPool;
    PFN_vkAllocateCommandBuffers allocateCommands;
    PFN_vkResetCommandBuffer resetCommand;
    PFN_vkBeginCommandBuffer beginCommand;
    PFN_vkEndCommandBuffer endCommand;
    PFN_vkCmdPipelineBarrier barrier;
    PFN_vkCmdBlitImage blit;
    PFN_vkCreateSemaphore createSemaphore;
    PFN_vkDestroySemaphore destroySemaphore;
} VkSwapchain;

// width/height are native hardware pixels, not logical window points.
VkSwapchain *VkSwapchain_new(const Device *device, uint32_t width, uint32_t height);
// Refuses while acquisition ownership is live. On a rebuild failure the old
// swapchain has been destroyed; the empty object may be retried or destroyed.
// Submitted and presented use is retired by both bounded completion proofs
// before images are destroyed. Failure retains the old owner for retry.
bool VkSwapchain_resize(VkSwapchain *self, uint32_t width, uint32_t height);
// Fence-only acquisition: no binary semaphore with a stranded signal.
// On a timeout or uncertain driver result the object is poisoned and retained,
// not freed: do not destroy the borrowed Device while it remains live.
bool VkSwapchain_acquire(VkSwapchain *self, VkImage *image, VkFormat *format, uint32_t *index);
// Wait at most 100ms for acquisition signal, then release with maintenance1.
// False retains ownership and requires a later retry.
bool VkSwapchain_release(VkSwapchain *self);
// Source must be a completed RGBA8 transfer-src image belonging to this Device.
// False on a bounded wait keeps the slot and owner alive for retry.
bool VkSwapchain_present(VkSwapchain *self, VkImage source, VkExtent2D sourceExtent);
// Returns false without freeing when ownership or fence status is uncertain.
bool VkSwapchain_destroy(VkSwapchain *self);
VkExtent2D VkSwapchain_getExtent(const VkSwapchain *self);
uint32_t VkSwapchain_getImageCount(const VkSwapchain *self);
bool VkSwapchain_isAcquired(const VkSwapchain *self);

#endif
