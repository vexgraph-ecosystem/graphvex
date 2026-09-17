#include "texture.h"
#include "io/vfs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system/image_mac.h"

#include <vulkan/vulkan.h>
#include "vulkan/vk_guard.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Texture (vulkan/texture/texture.c — bindless 1024-slot registry)
 * LEVEL: L4 — Self-Management (owns GPU images/views/samplers + retire ring)
 * ============================================================================
 * Procedural bindless registry: slot id -> (image, memory, view, sampler,
 * width, height). Uploads stage through a host-visible buffer, transition
 * with a one-shot CB from the module-owned transient pool, submit with a
 * per-upload fence, and wait at most 100ms (the Bounded Wait Law) — never DeviceWaitIdle
 * or QueueWaitIdle between another CB Begin/End on the shared queue.
 *
 * Replace policy: same-size replace is a fast in-place sub-update
 * (Texture_updateSubRaw only, zero destroy). A resize retires the old
 * (image, view, sampler, memory) onto the growable retire ring and points
 * the slot at the fresh handles + UpdateDescriptorSets immediately; the
 * old handles die only after their upload fence signals, or the retire
 * guard certifies no bindless-sampling Submit is in flight, or — for
 * standalone builds with no guard — 2 drained frames pass. A submitted
 * batch/pane CB that still samples the old slot must never meet a
 * FreeMemory under it (the Ecosystem Vulkan Safety Nets Law net: GPU page-fault on freed memory).
 *
 * SLOT RECORD (retired row — behaviorless, owned by the retire ring):
 * ----------------------------------------------------------------------------
 *   VkImage s_retireImage[i];         // retired image awaiting safe destroy
 *   VkDeviceMemory s_retireMemory[i]; // retired backing memory
 *   VkImageView s_retireView[i];      // retired view
 *   VkSampler s_retireSampler[i];     // retired sampler
 *   VkFence s_retireFence[i];         // upload fence proving GPU drain (or null)
 *   uint64_t s_retireSeq[i];          // frame seq at retire (2-frame fallback)
 * Ring: six parallel rows over [0, s_retireCap), starting at RETIRE_INIT 8
 * and doubling on demand (the Dynamic Scalability & Anti-Hardcoding Law);
 * s_frameSeq ticks per resize/free. Reap is non-blocking (GetFenceStatus
 * poll + the retire guard callback); OOM on growth fails the replace loudly
 * (return -1, old content kept, retry next tick) — never an unbounded wait,
 * never a leak.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Texture_initModule(instance, gpa, phys, device, queue, queueFamily)
 *   - Texture_shutdown(void)            : bounded-drain retire ring, then
 *                                         live slots, pool, descriptors (the Teardown Order Law)
 *   - Texture_load(vfsPath)
 *   - Texture_loadRaw(rgbaData, width, height)
 *   - Texture_updateSubRaw(id, rgbaData, x, y, width, height)
 *   - Texture_replaceRaw(id, rgbaData, width, height)
 *                                         : fast path (same-size -> sub-update);
 *                                           retire path (resize -> new handles
 *                                           + ring the old ones)
 *   - Texture_free(id)                  : ring the slot, do not destroy inline
 *     (the Ecosystem Vulkan Safety Nets Law net: load/loadRaw/updateSubRaw/replaceRaw guard the driver
 *      at entry; submit seams pass the live queue per VkGuard contract)
 *
 * Getters:
 *   - Texture_isReady(void)
 *   - Texture_getDescriptorSet(void)
 *   - Texture_getDescriptorSetLayout(void)
 *   - Texture_getSize(id, outW, outH)
 *   - Texture_maxBoundId(void)       — the Ecosystem Vulkan Safety Nets Law: ceiling for draw-site texId clamps
 *   - Texture_retireDepth(void)      — live retire-ring occupancy [0, 8]
 *   - Texture_retireCapacity(void)   — RETIRE_MAX 8 (bounded, the Bounded Wait Law)
 *   - Texture_frameSeq(void)         — retire frame clock (2-frame reap proof)
 *
 * Setters:
 *   - Texture_setRetireGuard(guard)  — bind the sampler-flight destroy probe
 *     (the Conflict Triage Law downward callback; registered by the compositor, null in
 *     standalone/headless builds -> CPU-lag fallback)
 * Slot mutation still flows only through load/replace/free core verbs.
 * Cold validation (the Cold-Strict, Hot-Minimal Validation Law): null data, zero width/height, id OOB, and
 * width*height*4 overflow reject loudly once at entry (return -1/false);
 * hot upload paths carry the nullptr entry guard only, zero per-texel work.
 * ============================================================================
 */


#define MAX_BINDLESS_TEXTURES 1024

// Vulkan Core State
static VkInstance s_instance;
static PFN_vkGetInstanceProcAddr s_gpa;
static VkPhysicalDevice s_phys;
static VkDevice s_device;
static VkQueue s_queue;
static uint32_t s_queueFamily;

// Bindless Registry Arrays
static VkImage s_images[MAX_BINDLESS_TEXTURES];
static VkDeviceMemory s_memories[MAX_BINDLESS_TEXTURES];
static VkImageView s_views[MAX_BINDLESS_TEXTURES];
static VkSampler s_samplers[MAX_BINDLESS_TEXTURES];
static uint32_t s_widths[MAX_BINDLESS_TEXTURES];
static uint32_t s_heights[MAX_BINDLESS_TEXTURES];
static int s_textureCount = 0;

static VkDescriptorPool s_descPool;
static VkDescriptorSetLayout s_descLayout;
static VkDescriptorSet s_bindlessSet;
static VkCommandPool s_cmdPool;

