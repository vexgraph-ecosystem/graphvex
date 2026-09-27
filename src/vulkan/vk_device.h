#ifndef VULKAN_VK_DEVICE_H
#define VULKAN_VK_DEVICE_H

#include "device/device.h"
#ifdef __APPLE__
#define VK_USE_PLATFORM_METAL_EXT 1
#elif defined(_WIN32)
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.h>

// vulkan/vk_device.h — the Vulkan dialect of the Device contract.
//
// Ported from the old reference (vulkan/vk_mac.c loader + vk_instance.c init),
// reduced to the DEVICE half: loader, instance, optional WSI surface, physical
// device, logical device, queue. Swapchain and renderer land in later slices.
//
// The row is exported so boot can register it:
//   Device_registerRow(Vulkan_row());

const DeviceRow *Vulkan_row(void);
// Internal borrowing seam: handles remain owned by Device, never destroy them here.
bool VkDevice_borrow(const Device *device, VkPhysicalDevice *physical,
                     VkDevice *native, VkQueue *queue, uint32_t *family,
                     PFN_vkGetInstanceProcAddr *gpa, VkInstance *instance);
// Borrowed WSI handle: null for offscreen devices; never destroy it as a borrower.
VkSurfaceKHR VkDevice_borrowSurface(const Device *device);
// True only when the windowed device enabled present_id + present_wait and
// resolved vkWaitForPresentKHR. Does not imply that a swapchain exists yet.
bool VkDevice_canWaitForPresent(const Device *device);
// Future swapchain seam: waits at most 100 ms; unsupported/invalid inputs fail
// closed. VK_TIMEOUT is a dropped-frame result, not proof of presentation.
VkResult VkDevice_waitForPresent(const Device *device, VkSwapchainKHR swapchain,
                                 uint64_t presentId, uint64_t timeoutNs);

#endif // VULKAN_VK_DEVICE_H
