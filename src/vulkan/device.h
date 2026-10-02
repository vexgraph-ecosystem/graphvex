#ifndef GRAPHICS_DEVICE_H
#define GRAPHICS_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

// graphvex R3 — vulkan/device.h
//
// The Vulkan session: instance → physical device → logical device → queue.
// On Apple this runs over MoltenVK; on Windows/Linux it is the native loader.
//
// *** NO SWAPCHAIN, EVER. *** We never touch VK_KHR_swapchain. The on-screen
// destination is a host-borrowed seam (a CAMetalLayer on Apple); we render into
// our OWN Images/Boards and the host copies the completed frame out. "Device"
// therefore means exactly the GPU session — nothing about presentation.

typedef struct Device Device;

Device *Device_create(bool enableValidation);
void Device_destroy(Device *device);
bool Device_isValid(const Device *device);
const char *Device_lastError(const Device *device);
const char *Device_name(const Device *device);
void *Device_native(const Device *device);   // opaque VkDevice (transit only)
void *Device_instance(const Device *device);
void *Device_physical(const Device *device);
void *Device_queue(const Device *device);
uint32_t Device_queueFamily(const Device *device);

#endif // GRAPHICS_DEVICE_H
