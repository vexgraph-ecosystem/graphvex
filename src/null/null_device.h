#ifndef NULL_NULL_DEVICE_H
#define NULL_NULL_DEVICE_H

#include "device/device.h"

// null/null_device.h — the null dialect of the Device contract.
//
// The no-op device: it accepts every verb and does nothing observable. It exists
// so tests and deterministic-teardown proofs can run the whole device lifecycle
// with zero GPU, zero window, and zero side effects — and so the registry can be
// exercised with more than one dialect without a real backend.
//
// Register at boot:
//   Device_registerRow(Null_row());

const DeviceRow *Null_row(void);

#endif // NULL_NULL_DEVICE_H
