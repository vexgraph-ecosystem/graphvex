#include "sync/command_buffer.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: CommandBuffer
 * ============================================================================
 * Single-threaded command recording handle capturing GPU render and compute operations.
 * Bound to a dedicated queue family (graphics, compute, copy) and strictly thread-confined
 * throughout its active recording lifecycle.
 *
 * In accordance with the Unified Graphics Abstraction Law, provides recording state
 * tracking and submission staging decoupled from underlying driver command pools,
 * allowing safe teardown and replay without driver deadlock.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: CommandBuffer (sync/command_buffer.c)
 * LEVEL: L2 — Behavior (CPU-side command record lifecycle stubs)
 * ============================================================================
 * SUMMARY:
 *   Per-thread command record handle. A CommandBuffer is owned by exactly one
 *   thread for its whole lifetime and must never be shared across threads.
 *   CPU-side stubs only: begin/end/reset/submit track the recording flag and
 *   touch no Vulkan/Metal/Direct backend.
 *
 * STRUCT FIELDS (Mirroring sync/command_buffer.h):
 * ----------------------------------------------------------------------------
 *   uint32_t queueFamily; // target queue family (COMMAND_FAMILY_* constant)
 *   bool recording;       // true between begin and end
 *   uint64_t typeId;      // reserved stub type stamp (0 until registered)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - CommandBuffer_0(void)                   : Construct default graphics command buffer
 *   - CommandBuffer_1(family)                 : Construct buffer for specific queue family
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - CommandBuffer_begin(self)               : Begin command recording
 *   - CommandBuffer_end(self)                 : Complete command recording
 *   - CommandBuffer_reset(self)               : Reset buffer recording state
 *   - CommandBuffer_submit(self, queue)       : Submit recorded buffer to command queue
 *   - CommandBuffer_free(self)                : Release command buffer memory
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - CommandBuffer_setFamily(self, family)   : Assign queue family affinity
 *   - CommandBuffer_setRecording(self, rec)   : Mutate recording status
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - CommandBuffer_getFamily(self)           : Query queue family affinity
 *   - CommandBuffer_getRecording(self)        : Query recording status
 *   - CommandBuffer_isRecording(self)         : Predicate for recording status
 *   - CommandBuffer_getTypeId(self)           : Query type identity stamp
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

CommandBuffer *CommandBuffer_0(void) {
    return CommandBuffer_1(COMMAND_FAMILY_GRAPHICS);
}

CommandBuffer *CommandBuffer_1(uint32_t family) {
    CommandBuffer *self = (CommandBuffer*) calloc(1, sizeof(CommandBuffer));
    if (!self)
        return nullptr;
    if (family > COMMAND_FAMILY_COPY)
        family = COMMAND_FAMILY_GRAPHICS;
    (*self).queueFamily = family;
    (*self).recording = false;
    (*self).typeId = 0;
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool CommandBuffer_begin(CommandBuffer *self) {
    if (!self)
        return false;
    if ((*self).recording)
        return false;
    (*self).recording = true;
    return true;
}

bool CommandBuffer_end(CommandBuffer *self) {
    if (!self)
        return false;
    if (!(*self).recording)
        return false;
    (*self).recording = false;
    return true;
}

bool CommandBuffer_reset(CommandBuffer *self) {
    if (!self)
        return false;
    (*self).recording = false;
    return true;
}

bool CommandBuffer_submit(CommandBuffer *self, struct CommandQueue *queue) {
    if (!self)
        return false;
    if ((*self).recording)
        return false;
    if (!queue)
        return false;
    return true;
}

void CommandBuffer_free(CommandBuffer *self) {
    if (!self)
        return;
    free(self);
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void CommandBuffer_setFamily(CommandBuffer *self, uint32_t family) {
    if (!self)
        return;
    if (family > COMMAND_FAMILY_COPY)
        family = COMMAND_FAMILY_GRAPHICS;
    (*self).queueFamily = family;
}

;;SETTER
void CommandBuffer_setRecording(CommandBuffer *self, bool recording) {
    if (!self)
        return;
    (*self).recording = recording;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t CommandBuffer_getFamily(const CommandBuffer *self) {
    if (!self)
        return COMMAND_FAMILY_GRAPHICS;
    return (*self).queueFamily;
}

;;GETTER
bool CommandBuffer_getRecording(const CommandBuffer *self) {
    if (!self)
        return false;
    return (*self).recording;
}

;;GETTER
bool CommandBuffer_isRecording(const CommandBuffer *self) {
    return CommandBuffer_getRecording(self);
}

;;GETTER
uint64_t CommandBuffer_getTypeId(const CommandBuffer *self) {
    if (!self)
        return 0;
    return (*self).typeId;
}
