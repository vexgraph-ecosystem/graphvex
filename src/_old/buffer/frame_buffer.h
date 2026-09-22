#include "c23/constructor.h"
#ifndef BUFFERS_FRAME_BUFFER_H
#define BUFFERS_FRAME_BUFFER_H

#include "buffer/buffer.h"

// buffers/frame_buffer.h — 4-channel composite frame buffer / render target.
// Ported from legacy buffers/FrameBuffer.java.

Buffer *FrameBuffer_2(size_t width, size_t height);


#define FrameBuffer(...) CONSTRUCTOR_DISPATCH(FrameBuffer, __VA_ARGS__)
#endif
