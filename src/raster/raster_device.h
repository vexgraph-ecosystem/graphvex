#ifndef RASTER_RASTER_DEVICE_H
#define RASTER_RASTER_DEVICE_H

#include "device/device.h"

// raster/raster_device.h — the raster dialect of the Device contract.
//
// The CPU software device: it owns an Image (the CPU framebuffer — its
// drawable) and presents by simply marking the frame done. No GPU, no window,
// no loader — which makes it the headless/CI dialect and the reference every
// GPU dialect must agree with pixel-for-pixel (the RasterGraphics row of the
// unified table runs on this device).
//
// Register at boot:
//   Device_registerRow(Raster_row());

const DeviceRow *Raster_row(void);

#endif // RASTER_RASTER_DEVICE_H
