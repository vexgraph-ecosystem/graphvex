#include "c23/constructor.h"
#ifndef BUFFERS_SHADOW_BUFFER_H
#define BUFFERS_SHADOW_BUFFER_H

#include "buffer/buffer.h"

// buffers/shadow_buffer.h — 1-channel shadow depth cascade buffer.
// Ported from legacy buffers/ShadowBuffer.java.

Buffer *ShadowBuffer_2(size_t width, size_t height);


#define ShadowBuffer(...) CONSTRUCTOR_DISPATCH(ShadowBuffer, __VA_ARGS__)
#endif
