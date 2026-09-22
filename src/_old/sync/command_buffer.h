#ifndef SYNC_COMMAND_BUFFER_H
#define SYNC_COMMAND_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"

// sync/command_buffer.h — CPU-side per-thread command record handle (stub).
//
// A CommandBuffer is owned by exactly one thread for its whole lifetime and
// must never be shared across threads. All lifecycle functions are CPU-side
// stubs: no Vulkan/Metal/Direct backend is touched here.

#ifndef COMMAND_FAMILY_GRAPHICS
#define COMMAND_FAMILY_GRAPHICS 0u
#endif
#ifndef COMMAND_FAMILY_COMPUTE
#define COMMAND_FAMILY_COMPUTE 1u
#endif
#ifndef COMMAND_FAMILY_COPY
#define COMMAND_FAMILY_COPY 2u
#endif

typedef struct CommandBuffer {
    uint32_t queueFamily;
    bool recording;
    uint64_t typeId;
} CommandBuffer;

// Submit target, defined in sync/command_queue.h. Forward-declared (never
// included) to keep one class per file pair with zero cross-includes.
struct CommandQueue;

CommandBuffer *CommandBuffer_0(void);
CommandBuffer *CommandBuffer_1(uint32_t family);

bool CommandBuffer_begin(CommandBuffer *self);
bool CommandBuffer_end(CommandBuffer *self);
bool CommandBuffer_reset(CommandBuffer *self);
bool CommandBuffer_submit(CommandBuffer *self, struct CommandQueue *queue);

void CommandBuffer_free(CommandBuffer *self);

void CommandBuffer_setFamily(CommandBuffer *self, uint32_t family);
uint32_t CommandBuffer_getFamily(const CommandBuffer *self);
void CommandBuffer_setRecording(CommandBuffer *self, bool recording);
bool CommandBuffer_getRecording(const CommandBuffer *self);
bool CommandBuffer_isRecording(const CommandBuffer *self);
uint64_t CommandBuffer_getTypeId(const CommandBuffer *self);

#define CommandBuffer(...) CONSTRUCTOR_DISPATCH(CommandBuffer, __VA_ARGS__)
#endif
