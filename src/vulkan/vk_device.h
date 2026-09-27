#ifndef VULKAN_VK_DEVICE_H
#define VULKAN_VK_DEVICE_H

#include "device/device.h"
#include <vulkan/vulkan.h>

// vulkan/vk_device.h — the Vulkan dialect of the Device contract.
//
// Ported from the old reference (vulkan/vk_mac.c loader + vk_instance.c init),
// reduced to the DEVICE half: loader -> instance -> physical device -> logical
// device -> queue. The seam surface, pipelines, and the master renderer land on
// top of this in later slices (vulkan/vk_render.c).
//
// The row is exported so boot can register it:
//   Device_registerRow(Vulkan_row());

const DeviceRow *Vulkan_row(void);
// Internal borrowing seam: handles remain owned by Device, never destroy them here.
bool VkDevice_borrow(const Device *device, VkPhysicalDevice *physical,
                     VkDevice *native, VkQueue *queue, uint32_t *family,
                     PFN_vkGetInstanceProcAddr *gpa, VkInstance *instance);

#endif // VULKAN_VK_DEVICE_H
