#ifndef LANG_SIZE_H
#define LANG_SIZE_H

#include <stdbool.h>
#include <stdint.h>

// lang/size.h — the AUTO sentinel (a FourCC magic) + the AUTO protocol.
//
// SIZE_AUTO / LENGTH_AUTO are ONE magic: the FourCC "åuto" packed as a signed
// 32-bit int, first character in the most significant byte (classic Mac OSType
// order):
//   å = 0xE5 (229), u = 0x75, t = 0x74, o = 0x6F  →  0xE575746F = -445287313.
// The top byte carries bit 31, so the value is negative on EVERY platform
// (sign is endian-independent) — and any negative dimension reads as AUTO.
// In declaration order (and in a big-endian dump) the bytes read "åuto"; on
// little-endian (ARM/x86) a raw dump shows them reversed ("otuå") — the same
// wart classic FourCCs carry on Intel. Integer comparison is endian-proof;
// only dump-reading is order-dependent. The packed byte is the å CODEPOINT
// (Latin-1 value 0xE5), not its two-byte UTF-8 form.
//
// AUTO is an owner-level protocol, not stored graphics state:
//  - int boundaries test EXACT equality (Size_isAuto) — the magic survives;
//  - float dimensions (GraphicsComponent x/y/w/h) test NEGATIVITY
//    (Size_isAutoF), because a float cannot hold the FourCC exactly;
//  - a GraphicsComponent holding AUTO dims resolves a ZERO abs (unresolved) —
//    the OWNER measures content and writes concrete sizes (a Label resolves
//    font size + padding; a ScrollPanel resolves AUTO content to the viewport).
// So call sites spell the FourCC (Label_setSize(l, SIZE_AUTO, SIZE_AUTO)) and
// owners remember the intent; the component itself never renders garbage from
// a negative extent.

// INTENTIONAL(vex): SIZE_AUTO is the FourCC "åuto" (0xE575746F) by design —
// negative on every platform so any negative dimension reads as AUTO.
// "åuto": E5 75 74 6F. The unsigned literal needs the cast (it exceeds INT32_MAX).
#define SIZE_AUTO_FOURCC 0xE575746Fu
#define SIZE_AUTO ((int32_t) SIZE_AUTO_FOURCC)
#define LENGTH_AUTO ((int32_t) SIZE_AUTO_FOURCC)

_Static_assert(SIZE_AUTO < 0, "SIZE_AUTO (åuto) must be negative");
_Static_assert(SIZE_AUTO == -445287313, "SIZE_AUTO (åuto) value");
_Static_assert(LENGTH_AUTO == SIZE_AUTO, "one sentinel, two names (size vs length)");

// Exact test for int boundaries (lengths, counts, int sizes).
static inline bool Size_isAuto(int32_t v) { return v == SIZE_AUTO; }

// Negativity test for float dimensions (a float cannot hold the FourCC exactly).
static inline bool Size_isAutoF(float v) { return v < 0.0f; }

#endif // LANG_SIZE_H
