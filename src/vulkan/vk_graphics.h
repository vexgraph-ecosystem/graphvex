#ifndef VULKAN_VK_GRAPHICS_H
#define VULKAN_VK_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include "lang/graphics.h"

// One process-wide offscreen Graphics row. The Device owns all Vulkan device handles.
// Unbind before destroying the Device; its destruction also unbinds automatically.
bool VkGraphics_bind(Device *device);
void VkGraphics_unbind(void);
// False means a bounded fence wait timed out: owner must remain alive and retry.
bool VkGraphics_unbindIfDevice(VkDevice native);
const Graphics *VkGraphics_getRow(void);
// Cold RGBA8 copy, tightly packed native pixels. Requires a completed frame.
// A failed bounded wait leaves the submitted frame pending for a later retry.
bool VkGraphics_readback(size_t capacity, uint8_t *dest);
// Backend-private loan: only a completed, dirty transfer-src frame for this Device.
// The image remains owned by VkGraphics and valid until the next begin/resize/unbind.
bool VkGraphics_borrowPresentImage(const Device *device, VkImage *image, VkExtent2D *extent);

#endif
