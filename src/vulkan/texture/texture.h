#ifndef ANTI_TEXTURE_H
#define ANTI_TEXTURE_H

#include <stdint.h>
#include <stdbool.h>

// Initialize the global texture registry (creates bindless descriptor sets)
bool Texture_initModule(void *instance, void *gpa, void *phys, void *device, void *queue, uint32_t queueFamily);

// Shut down the registry and free all VkImages and memory
void Texture_shutdown(void);

// Loads an image from the Virtual File System (VFS) via macOS CoreGraphics.
// Automatically transitions to SHADER_READ_ONLY_OPTIMAL and uploads to VRAM.
// Returns a bindless texture ID (>= 0) on success, or -1 on failure.
int32_t Texture_load(const char *vfsPath);

// True once Texture_initModule has provided a live Vulkan device.
// The font baker runs headless (no GPU) and uses this to skip uploads:
// atlas bytes + dictionaries bake fine on CPU, textures upload at runtime.
bool Texture_isReady(void);

// Gets the global Vulkan Descriptor Set that contains the bindless texture array
void *Texture_getDescriptorSet(void);

// Gets the descriptor set layout used for the bindless array
void *Texture_getDescriptorSetLayout(void);

// Returns the pixel dimensions of a loaded texture by ID. Returns false if ID is invalid.
bool Texture_getSize(int32_t id, uint32_t *outW, uint32_t *outH);

// Returns the number of bound bindless textures: valid IDs are [0, maxBoundId).
// Draw seams clamp shader texId against this so OOB -1 / exhausted ids never
// reach the bindless array and fault the GPU (Rule 39 hot-minimal guard).
int32_t Texture_maxBoundId(void);

#endif // ANTI_TEXTURE_H

// Loads a texture directly from raw RGBA8 data in memory.
// Returns a bindless texture ID (>= 0) on success, or -1 on failure.
int32_t Texture_loadRaw(const void *rgbaData, uint32_t width, uint32_t height);

// Updates a sub-region of an existing texture from raw RGBA8 data in memory.
// Useful for dynamic atlases.
bool Texture_updateSubRaw(int32_t id, const void *rgbaData, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Replaces or reallocates the raw RGBA8 data of an existing bindless texture ID.
// If dimensions match, it updates the existing texture in-place.
// If dimensions change, it destroys the old VkImage and allocates a new one at the same slot.
int32_t Texture_replaceRaw(int32_t id, const void *rgbaData, uint32_t width, uint32_t height);

// Frees the GPU resources for a bindless texture ID.
void Texture_free(int32_t id);
