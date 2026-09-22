#ifndef SYNC_COMMAND_QUEUE_H
#define SYNC_COMMAND_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"

// sync/command_queue.h — CPU-side queue-family handle (stub).
//
// A CommandQueue names one GRAPHICS/COMPUTE/COPY family. All lifecycle
// functions are CPU-side stubs: no Vulkan/Metal/Direct backend is touched
// here.

#ifndef COMMAND_FAMILY_GRAPHICS
#define COMMAND_FAMILY_GRAPHICS 0u
#endif
#ifndef COMMAND_FAMILY_COMPUTE
#define COMMAND_FAMILY_COMPUTE 1u
#endif
#ifndef COMMAND_FAMILY_COPY
#define COMMAND_FAMILY_COPY 2u
#endif

typedef struct CommandQueue {
    uint32_t family;
    uint64_t typeId;
} CommandQueue;

// Submitted record, defined in sync/command_buffer.h. Forward-declared
// (never included) to keep one class per file pair with zero cross-includes.
struct CommandBuffer;

CommandQueue *CommandQueue_0(void);
CommandQueue *CommandQueue_1(uint32_t family);

bool CommandQueue_begin(CommandQueue *self);
bool CommandQueue_end(CommandQueue *self);
bool CommandQueue_reset(CommandQueue *self);
bool CommandQueue_submit(CommandQueue *self, const struct CommandBuffer *cmd);

void CommandQueue_free(CommandQueue *self);

void CommandQueue_setFamily(CommandQueue *self, uint32_t family);
uint32_t CommandQueue_getFamily(const CommandQueue *self);
uint64_t CommandQueue_getTypeId(const CommandQueue *self);

#define CommandQueue(...) CONSTRUCTOR_DISPATCH(CommandQueue, __VA_ARGS__)
#endif