// Growable retire ring (SLOT RECORD rows): a resized/freed slot's old
// (image, memory, view, sampler) waits here until the GPU provably drains
// it — upload-fence signal OR 2 retired frames — instead of dying under a
// DeviceWaitIdle between another CB Begin/End on the shared queue. The ring
// starts at TEXTURE_RETIRE_INIT rows and doubles on demand (the Dynamic
// Scalability & Anti-Hardcoding Law) — a burst of resizes can never wedge
// on a full ring.
#define TEXTURE_RETIRE_INIT 8
#define TEXTURE_RETIRE_FRAME_LAG 2
#define TEXTURE_FENCE_WAIT_NS 100000000ULL
static VkImage *s_retireImage = NULL;
static VkDeviceMemory *s_retireMemory = NULL;
static VkImageView *s_retireView = NULL;
static VkSampler *s_retireSampler = NULL;
static VkFence *s_retireFence = NULL;
static uint64_t *s_retireSeq = NULL;
static int s_retireCount = 0;
static int s_retireCap = 0;
static uint64_t s_frameSeq = 0;
// Sampler-flight destroy probe (the Conflict Triage Law callback seam, registered by the
// darling compositor): null = legacy standalone mode (CPU-lag fallback).
static bool (*s_retireGuard)(void) = nullptr;

#define VK_LOAD(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = (PFN_vk##name)s_gpa(s_instance, "vk" #name);

static uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VK_LOAD(GetPhysicalDeviceMemoryProperties)
    VkPhysicalDeviceMemoryProperties memProperties;
    GetPhysicalDeviceMemoryProperties_fn(s_phys, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

// CORE HELPERS (retire ring + bounded upload submit; file-local, no API)

// Cold validator (the Cold-Strict, Hot-Minimal Validation Law): null/zero/overflow reject once at entry.
// Dest-last: pixel byte count lands in outBytes (may be nullptr).
static bool textureBytesOk(const void *rgbaData, uint32_t width, uint32_t height, size_t *outBytes) {
    if (rgbaData == nullptr)
        return false;
    if (width == 0 || height == 0)
        return false;
    size_t pixels = (size_t) width * (size_t) height;
    if (pixels > (SIZE_MAX / 4u))
        return false;
    if (outBytes)
        *outBytes = pixels * 4u;
    return true;
}

// Grow the six parallel retire rows so a burst of resizes never wedges on a
// full ring (the Dynamic Scalability & Anti-Hardcoding Law). The cap commits
// only after every array grew; a partial grow is harmless because reads are
// bounded by s_retireCap. OOM leaves depth/cap untouched: the caller keeps
// old content live and retries next tick (drop-degrade per the Cold-Strict,
// Hot-Minimal Validation Law). New tails zero to VK_NULL_HANDLE so the
// free-slot scan recognizes them.
static bool retireGrow(void) {
    if (s_retireCount < s_retireCap)
        return true;
    int newCap = (s_retireCap == 0) ? TEXTURE_RETIRE_INIT : s_retireCap * 2;
    size_t newBytes = (size_t) newCap;
    VkImage *ni = (VkImage*) realloc(s_retireImage, newBytes * sizeof(VkImage));
    if (!ni)
        return false;
    s_retireImage = ni;
    VkDeviceMemory *nm = (VkDeviceMemory*) realloc(s_retireMemory, newBytes * sizeof(VkDeviceMemory));
    if (!nm)
        return false;
    s_retireMemory = nm;
    VkImageView *nv = (VkImageView*) realloc(s_retireView, newBytes * sizeof(VkImageView));
    if (!nv)
        return false;
    s_retireView = nv;
    VkSampler *ns = (VkSampler*) realloc(s_retireSampler, newBytes * sizeof(VkSampler));
    if (!ns)
        return false;
    s_retireSampler = ns;
    VkFence *nf = (VkFence*) realloc(s_retireFence, newBytes * sizeof(VkFence));
    if (!nf)
        return false;
    s_retireFence = nf;
    uint64_t *nq = (uint64_t*) realloc(s_retireSeq, newBytes * sizeof(uint64_t));
    if (!nq)
        return false;
    s_retireSeq = nq;
    memset(s_retireImage + s_retireCap, 0, (size_t) (newCap - s_retireCap) * sizeof(VkImage));
    memset(s_retireMemory + s_retireCap, 0, (size_t) (newCap - s_retireCap) * sizeof(VkDeviceMemory));
    memset(s_retireView + s_retireCap, 0, (size_t) (newCap - s_retireCap) * sizeof(VkImageView));
    memset(s_retireSampler + s_retireCap, 0, (size_t) (newCap - s_retireCap) * sizeof(VkSampler));
    memset(s_retireFence + s_retireCap, 0, (size_t) (newCap - s_retireCap) * sizeof(VkFence));
    memset(s_retireSeq + s_retireCap, 0, (size_t) (newCap - s_retireCap) * sizeof(uint64_t));
    s_retireCap = newCap;
    return true;
}

// Destroy one retired row (fence included). Caller proves drain first.
static void retireDestroySlot(VkDevice dev, int slot) {    VK_LOAD(DestroySampler)
    VK_LOAD(DestroyImageView)
    VK_LOAD(DestroyImage)
    VK_LOAD(FreeMemory)
    VK_LOAD(DestroyFence)
    if (s_retireSampler[slot] != VK_NULL_HANDLE && DestroySampler_fn)
        DestroySampler_fn(dev, s_retireSampler[slot], nullptr);
    if (s_retireView[slot] != VK_NULL_HANDLE && DestroyImageView_fn)
        DestroyImageView_fn(dev, s_retireView[slot], nullptr);
    if (s_retireImage[slot] != VK_NULL_HANDLE && DestroyImage_fn)
        DestroyImage_fn(dev, s_retireImage[slot], nullptr);
    if (s_retireMemory[slot] != VK_NULL_HANDLE && FreeMemory_fn)
        FreeMemory_fn(dev, s_retireMemory[slot], nullptr);
    if (s_retireFence[slot] != VK_NULL_HANDLE && DestroyFence_fn)
        DestroyFence_fn(dev, s_retireFence[slot], nullptr);
    s_retireImage[slot] = VK_NULL_HANDLE;
    s_retireMemory[slot] = VK_NULL_HANDLE;
    s_retireView[slot] = VK_NULL_HANDLE;
    s_retireSampler[slot] = VK_NULL_HANDLE;
    s_retireFence[slot] = VK_NULL_HANDLE;
    s_retireSeq[slot] = 0;
}

// Non-blocking reap: signaled upload fence, OR the registered retire guard
// certifying no bindless-sampling Submit is in flight, OR (standalone builds
// without a guard) 2 retired frames. Never waits — a still-flying row stays
// ringed for a later tick.
static void retireDrain(VkDevice dev) {
    if (dev == VK_NULL_HANDLE)
        return;
    VK_LOAD(GetFenceStatus)
    int live = 0;
    for (int i = 0; i < s_retireCap; i++) {
        if (s_retireImage[i] == VK_NULL_HANDLE)
            continue;
        bool drainable = false;
        if (s_retireFence[i] != VK_NULL_HANDLE && GetFenceStatus_fn) {
            if (GetFenceStatus_fn(dev, s_retireFence[i]) == VK_SUCCESS)
                drainable = true;
        }
        // Fence-less rows (free / resize rollovers) are sampled by submitted
        // batch + pane CBs, NOT proven by the upload fence. When a retire
        // guard is registered it is the ONLY destroy proof for them: it tells
        // us no bindless-sampling Submit is in flight, so FreeMemory under a
        // flying CB (the kIOGPUCommandBufferCallbackErrorPageFault defect) can
        // never happen. The 2-frame CPU lag survives solely as the standalone
        // / headless fallback — a registered guard is authoritative.
        if (!drainable && s_retireGuard) {
            if (s_retireGuard())
                drainable = true;
        }
        if (!drainable && s_retireGuard == nullptr && s_frameSeq >= s_retireSeq[i] + TEXTURE_RETIRE_FRAME_LAG)
            drainable = true;
        if (!drainable) {
            live++;
            continue;
        }
        retireDestroySlot(dev, i);
    }
    s_retireCount = live;
}

// Ring one retired row. False when the ring is full with no drainable slot:
// the caller keeps old content live and fails loudly (retry next tick).
static bool retirePush(VkImage image, VkDeviceMemory memory, VkImageView view, VkSampler sampler, VkFence fence) {
    VkDevice dev = s_device;
    if (dev == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return false;
    retireDrain(dev);
    if (s_retireCount >= s_retireCap && !retireGrow())
        return false;
    int slot = -1;
    for (int i = 0; i < s_retireCap; i++) {
        if (s_retireImage[i] == VK_NULL_HANDLE) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return false;
    s_frameSeq++;
    s_retireImage[slot] = image;
    s_retireMemory[slot] = memory;
    s_retireView[slot] = view;
    s_retireSampler[slot] = sampler;
    s_retireFence[slot] = fence;
    s_retireSeq[slot] = s_frameSeq;
    s_retireCount++;
    return true;
}

// Bounded upload tail (the Bounded Wait Law): per-upload fence, two 100ms waits max,
// throttled log, drop-degrade false. Never QueueWaitIdle/DEVICE_WAIT_IDLE.
// Owns the whole submit-or-fail tail: on success the caller tears down its
// CB + staging normally; on timeout the CB + staging are still flying, so
// this leaks them once (reclaimed at pool destroy) rather than use-after-
// free the GPU read, rings the fresh image (VK_NULL_HANDLE when the upload
// targets a live slot, as in updateSubRaw) for 2-frame reap, and returns
// false. Timeouts fire only on a dead drawable; healthy hitches never trip
// 200ms. No UINT64_MAX anywhere on this path.
static bool submitUploadTail(VkDevice dev, VkQueue queue, VkCommandBuffer cb, VkBuffer staging, VkDeviceMemory stagingMem, VkImage freshImage, VkDeviceMemory freshMem) {
    (void) staging;
    (void) stagingMem;
    VK_LOAD(CreateFence)
    VK_LOAD(DestroyFence)
    VK_LOAD(QueueSubmit)
    VK_LOAD(WaitForFences)
    if (!CreateFence_fn || !DestroyFence_fn || !QueueSubmit_fn || !WaitForFences_fn)
        return false;
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence = VK_NULL_HANDLE;
    if (CreateFence_fn(dev, &fi, nullptr, &fence) != VK_SUCCESS)
        return false;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    bool ok = false;
    if (QueueSubmit_fn(queue, 1, &si, fence) == VK_SUCCESS) {
        if (WaitForFences_fn(dev, 1, &fence, VK_TRUE, TEXTURE_FENCE_WAIT_NS) == VK_SUCCESS)
            ok = true;
        else if (WaitForFences_fn(dev, 1, &fence, VK_TRUE, TEXTURE_FENCE_WAIT_NS) == VK_SUCCESS)
            ok = true;
        else {
            static uint32_t s_timeoutStrikes = 0;
            s_timeoutStrikes++;
            if ((s_timeoutStrikes % 64u) == 1u) {
                fprintf(stderr, "[Texture] upload fence wait TIMEOUT (strike %u): drop-degrade, retry next tick\n", s_timeoutStrikes);
                fflush(stderr);
            }
        }
    }
    // Fence destroy is legal even when the submit is still flying (fence
    // lifetime is independent); the CB/buffers are the caller's problem —
    // see the leak-once contract above.
    DestroyFence_fn(dev, fence, nullptr);
    if (!ok && freshImage != VK_NULL_HANDLE) {
        // The fresh image never reached a slot: ring it for 2-frame reap so
        // the timed-out upload leaks nothing but its CB + staging pair.
        retirePush(freshImage, freshMem, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);
    }
    return ok;
}

bool Texture_isReady(void) {
    return s_device != VK_NULL_HANDLE && s_instance != VK_NULL_HANDLE && s_gpa != nullptr;
}

bool Texture_initModule(void *instance, void *gpa, void *phys, void *device, void *queue, uint32_t queueFamily) {
    s_instance = (VkInstance) instance;
    s_gpa = (PFN_vkGetInstanceProcAddr) gpa;
    s_phys = (VkPhysicalDevice) phys;
    s_device = (VkDevice) device;
    s_queue = (VkQueue) queue;
    s_queueFamily = queueFamily;
    s_retireCount = 0;
    s_frameSeq = 0;

    VK_LOAD(CreateDescriptorSetLayout)
    VK_LOAD(CreateDescriptorPool)
    VK_LOAD(AllocateDescriptorSets)
    VK_LOAD(CreateCommandPool)

    // 1. Create Descriptor Set Layout (Bindless)
    VkDescriptorSetLayoutBinding binding = {0};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = MAX_BINDLESS_TEXTURES;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorBindingFlags bindlessFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo layoutFlags = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    layoutFlags.bindingCount = 1;
    layoutFlags.pBindingFlags = &bindlessFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.pNext = &layoutFlags;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    
    if (CreateDescriptorSetLayout_fn(s_device, &layoutInfo, nullptr, &s_descLayout) != VK_SUCCESS) {
        printf("Failed to create bindless descriptor set layout\n");
        return false;
    }

    // 2. Create Descriptor Pool
    VkDescriptorPoolSize poolSize = {0};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_BINDLESS_TEXTURES;

    VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (CreateDescriptorPool_fn(s_device, &poolInfo, nullptr, &s_descPool) != VK_SUCCESS) {
        printf("Failed to create bindless descriptor pool\n");
        return false;
    }

    // 3. Allocate the single giant bindless set
    uint32_t maxDescCount = MAX_BINDLESS_TEXTURES;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableAllocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
    variableAllocInfo.descriptorSetCount = 1;
    variableAllocInfo.pDescriptorCounts = &maxDescCount;

    VkDescriptorSetAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.pNext = &variableAllocInfo;
    allocInfo.descriptorPool = s_descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &s_descLayout;

    if (AllocateDescriptorSets_fn(s_device, &allocInfo, &s_bindlessSet) != VK_SUCCESS) {
        printf("Failed to allocate bindless descriptor set\n");
        return false;
    }

    // 4. Create Command Pool for transient transfers
    VkCommandPoolCreateInfo cpInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpInfo.queueFamilyIndex = s_queueFamily;
    
    if (CreateCommandPool_fn(s_device, &cpInfo, nullptr, &s_cmdPool) != VK_SUCCESS) {
        printf("Failed to create texture transfer command pool\n");
        return false;
    }

    return true;
}

void Texture_shutdown(void) {
    if (!Texture_isReady() || !s_gpa || s_instance == VK_NULL_HANDLE) {
        s_device = VK_NULL_HANDLE;
        s_instance = VK_NULL_HANDLE;
        s_phys = VK_NULL_HANDLE;
        s_queue = VK_NULL_HANDLE;
        s_gpa = nullptr;
        s_textureCount = 0;
        s_retireCount = 0;
        s_frameSeq = 0;
        return;
    }
    VK_LOAD(DestroySampler)
    VK_LOAD(DestroyImageView)
    VK_LOAD(DestroyImage)
    VK_LOAD(FreeMemory)
    VK_LOAD(DestroyDescriptorPool)
    VK_LOAD(DestroyDescriptorSetLayout)
    VK_LOAD(DestroyCommandPool)
    VK_LOAD(WaitForFences)

    // the Teardown Order Law teardown, the Bounded Wait Law bound: the retire ring dies FIRST — one
    // bounded 100ms wait per fence-carrying row, then force-destroy whatever
    // remains (shutdown is the last resort; the device goes away next).
    // Live slots, pool, and descriptors follow in dependency order.
    for (int i = 0; i < s_retireCap; i++) {
        if (s_retireImage[i] == VK_NULL_HANDLE)
            continue;
        if (s_retireFence[i] != VK_NULL_HANDLE && WaitForFences_fn)
            WaitForFences_fn(s_device, 1, &s_retireFence[i], VK_TRUE, TEXTURE_FENCE_WAIT_NS);
        retireDestroySlot(s_device, i);
    }
    s_retireCount = 0;
    s_frameSeq = 0;
    free(s_retireImage);
    free(s_retireMemory);
    free(s_retireView);
    free(s_retireSampler);
    free(s_retireFence);
    free(s_retireSeq);
    s_retireImage = NULL;
    s_retireMemory = NULL;
    s_retireView = NULL;
    s_retireSampler = NULL;
    s_retireFence = NULL;
    s_retireSeq = NULL;
    s_retireCap = 0;

    for (int i = 0; i < s_textureCount; i++) {
        if (s_samplers[i] != VK_NULL_HANDLE && DestroySampler_fn) {
            DestroySampler_fn(s_device, s_samplers[i], nullptr);
            s_samplers[i] = VK_NULL_HANDLE;
        }
        if (s_views[i] != VK_NULL_HANDLE && DestroyImageView_fn) {
            DestroyImageView_fn(s_device, s_views[i], nullptr);
            s_views[i] = VK_NULL_HANDLE;
        }
        if (s_images[i] != VK_NULL_HANDLE && DestroyImage_fn) {
            DestroyImage_fn(s_device, s_images[i], nullptr);
            s_images[i] = VK_NULL_HANDLE;
        }
        if (s_memories[i] != VK_NULL_HANDLE && FreeMemory_fn) {
            FreeMemory_fn(s_device, s_memories[i], nullptr);
            s_memories[i] = VK_NULL_HANDLE;
        }
        s_widths[i] = 0;
        s_heights[i] = 0;
    }

    if (s_cmdPool != VK_NULL_HANDLE && DestroyCommandPool_fn) {
        DestroyCommandPool_fn(s_device, s_cmdPool, nullptr);
        s_cmdPool = VK_NULL_HANDLE;
    }
    if (s_descPool != VK_NULL_HANDLE && DestroyDescriptorPool_fn) {
        DestroyDescriptorPool_fn(s_device, s_descPool, nullptr);
        s_descPool = VK_NULL_HANDLE;
    }
    if (s_descLayout != VK_NULL_HANDLE && DestroyDescriptorSetLayout_fn) {
        DestroyDescriptorSetLayout_fn(s_device, s_descLayout, nullptr);
        s_descLayout = VK_NULL_HANDLE;
    }

    s_bindlessSet = VK_NULL_HANDLE;
    s_textureCount = 0;
    s_retireCount = 0;
    s_frameSeq = 0;

    s_device = VK_NULL_HANDLE;
    s_instance = VK_NULL_HANDLE;
    s_phys = VK_NULL_HANDLE;
    s_queue = VK_NULL_HANDLE;
    s_gpa = nullptr;
}

int32_t Texture_load(const char *vfsPath) {
    if (!VkGuard_check("Texture_load", s_device, s_queue, false))
        return -1;
    if (!Texture_isReady() || !s_gpa) {
        fprintf(stderr, "[Texture] Texture module not initialized yet!\n");
        return -1;
    }
    if (vfsPath == nullptr)
        return -1;
    if (s_textureCount >= MAX_BINDLESS_TEXTURES) {
        printf("Texture limit reached!\n");
        return -1;
    }

    char resolvedPath[1024];
    const char *actualPath = vfsPath;
    if (Vfs_resolve(vfsPath, resolvedPath, sizeof(resolvedPath))) {
        actualPath = resolvedPath;
    }

    FILE *f = fopen(actualPath, "rb");
    if (!f) {
        printf("Failed to read texture file: %s (resolved: %s)\n", vfsPath, actualPath);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *fileData = malloc(fileSize);
    if (!fileData) { fclose(f); return -1; }
    fread(fileData, 1, fileSize, f);
    fclose(f);

    // Decode using platform-specific image decoder
    size_t width = 0, height = 0;
    void *rgbaData = ImageMac_decode(fileData, fileSize, &width, &height);
    free(fileData);

    if (!rgbaData) {
        printf("Failed to decode image: %s\n", vfsPath);
        return -1;
    }

    size_t imageSize = 0;
    if (width == 0 || height == 0 || width > (SIZE_MAX / 4u) / height) {
        free(rgbaData);
        return -1;
    }
    imageSize = width * height * 4;

    VK_LOAD(CreateBuffer)
    VK_LOAD(GetBufferMemoryRequirements)
    VK_LOAD(AllocateMemory)
    VK_LOAD(BindBufferMemory)
    VK_LOAD(MapMemory)
    VK_LOAD(UnmapMemory)
    VK_LOAD(DestroyBuffer)
    VK_LOAD(FreeMemory)
    
    // 1. Create Staging Buffer
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (CreateBuffer_fn(s_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        free(rgbaData); return -1;
    }

    VkMemoryRequirements memReqs;
    GetBufferMemoryRequirements_fn(s_device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (allocInfo.memoryTypeIndex == UINT32_MAX || AllocateMemory_fn(s_device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        free(rgbaData); return -1;
    }
    BindBufferMemory_fn(s_device, stagingBuffer, stagingBufferMemory, 0);

    void *data = nullptr;
    if (MapMemory_fn(s_device, stagingBufferMemory, 0, imageSize, 0, &data) != VK_SUCCESS || !data) {
        free(rgbaData); return -1;
    }
    memcpy(data, rgbaData, imageSize);
    UnmapMemory_fn(s_device, stagingBufferMemory);
    free(rgbaData);

    // 2. Create VkImage
    VK_LOAD(CreateImage)
    VK_LOAD(GetImageMemoryRequirements)
    VK_LOAD(BindImageMemory)

    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)width;
    imageInfo.extent.height = (uint32_t)height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // CoreGraphics output is SRGB generally
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    if (CreateImage_fn(s_device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS) {
        return -1;
    }

    GetImageMemoryRequirements_fn(s_device, textureImage, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocInfo.memoryTypeIndex == UINT32_MAX || AllocateMemory_fn(s_device, &allocInfo, nullptr, &textureImageMemory) != VK_SUCCESS) {
        return -1;
    }
    BindImageMemory_fn(s_device, textureImage, textureImageMemory, 0);

    // 3. Command Buffer for layout transitions and copy
    VK_LOAD(AllocateCommandBuffers)
    VK_LOAD(BeginCommandBuffer)
    VK_LOAD(EndCommandBuffer)
    VK_LOAD(CmdPipelineBarrier)
    VK_LOAD(CmdCopyBufferToImage)
    VK_LOAD(FreeCommandBuffers)

    VkCommandBufferAllocateInfo cmdAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = s_cmdPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    AllocateCommandBuffers_fn(s_device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BeginCommandBuffer_fn(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;

    CmdCopyBufferToImage_fn(commandBuffer, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    EndCommandBuffer_fn(commandBuffer);

    if (!submitUploadTail(s_device, s_queue, commandBuffer, stagingBuffer, stagingBufferMemory, textureImage, textureImageMemory))
        return -1;
    FreeCommandBuffers_fn(s_device, s_cmdPool, 1, &commandBuffer);

    DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
    FreeMemory_fn(s_device, stagingBufferMemory, nullptr);

    // 4. Create ImageView & Sampler
    VK_LOAD(CreateImageView)
    VK_LOAD(CreateSampler)

    VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView textureImageView;
    if (CreateImageView_fn(s_device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        return -1;
    }

    VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler textureSampler;
    if (CreateSampler_fn(s_device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        return -1;
    }

    // 5. Update Bindless Descriptor Set
    VK_LOAD(UpdateDescriptorSets)
    VkDescriptorImageInfo descImageInfo = {0};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = textureImageView;
    descImageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrite = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    descriptorWrite.dstSet = s_bindlessSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = s_textureCount;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    UpdateDescriptorSets_fn(s_device, 1, &descriptorWrite, 0, nullptr);

    // 6. Record to Registry
    int32_t id = s_textureCount;
    s_images[id] = textureImage;
    s_memories[id] = textureImageMemory;
    s_views[id] = textureImageView;
    s_samplers[id] = textureSampler;
    s_widths[id]  = (uint32_t)width;
    s_heights[id] = (uint32_t)height;
    
    s_textureCount++;
    printf("Successfully bound texture %s to ID %d\n", vfsPath, id);
    return id;
}


int32_t Texture_loadRaw(const void *rgbaData, uint32_t width, uint32_t height) {
    if (!VkGuard_check("Texture_loadRaw", s_device, s_queue, false))
        return -1;
    if (!Texture_isReady() || !s_gpa) {
        return -1;
    }
    if (s_textureCount >= MAX_BINDLESS_TEXTURES) {
        printf("Texture limit reached!\n");
        return -1;
    }

    size_t imageSize = 0;
    if (!textureBytesOk(rgbaData, width, height, &imageSize))
        return -1;

    VK_LOAD(CreateBuffer)
    VK_LOAD(GetBufferMemoryRequirements)
    VK_LOAD(AllocateMemory)
    VK_LOAD(BindBufferMemory)
    VK_LOAD(MapMemory)
    VK_LOAD(UnmapMemory)
    VK_LOAD(DestroyBuffer)
    VK_LOAD(FreeMemory)

    // 1. Create Staging Buffer
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (CreateBuffer_fn(s_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return -1;
    }

    VkMemoryRequirements memReqs;
    GetBufferMemoryRequirements_fn(s_device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (AllocateMemory_fn(s_device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        return -1;
    }
    BindBufferMemory_fn(s_device, stagingBuffer, stagingBufferMemory, 0);

    void *data;
    MapMemory_fn(s_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, rgbaData, imageSize);
    UnmapMemory_fn(s_device, stagingBufferMemory);

    // 2. Create VkImage
    VK_LOAD(CreateImage)
    VK_LOAD(GetImageMemoryRequirements)
    VK_LOAD(BindImageMemory)

    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)width;
    imageInfo.extent.height = (uint32_t)height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    if (CreateImage_fn(s_device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS) {
        return -1;
    }

    GetImageMemoryRequirements_fn(s_device, textureImage, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (AllocateMemory_fn(s_device, &allocInfo, nullptr, &textureImageMemory) != VK_SUCCESS) {
        return -1;
    }
    BindImageMemory_fn(s_device, textureImage, textureImageMemory, 0);

    // 3. Command Buffer for layout transitions and copy
    VK_LOAD(AllocateCommandBuffers)
    VK_LOAD(BeginCommandBuffer)
    VK_LOAD(EndCommandBuffer)
    VK_LOAD(CmdPipelineBarrier)
    VK_LOAD(CmdCopyBufferToImage)
    VK_LOAD(FreeCommandBuffers)

    VkCommandBufferAllocateInfo cmdAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = s_cmdPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    AllocateCommandBuffers_fn(s_device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BeginCommandBuffer_fn(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;

    CmdCopyBufferToImage_fn(commandBuffer, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    EndCommandBuffer_fn(commandBuffer);

    if (!submitUploadTail(s_device, s_queue, commandBuffer, stagingBuffer, stagingBufferMemory, textureImage, textureImageMemory))
        return -1;
    FreeCommandBuffers_fn(s_device, s_cmdPool, 1, &commandBuffer);

    DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
    FreeMemory_fn(s_device, stagingBufferMemory, nullptr);

    // 4. Create ImageView & Sampler
    VK_LOAD(CreateImageView)
    VK_LOAD(CreateSampler)

    VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView textureImageView;
    if (CreateImageView_fn(s_device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        return -1;
    }

    VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler textureSampler;
    if (CreateSampler_fn(s_device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        return -1;
    }

    // 5. Update Bindless Descriptor Set
    VK_LOAD(UpdateDescriptorSets)
    VkDescriptorImageInfo descImageInfo = {0};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = textureImageView;
    descImageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrite = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    descriptorWrite.dstSet = s_bindlessSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = s_textureCount;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    UpdateDescriptorSets_fn(s_device, 1, &descriptorWrite, 0, nullptr);

    // 6. Record to Registry
    int32_t id = s_textureCount;
    s_images[id] = textureImage;
    s_memories[id] = textureImageMemory;
    s_views[id] = textureImageView;
    s_samplers[id] = textureSampler;
    s_widths[id]  = (uint32_t)width;
    s_heights[id] = (uint32_t)height;
    
    s_textureCount++;
    return id;
}

bool Texture_updateSubRaw(int32_t id, const void *rgbaData, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!VkGuard_check("Texture_updateSubRaw", s_device, s_queue, false))
        return false;
    if (!Texture_isReady() || !s_gpa || id < 0 || id >= s_textureCount) return false;

    size_t imageSize = 0;
    if (!textureBytesOk(rgbaData, width, height, &imageSize))
        return false;
    uint32_t slotW = s_widths[id];
    uint32_t slotH = s_heights[id];
    if (x >= slotW || y >= slotH || width > slotW - x || height > slotH - y)
        return false;

    VK_LOAD(CreateBuffer)
    VK_LOAD(GetBufferMemoryRequirements)
    VK_LOAD(AllocateMemory)
    VK_LOAD(BindBufferMemory)
    VK_LOAD(MapMemory)
    VK_LOAD(UnmapMemory)
    VK_LOAD(DestroyBuffer)
    VK_LOAD(FreeMemory)
    
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (CreateBuffer_fn(s_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    GetBufferMemoryRequirements_fn(s_device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (AllocateMemory_fn(s_device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) return false;
    BindBufferMemory_fn(s_device, stagingBuffer, stagingBufferMemory, 0);

    void *data;
    MapMemory_fn(s_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, rgbaData, imageSize);
    UnmapMemory_fn(s_device, stagingBufferMemory);

    VK_LOAD(AllocateCommandBuffers)
    VK_LOAD(BeginCommandBuffer)
    VK_LOAD(EndCommandBuffer)
    VK_LOAD(CmdPipelineBarrier)
    VK_LOAD(CmdCopyBufferToImage)
    VK_LOAD(FreeCommandBuffers)

    VkCommandBufferAllocateInfo cmdAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = s_cmdPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    AllocateCommandBuffers_fn(s_device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BeginCommandBuffer_fn(commandBuffer, &beginInfo);

    VkImage textureImage = s_images[id];

    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageOffset.y = y;
    region.imageOffset.z = 0;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;

    CmdCopyBufferToImage_fn(commandBuffer, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    EndCommandBuffer_fn(commandBuffer);

    if (!submitUploadTail(s_device, s_queue, commandBuffer, stagingBuffer, stagingBufferMemory, VK_NULL_HANDLE, VK_NULL_HANDLE))
        return false;
    FreeCommandBuffers_fn(s_device, s_cmdPool, 1, &commandBuffer);

    DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
    FreeMemory_fn(s_device, stagingBufferMemory, nullptr);

    return true;
}
void *Texture_getDescriptorSet(void) {
    return s_bindlessSet;
}

void *Texture_getDescriptorSetLayout(void) {
    return s_descLayout;
}

bool Texture_getSize(int32_t id, uint32_t *outW, uint32_t *outH) {
    if (id < 0 || id >= s_textureCount) return false;
    if (outW) *outW = s_widths[id];
    if (outH) *outH = s_heights[id];
    return true;
}

int32_t Texture_maxBoundId(void) {
    return s_textureCount;
}

int32_t Texture_replaceRaw(int32_t id, const void *rgbaData, uint32_t width, uint32_t height) {
    if (!VkGuard_check("Texture_replaceRaw", s_device, s_queue, false))
        return -1;
    if (!Texture_isReady() || !s_gpa) return -1;
    if (id < 0 || id >= s_textureCount) {
        return Texture_loadRaw(rgbaData, width, height);
    }
    // Fast path: same size repaints the live image in place — zero destroy,
    // zero descriptor rewrite, zero retire traffic.
    if (s_widths[id] == width && s_heights[id] == height) {
        if (Texture_updateSubRaw(id, rgbaData, 0, 0, width, height)) {
            return id;
        }
        return -1;
    }

    size_t imageSize = 0;
    if (!textureBytesOk(rgbaData, width, height, &imageSize))
        return -1;

    // Retire path: detach the old handles into locals FIRST. They move onto
    // the retire ring only after the fresh image proves uploadable — a
    // failed resize keeps old content live (drop-degrade, retry next tick).
    // Never DeviceWaitIdle/Destroy* between another CB Begin/End here.
    VkImage oldImage = s_images[id];
    VkDeviceMemory oldMemory = s_memories[id];
    VkImageView oldView = s_views[id];
    VkSampler oldSampler = s_samplers[id];
    s_images[id] = VK_NULL_HANDLE;
    s_memories[id] = VK_NULL_HANDLE;
    s_views[id] = VK_NULL_HANDLE;
    s_samplers[id] = VK_NULL_HANDLE;

    VK_LOAD(CreateBuffer)
    VK_LOAD(GetBufferMemoryRequirements)
    VK_LOAD(AllocateMemory)
    VK_LOAD(BindBufferMemory)
    VK_LOAD(MapMemory)
    VK_LOAD(UnmapMemory)
    VK_LOAD(DestroyBuffer)
    VK_LOAD(FreeMemory)
    VK_LOAD(DestroyImage)

    // 1. Create Staging Buffer (failures restore the detached slot: the
    // resize never happened, old content stays live)
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    if (CreateBuffer_fn(s_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }

    VkMemoryRequirements memReqs;
    GetBufferMemoryRequirements_fn(s_device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (AllocateMemory_fn(s_device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }
    BindBufferMemory_fn(s_device, stagingBuffer, stagingBufferMemory, 0);

    void *data = nullptr;
    MapMemory_fn(s_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, rgbaData, imageSize);
    UnmapMemory_fn(s_device, stagingBufferMemory);

    // 2. Create VkImage
    VK_LOAD(CreateImage)
    VK_LOAD(GetImageMemoryRequirements)
    VK_LOAD(BindImageMemory)

    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)width;
    imageInfo.extent.height = (uint32_t)height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    if (CreateImage_fn(s_device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS) {
        DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
        FreeMemory_fn(s_device, stagingBufferMemory, nullptr);
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }

    GetImageMemoryRequirements_fn(s_device, textureImage, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (AllocateMemory_fn(s_device, &allocInfo, nullptr, &textureImageMemory) != VK_SUCCESS) {
        DestroyImage_fn(s_device, textureImage, nullptr);
        DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
        FreeMemory_fn(s_device, stagingBufferMemory, nullptr);
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }
    BindImageMemory_fn(s_device, textureImage, textureImageMemory, 0);

    // 3. Command Buffer for layout transitions and copy
    VK_LOAD(AllocateCommandBuffers)
    VK_LOAD(BeginCommandBuffer)
    VK_LOAD(EndCommandBuffer)
    VK_LOAD(CmdPipelineBarrier)
    VK_LOAD(CmdCopyBufferToImage)
    VK_LOAD(FreeCommandBuffers)

    VkCommandBufferAllocateInfo cmdAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = s_cmdPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    AllocateCommandBuffers_fn(s_device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BeginCommandBuffer_fn(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;

    CmdCopyBufferToImage_fn(commandBuffer, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    CmdPipelineBarrier_fn(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    EndCommandBuffer_fn(commandBuffer);

    // Upload timeout: the fresh image is already ringed for 2-frame reap
    // inside submitUploadTail — restore the old slot untouched (old content
    // kept, retry next tick). Never a null-handle window in the registry.
    if (!submitUploadTail(s_device, s_queue, commandBuffer, stagingBuffer, stagingBufferMemory, textureImage, textureImageMemory)) {
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }
    FreeCommandBuffers_fn(s_device, s_cmdPool, 1, &commandBuffer);

    DestroyBuffer_fn(s_device, stagingBuffer, nullptr);
    FreeMemory_fn(s_device, stagingBufferMemory, nullptr);

    // 4. Create ImageView & Sampler (DestroyImage/FreeMemory ride on the
    // staging-section loads above — one load per name per function)
    VK_LOAD(CreateImageView)
    VK_LOAD(CreateSampler)
    VK_LOAD(DestroyImageView)

    VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView textureImageView;
    if (CreateImageView_fn(s_device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        DestroyImage_fn(s_device, textureImage, nullptr);
        FreeMemory_fn(s_device, textureImageMemory, nullptr);
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }

    VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler textureSampler;
    if (CreateSampler_fn(s_device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        DestroyImageView_fn(s_device, textureImageView, nullptr);
        DestroyImage_fn(s_device, textureImage, nullptr);
        FreeMemory_fn(s_device, textureImageMemory, nullptr);
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return -1;
    }

    // 5. Retire the old row BEFORE publishing: the fresh handles are still
    // unpublished, so a full ring restores old content and destroys fresh
    // safely (never seen by a frame). Published rows die only via the ring.
    // A null old image (half-torn slot) destroys its orphaned peers inline —
    // they were never published either.
    VK_LOAD(DestroySampler)
    if (oldImage != VK_NULL_HANDLE) {
        if (!retirePush(oldImage, oldMemory, oldView, oldSampler, VK_NULL_HANDLE)) {
            DestroyImageView_fn(s_device, textureImageView, nullptr);
            DestroyImage_fn(s_device, textureImage, nullptr);
            FreeMemory_fn(s_device, textureImageMemory, nullptr);
            if (DestroySampler_fn)
                DestroySampler_fn(s_device, textureSampler, nullptr);
            s_images[id] = oldImage;
            s_memories[id] = oldMemory;
            s_views[id] = oldView;
            s_samplers[id] = oldSampler;
            return -1;
        }
    } else {
        if (oldView != VK_NULL_HANDLE)
            DestroyImageView_fn(s_device, oldView, nullptr);
        if (oldMemory != VK_NULL_HANDLE)
            FreeMemory_fn(s_device, oldMemory, nullptr);
        if (oldSampler != VK_NULL_HANDLE && DestroySampler_fn)
            DestroySampler_fn(s_device, oldSampler, nullptr);
    }

    // 6. Update Bindless Descriptor Set at slot id
    VK_LOAD(UpdateDescriptorSets)
    VkDescriptorImageInfo descImageInfo = {0};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = textureImageView;
    descImageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrite = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    descriptorWrite.dstSet = s_bindlessSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = (uint32_t)id;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    UpdateDescriptorSets_fn(s_device, 1, &descriptorWrite, 0, nullptr);

    // 7. Record to Registry at slot id
    s_images[id] = textureImage;
    s_memories[id] = textureImageMemory;
    s_views[id] = textureImageView;
    s_samplers[id] = textureSampler;
    s_widths[id]  = (uint32_t)width;
    s_heights[id] = (uint32_t)height;

    return id;
}

void Texture_free(int32_t id) {
    if (!Texture_isReady() || !s_gpa || s_instance == VK_NULL_HANDLE) return;
    if (id < 0 || id >= s_textureCount) return;
    // Retire, never destroy inline: the slot's image may still be referenced
    // by an in-flight frame. A full ring restores the slot and defers the
    // free (retry next tick) — never DeviceWaitIdle, never an inline kill.
    VkImage oldImage = s_images[id];
    VkDeviceMemory oldMemory = s_memories[id];
    VkImageView oldView = s_views[id];
    VkSampler oldSampler = s_samplers[id];
    if (oldImage == VK_NULL_HANDLE)
        return;
    s_images[id] = VK_NULL_HANDLE;
    s_memories[id] = VK_NULL_HANDLE;
    s_views[id] = VK_NULL_HANDLE;
    s_samplers[id] = VK_NULL_HANDLE;
    if (!retirePush(oldImage, oldMemory, oldView, oldSampler, VK_NULL_HANDLE)) {
        s_images[id] = oldImage;
        s_memories[id] = oldMemory;
        s_views[id] = oldView;
        s_samplers[id] = oldSampler;
        return;
    }
    s_widths[id] = 0;
    s_heights[id] = 0;
}

// GETTERS (the Symmetric Getter/Setter Completeness Law: symmetric, null-safe; statics need no guard)

int32_t Texture_retireDepth(void) {
    return s_retireCount;
}

int32_t Texture_retireCapacity(void) {
    return s_retireCap ? s_retireCap : TEXTURE_RETIRE_INIT;
}

uint64_t Texture_frameSeq(void) {
    return s_frameSeq;
}

void Texture_setRetireGuard(bool (*guard)(void)) {
    s_retireGuard = guard;
}

// the Symmetric Getter/Setter Completeness Law — symmetric introspection for the registered retire guard. Returns
// NULL when none is bound (standalone fallback path), never blocks.
bool (*Texture_getRetireGuard(void))(void) {
    return s_retireGuard;
}
