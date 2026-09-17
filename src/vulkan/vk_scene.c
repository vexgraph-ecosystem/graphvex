#include "vulkan/vk_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vk_guard.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkSceneCanvas (per-scene offscreen canvas)
 * LEVEL: L4 — Self-Management (Vulkan per-scene canvas GPU setup)
 * ============================================================================
 * Per-scene offscreen canvases (the scene's own buffer): fixed-size private
 * images rendered with a full-canvas viewport, then stamped 1:1 onto the
 * monitor cache so window resizes crop instead of squishing scene content.
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   VkSceneCanvas {        // Opaque scene canvas (see vk_scene.h)
 *     uintptr_t key;       // Caller-chosen identity key (panel pointer id)
 *     uint32_t width;      // Canvas pixel width
 *     uint32_t height;     // Canvas pixel height
 *     VkImage image[2];    // Double-buffered canvas images (front^1 renders)
 *     VkDeviceMemory memory[2]; // Per-buffer device memory
 *     VkImageView view[2]; // Per-buffer image views
 *     VkFramebuffer fb[2]; // Per-buffer framebuffers
 *     VkRenderPass pass;   // Shared single-subpass pass (view-compatible)
 *     uint32_t front;      // Index of the finished image the collage stamps
 *     bool hasFront;       // True once a first flip has completed
 *     uint32_t gen;        // Buffer-pair generation (stale-flip guard)
 *     VkImage staleImage;  // Previous-geometry front kept across resizes
 *     VkDeviceMemory staleMemory; // Stale bridge device memory
 *     uint32_t staleWidth; // Stale bridge pixel width
 *     uint32_t staleHeight; // Stale bridge pixel height
 *     bool pending;        // Back pass submitted, fence not yet confirmed
 *     uint64_t lastSubmitNs; // Last scene-pass record time (clock pacing)
 *   }
 *   SceneRetired {         // Generation graveyard entry for in-flight pairs
 *     VkImage image[2];    // Retired canvas images awaiting fence harvest
 *     VkDeviceMemory memory[2]; // Retired device memory
 *     VkImageView view[2]; // Retired image views
 *     VkFramebuffer fb[2]; // Retired framebuffers
 *     VkRenderPass pass;   // Retired shared pass
 *   }
 *   Slot + graveyard tables are heap-grown: SceneSlot *s_slots over
 *   [0, s_slotCap) (each canvas individually allocated so its address never
 *   moves) and SceneRetired *s_retired over [0, s_retiredCap); both double
 *   via slotTableGrow()/sceneRetiredGrow() (the Dynamic Scalability &
 *   Anti-Hardcoding Law), OOM failing the acquire / deferring the resize.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - VkSceneCanvas_initModule(instance, gpa, phys, device)
 *   - VkSceneCanvas_acquire(key, width, height)
 *   - VkSceneCanvas_flushRetired(void)
 *     (the Ecosystem Vulkan Safety Nets Law net: beginBackPass guards the record seam at entry)
 *   - VkSceneCanvas_width(canvas)
 *   - VkSceneCanvas_height(canvas)
 *   - VkSceneCanvas_frontImage(canvas)
 *   - VkSceneCanvas_staleImage(canvas, outWidth, outHeight)
 *   - VkSceneCanvas_beginBackPass(canvas, cb, r, g, b, a)
 *   - VkSceneCanvas_endBackPass(canvas, cb)
 *   - VkSceneCanvas_needsRender(canvas, nowNs, minGapNs)
 *   - VkSceneCanvas_generation(canvas)
 *   - VkSceneCanvas_markSubmitted(canvas, nowNs)
 *   - VkSceneCanvas_flip(canvas)
 *   - VkSceneCanvas_shutdownModule(void)
 * ============================================================================
 */


// vulkan/vk_scene.c — per-scene offscreen canvases.
//
// Mirrors vk_view's structure at scene scale: one BGRA8 device-local image,
// framebuffer, and a single-subpass clear-on-load renderpass per canvas.
// The renderpass is byte-identical to VkView's (same format, same ops, same
// layouts), which makes every pipeline built against a view's pass legal
// here — no duplicate pipelines for scene content.
//
// Layout lifecycle: initialLayout UNDEFINED (discard-safe: every pass clears
// the whole canvas), finalLayout TRANSFER_SRC_OPTIMAL. Between frames the
// image simply sits in TRANSFER_SRC; the next pass discards it via UNDEFINED.

#define VKS_CHECK(call, what)                                                  \
    do {                                                                       \
        if ((call) != VK_SUCCESS) {                                            \
            fprintf(stderr, "vk_scene: %s failed\n", what);                    \
            return false;                                                      \
        }                                                                      \
    } while (0)

struct VkSceneCanvas {
    // logical identity
    uintptr_t key;
    uint32_t width;
    uint32_t height;

    // GPU objects — DOUBLE BUFFERED (phase 2): index 0/1, `front` names the
    // finished image the collage stamps; scene passes always render into
    // front^1. Both sit in TRANSFER_SRC between their own passes.
    VkImage image[2];
    VkDeviceMemory memory[2];
    VkImageView view[2];
    VkFramebuffer fb[2];
    VkRenderPass pass;     // shared by both buffers (same format, one subpass)
    uint32_t front;
    bool hasFront;
    uint32_t gen;          // bumped on every buffer-pair swap; batch flips
                            // only apply when the recorded generation matches

    // STALE BRIDGE: the previous geometry's front image, kept alive across a
    // resize so the collage stretches it into the cut instead of showing a
    // hole. Only image+memory survive (blit needs no view); view/fb/pass of
    // that slot still ride the graveyard. Retired at the first flip.
    VkImage staleImage;
    VkDeviceMemory staleMemory;
    uint32_t staleWidth;
    uint32_t staleHeight;

    // phase-2/3 hand-off state (fence + clock live with the caller)
    bool pending;          // back pass submitted, fence not yet confirmed
    uint64_t lastSubmitNs; // last scene-pass record time (clock pacing)
};

// Keyed slot table. Linear scan — scene counts are small by nature (a
// handful of scenes per engine, not thousands). The table grows on demand
// (the Dynamic Scalability & Anti-Hardcoding Law) and each canvas lives at
// its own heap address, so the VkSceneCanvas* handed to callers stays valid
// for that canvas's whole life — growth moves table rows, never canvases.
// A failed growth fails the acquire loudly rather than evicting live
// canvases.
typedef struct {
    uintptr_t key;
    VkSceneCanvas *canvas;
} SceneSlot;
static SceneSlot *s_slots = NULL;
static size_t s_slotCap = 0;
static size_t s_slotCount = 0; // high-water mark of used slots

// Double the slot table. Callers invoke it only when the scan found no free
// row; OOM leaves the table untouched (drop-degrade).
static bool slotTableGrow(void) {
    size_t newCap = (s_slotCap == 0) ? 16 : s_slotCap * 2;
    SceneSlot *nb = (SceneSlot*) realloc(s_slots, newCap * sizeof(SceneSlot));
    if (!nb)
        return false;
    memset(nb + s_slotCap, 0, (newCap - s_slotCap) * sizeof(SceneSlot));
    s_slots = nb;
    s_slotCap = newCap;
    return true;
}

// Generation graveyard for buffer pairs retired while a scene pass was in
// flight into them (resize-during-drag). Same law as the swapchain
// graveyard: never free what a submitted command buffer may still touch.
// Entries stashed during a pending batch are flushed at that batch's fence
// completion — queue FIFO ordering means the collage blit that could have
// sampled the old front also completed by then.
typedef struct {
    VkImage image[2];
    VkDeviceMemory memory[2];
    VkImageView view[2];
    VkFramebuffer fb[2];
    VkRenderPass pass;
} SceneRetired;
static SceneRetired *s_retired = NULL;
static uint32_t s_retiredCount = 0;
static uint32_t s_retiredCap = 0;

// Double the graveyard on demand (the Dynamic Scalability & Anti-Hardcoding
// Law). Rows are index-accessed only, so realloc's move is invisible; OOM
// leaves the cap untouched and the caller defers the resize.
static bool sceneRetiredGrow(void) {
    uint32_t newCap = (s_retiredCap == 0) ? 8 : s_retiredCap * 2;
    SceneRetired *nb = (SceneRetired*) realloc(s_retired, (size_t) newCap * sizeof(SceneRetired));
    if (!nb)
        return false;
    memset(nb + s_retiredCap, 0, (size_t) (newCap - s_retiredCap) * sizeof(SceneRetired));
    s_retired = nb;
    s_retiredCap = newCap;
    return true;
}

// loader plumbing — fetched once through the caller's gpa (vk_view pattern)
static PFN_vkGetInstanceProcAddr s_gpa = nullptr;
static PFN_vkGetDeviceProcAddr s_gdpa = nullptr;
static VkInstance s_instance;
static VkPhysicalDevice s_phys;
static VkDevice s_device;

#define VKS_LOAD_INSTANCE(name)                                                \
    static PFN_vk##name name##_fn = nullptr;                                      \
    if (!name##_fn)                                                            \
        name##_fn = (PFN_vk##name)s_gpa(s_instance, "vk" #name);               \
    if (!name##_fn) {                                                          \
        fprintf(stderr, "vk_scene: missing vk%s\n", #name);                    \
        return false;                                                          \
    }

#define VKS_LOAD_DEVICE(name)                                                  \
    static PFN_vk##name name##_fn = nullptr;                                      \
    if (!name##_fn)                                                            \
        name##_fn = s_gdpa                                                     \
            ? (PFN_vk##name)s_gdpa(s_device, "vk" #name)                       \
            : (PFN_vk##name)s_gpa(s_instance, "vk" #name);                     \
    if (!name##_fn) {                                                          \
        fprintf(stderr, "vk_scene: missing vk%s\n", #name);                    \
        return false;                                                          \
    }

#define VKS_LOAD_DEVICE_VOID(name)                                             \
    static PFN_vk##name name##_fn = nullptr;                                      \
    if (!name##_fn)                                                            \
        name##_fn = s_gdpa                                                     \
            ? (PFN_vk##name)s_gdpa(s_device, "vk" #name)                       \
            : (PFN_vk##name)s_gpa(s_instance, "vk" #name);

bool VkSceneCanvas_initModule(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                              VkPhysicalDevice phys, VkDevice device) {
    if (!gpa || phys == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
        return false;
    s_gpa = gpa;
    s_instance = instance;
    s_phys = phys;
    s_device = device;
    s_gdpa = (PFN_vkGetDeviceProcAddr)gpa(instance, "vkGetDeviceProcAddr");
    return true;
}

static void destroyCanvasObjects(VkSceneCanvas *c) {
    VKS_LOAD_DEVICE_VOID(DestroyFramebuffer)
    VKS_LOAD_DEVICE_VOID(DestroyImageView)
    VKS_LOAD_DEVICE_VOID(DestroyImage)
    VKS_LOAD_DEVICE_VOID(FreeMemory)
    VKS_LOAD_DEVICE_VOID(DestroyRenderPass)

    if (s_device == VK_NULL_HANDLE)
        return;
    // The stale bridge lives OUTSIDE the pair arrays on purpose: callers
    // stash it before tearing the pair down, so the blanket memset below
    // must carry it across, not swallow it (leak + lost bridge).
    VkImage staleImage = (*c).staleImage;
    VkDeviceMemory staleMemory = (*c).staleMemory;
    uint32_t staleWidth = (*c).staleWidth;
    uint32_t staleHeight = (*c).staleHeight;
    for (uint32_t i = 0; i < 2; i++) {
        if ((*c).fb[i] != VK_NULL_HANDLE && DestroyFramebuffer_fn)
            DestroyFramebuffer_fn(s_device, (*c).fb[i], nullptr);
        if ((*c).view[i] != VK_NULL_HANDLE && DestroyImageView_fn)
            DestroyImageView_fn(s_device, (*c).view[i], nullptr);
        if ((*c).image[i] != VK_NULL_HANDLE && DestroyImage_fn)
            DestroyImage_fn(s_device, (*c).image[i], nullptr);
        if ((*c).memory[i] != VK_NULL_HANDLE && FreeMemory_fn)
            FreeMemory_fn(s_device, (*c).memory[i], nullptr);
    }
    if ((*c).pass != VK_NULL_HANDLE && DestroyRenderPass_fn)
        DestroyRenderPass_fn(s_device, (*c).pass, nullptr);
    memset(c, 0, sizeof(VkSceneCanvas));
    (*c).staleImage = staleImage;
    (*c).staleMemory = staleMemory;
    (*c).staleWidth = staleWidth;
    (*c).staleHeight = staleHeight;
}

static bool buildCanvas(VkSceneCanvas *c) {
    VKS_LOAD_DEVICE(CreateImage)
    VKS_LOAD_DEVICE(AllocateMemory)
    VKS_LOAD_DEVICE(BindImageMemory)
    VKS_LOAD_DEVICE(CreateImageView)
    VKS_LOAD_DEVICE(CreateRenderPass)
    VKS_LOAD_DEVICE(CreateFramebuffer)
    VKS_LOAD_DEVICE(GetImageMemoryRequirements)

    // One renderpass shared by both buffers (compatibility is per-pass, and
    // the buffers differ only in attachment).
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_B8G8R8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef = {0};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VKS_CHECK(CreateRenderPass_fn(s_device, &rpci, nullptr, &(*c).pass), "canvas CreateRenderPass");

    for (uint32_t i = 0; i < 2; i++) {
        VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_B8G8R8A8_UNORM;
        ici.extent.width = (*c).width;
        ici.extent.height = (*c).height;
        ici.extent.depth = 1;
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VKS_CHECK(CreateImage_fn(s_device, &ici, nullptr, &(*c).image[i]), "canvas CreateImage");

        VkMemoryRequirements req;
        GetImageMemoryRequirements_fn(s_device, (*c).image[i], &req);
        uint32_t typeIdx = UINT32_MAX;
        {
            VKS_LOAD_INSTANCE(GetPhysicalDeviceMemoryProperties)
            VkPhysicalDeviceMemoryProperties props;
            GetPhysicalDeviceMemoryProperties_fn(s_phys, &props);
            for (uint32_t m = 0; m < props.memoryTypeCount; m++) {
                if (!(req.memoryTypeBits & (1u << m)))
                    continue;
                if ((props.memoryTypes[m].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                    == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                    typeIdx = m;
                    break;
                }
            }
        }
        if (typeIdx == UINT32_MAX) {
            fprintf(stderr, "vk_scene: no device-local memory type\n");
            return false;
        }
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = typeIdx;
        VKS_CHECK(AllocateMemory_fn(s_device, &mai, nullptr, &(*c).memory[i]), "canvas AllocateMemory");
        VKS_CHECK(BindImageMemory_fn(s_device, (*c).image[i], (*c).memory[i], 0), "canvas BindImageMemory");

        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = (*c).image[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_B8G8R8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        VKS_CHECK(CreateImageView_fn(s_device, &vci, nullptr, &(*c).view[i]), "canvas CreateImageView");

        VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = (*c).pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &(*c).view[i];
        fci.width = (*c).width;
        fci.height = (*c).height;
        fci.layers = 1;
        VKS_CHECK(CreateFramebuffer_fn(s_device, &fci, nullptr, &(*c).fb[i]), "canvas CreateFramebuffer");
    }

    return true;
}

// Graveyard entry disposal — same loader discipline as canvas teardown.
static void destroyRetiredEntry(SceneRetired *r) {
    VKS_LOAD_DEVICE_VOID(DestroyFramebuffer)
    VKS_LOAD_DEVICE_VOID(DestroyImageView)
    VKS_LOAD_DEVICE_VOID(DestroyImage)
    VKS_LOAD_DEVICE_VOID(FreeMemory)
    VKS_LOAD_DEVICE_VOID(DestroyRenderPass)

    if (s_device == VK_NULL_HANDLE)
        return;
    for (uint32_t i = 0; i < 2; i++) {
        if ((*r).fb[i] != VK_NULL_HANDLE && DestroyFramebuffer_fn)
            DestroyFramebuffer_fn(s_device, (*r).fb[i], nullptr);
        if ((*r).view[i] != VK_NULL_HANDLE && DestroyImageView_fn)
            DestroyImageView_fn(s_device, (*r).view[i], nullptr);
        if ((*r).image[i] != VK_NULL_HANDLE && DestroyImage_fn)
            DestroyImage_fn(s_device, (*r).image[i], nullptr);
        if ((*r).memory[i] != VK_NULL_HANDLE && FreeMemory_fn)
            FreeMemory_fn(s_device, (*r).memory[i], nullptr);
    }
    if ((*r).pass != VK_NULL_HANDLE && DestroyRenderPass_fn)
        DestroyRenderPass_fn(s_device, (*r).pass, nullptr);
}

// Fence-law note: stale retirement only ever runs inside flip() — i.e. at a
// completed batch harvest — or at shutdown (device idle), so anything any
// submitted collage blit may still be reading is covered by the same FIFO
// completion argument that governs flushRetired itself.
static void retireStale(struct VkSceneCanvas *c) {
    if ((*c).staleImage == VK_NULL_HANDLE)
        return;
    if (s_retiredCount < s_retiredCap || sceneRetiredGrow()) {
        SceneRetired *r = &s_retired[s_retiredCount++];
        (*r).image[0] = (*c).staleImage;
        (*r).memory[0] = (*c).staleMemory;
        (*r).image[1] = VK_NULL_HANDLE;
        (*r).memory[1] = VK_NULL_HANDLE;
        for (uint32_t i = 0; i < 2; i++) {
            (*r).view[i] = VK_NULL_HANDLE;
            (*r).fb[i] = VK_NULL_HANDLE;
        }
        (*r).pass = VK_NULL_HANDLE;
    } else {
        // Table full: the harvest fence already proves every pre-batch blit
        // done, so direct disposal is covered by the same proof. Pathological.
        fprintf(stderr, "vk_scene: retire table full; dropping stale directly\n");
        destroyRetiredEntry(&(SceneRetired){ .image[0] = (*c).staleImage,
                                             .memory[0] = (*c).staleMemory });
    }
    (*c).staleImage = VK_NULL_HANDLE;
    (*c).staleMemory = VK_NULL_HANDLE;
    (*c).staleWidth = 0;
    (*c).staleHeight = 0;
}

// Move the current front's image+memory into the stale bridge BEFORE the
// resize path retires/rebuilds the pair. The graveyard copies below must
// not see these two handles again, so they are nulled in place first.
static void stashStale(VkSceneCanvas *c) {
    retireStale(c); // previous bridge yields to the newer one
    if (!(*c).hasFront)
        return; // nothing ever rendered: no front worth bridging
    uint32_t f = (*c).front;
    (*c).staleImage = (*c).image[f];
    (*c).staleMemory = (*c).memory[f];
    (*c).staleWidth = (*c).width;
    (*c).staleHeight = (*c).height;
    (*c).image[f] = VK_NULL_HANDLE;
    (*c).memory[f] = VK_NULL_HANDLE;
}

void VkSceneCanvas_flushRetired(void) {
    for (uint32_t i = 0; i < s_retiredCount; i++)
        destroyRetiredEntry(&s_retired[i]);
    s_retiredCount = 0;
}

VkSceneCanvas *VkSceneCanvas_acquire(uintptr_t key, uint32_t width, uint32_t height) {
    if (!s_gpa || width == 0 || height == 0)
        return nullptr;

    VkSceneCanvas *existing = nullptr;
    size_t freeSlot = SIZE_MAX;
    for (size_t i = 0; i < s_slotCap; i++) {
        if (s_slots[i].key == key) {
            existing = s_slots[i].canvas;
            break;
        }
        if (freeSlot == SIZE_MAX && s_slots[i].key == 0)
            freeSlot = i;
    }
    if (!existing && freeSlot == SIZE_MAX) {
        if (!slotTableGrow()) {
            fprintf(stderr, "vk_scene: slot table grow failed\n");
            return nullptr;
        }
        freeSlot = s_slotCap - 1; // the freshly appended, zeroed row
    }

    if (existing) {
        if ((*existing).width == width && (*existing).height == height)
            return existing;
        if ((*existing).pending) {
            // Resize during an in-flight scene pass: swap in a FRESH pair
            // right now — the collage tracks geometry every tick — and
            // graveyard the old pair until the batch fence proves it done.
            if (s_retiredCount >= s_retiredCap && !sceneRetiredGrow()) {
                fprintf(stderr, "vk_scene: retire table full; deferring resize\n");
                return existing; // pathological; old defer as last resort
            }
            // Bridge first: the old front becomes the collage's stretch
            // source while the fresh pair earns its own flip.
            stashStale(existing);
            SceneRetired *r = &s_retired[s_retiredCount++];
            for (uint32_t i = 0; i < 2; i++) {
                (*r).image[i] = (*existing).image[i];
                (*r).memory[i] = (*existing).memory[i];
                (*r).view[i] = (*existing).view[i];
                (*r).fb[i] = (*existing).fb[i];
            }
            (*r).pass = (*existing).pass;

            // The in-flight batch still points at this struct, but its
            // recorded generation no longer matches: its flip is refused
            // and its buffers live on in the graveyard instead.
            memset((*existing).image, 0, sizeof((*existing).image));
            memset((*existing).memory, 0, sizeof((*existing).memory));
            memset((*existing).view, 0, sizeof((*existing).view));
            memset((*existing).fb, 0, sizeof((*existing).fb));
            (*existing).width = width;
            (*existing).height = height;
            (*existing).front = 0;
            (*existing).hasFront = false;
            (*existing).pending = false;
            (*existing).gen++;
            if (!buildCanvas(existing)) {
                destroyCanvasObjects(existing);
                fprintf(stderr, "vk_scene: resize rebuild failed for key %zu\n", (size_t)key);
                return nullptr;
            }
            fprintf(stderr, "vk_scene: canvas %zu resized %ux%u (old pair retired)\n",
                    (size_t)key, width, height);
            return existing;
        }
        // Size drift with nothing in flight: rebuild directly. Same bridge
        // law — the old front serves stretched until the new pair flips.
        stashStale(existing);
        destroyCanvasObjects(existing);
        (*existing).width = width;
        (*existing).height = height;
        (*existing).front = 0;
        (*existing).hasFront = false;
        (*existing).gen++;
        if (!buildCanvas(existing)) {
            destroyCanvasObjects(existing);
            fprintf(stderr, "vk_scene: resize rebuild failed for key %zu\n", (size_t)key);
            return nullptr;
        }
        fprintf(stderr, "vk_scene: canvas %zu rebuilt %ux%u\n", (size_t)key, width, height);
        return existing;
    }

    if (freeSlot == SIZE_MAX || freeSlot >= s_slotCap) {
        fprintf(stderr, "vk_scene: slot table full\n");
        return nullptr;
    }

    VkSceneCanvas *c = s_slots[freeSlot].canvas;
    if (!c) {
        c = (VkSceneCanvas*) calloc(1, sizeof(VkSceneCanvas));
        if (!c) {
            fprintf(stderr, "vk_scene: canvas alloc failed\n");
            return nullptr;
        }
        s_slots[freeSlot].canvas = c;
    }
    memset(c, 0, sizeof(*c));
    s_slots[freeSlot].key = key;
    (*c).key = key;
    (*c).width = width;
    (*c).height = height;
    if (!buildCanvas(c)) {
        destroyCanvasObjects(c);
        s_slots[freeSlot].key = 0;
        return nullptr;
    }
    if (freeSlot == s_slotCount)
        s_slotCount++;
    fprintf(stderr, "vk_scene: canvas %zu created %ux%u (double-buffered)\n", (size_t)key, width, height);
    return c;
}

uint32_t VkSceneCanvas_width(const VkSceneCanvas *canvas) {
    return canvas ? (*canvas).width : 0;
}

uint32_t VkSceneCanvas_height(const VkSceneCanvas *canvas) {
    return canvas ? (*canvas).height : 0;
}

VkImage VkSceneCanvas_frontImage(const VkSceneCanvas *canvas) {
    if (!canvas || !(*canvas).hasFront)
        return VK_NULL_HANDLE;
    return (*canvas).image[(*canvas).front];
}

VkImage VkSceneCanvas_staleImage(const VkSceneCanvas *canvas,
                                 uint32_t *outWidth, uint32_t *outHeight) {
    if (outWidth)
        *outWidth = 0;
    if (outHeight)
        *outHeight = 0;
    if (!canvas || (*canvas).staleImage == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
    if (outWidth)
        *outWidth = (*canvas).staleWidth;
    if (outHeight)
        *outHeight = (*canvas).staleHeight;
    return (*canvas).staleImage;
}

bool VkSceneCanvas_needsRender(const VkSceneCanvas *canvas, uint64_t nowNs, int64_t minGapNs) {
    if (!canvas)
        return false;
    if ((*canvas).pending)
        return false; // previous pass still in flight — front keeps serving
    if (minGapNs <= 0)
        return true;  // clock disabled: every present may re-render
    return nowNs - (*canvas).lastSubmitNs >= (uint64_t)minGapNs;
}

uint32_t VkSceneCanvas_generation(const VkSceneCanvas *canvas) {
    return canvas ? (*canvas).gen : 0;
}

void VkSceneCanvas_markSubmitted(VkSceneCanvas *canvas, uint64_t nowNs) {
    if (!canvas)
        return;
    (*canvas).pending = true;
    (*canvas).lastSubmitNs = nowNs;
}

void VkSceneCanvas_flip(VkSceneCanvas *canvas) {
    if (!canvas)
        return;
    (*canvas).pending = false;
    (*canvas).front ^= 1u;
    (*canvas).hasFront = true;
    // Fresh front landed: realtime preference resumes. The bridge image is
    // no longer served; send it through the graveyard under this harvest's
    // fence proof.
    retireStale(canvas);
}

bool VkSceneCanvas_beginBackPass(VkSceneCanvas *canvas, VkCommandBuffer cb,
                                 float r, float g, float b, float a) {
    if (!VkGuard_checkResource("VkSceneCanvas_beginBackPass", s_device, false))
        return false;
    if (!canvas || (*canvas).fb[(*canvas).front ^ 1u] == VK_NULL_HANDLE)
        return false;
    VKS_LOAD_DEVICE(CmdBeginRenderPass)

    uint32_t back = (*canvas).front ^ 1u;
    VkClearValue clear = {0};
    clear.color.float32[0] = r;
    clear.color.float32[1] = g;
    clear.color.float32[2] = b;
    clear.color.float32[3] = a;

    VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = (*canvas).pass;
    rbi.framebuffer = (*canvas).fb[back];
    rbi.renderArea.extent = (VkExtent2D){
        .width = (*canvas).width,
        .height = (*canvas).height
    };
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    CmdBeginRenderPass_fn(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

bool VkSceneCanvas_endBackPass(VkSceneCanvas *canvas, VkCommandBuffer cb) {
    if (!canvas)
        return false;
    VKS_LOAD_DEVICE(CmdEndRenderPass)
    CmdEndRenderPass_fn(cb);

    // Availability hand-off to the collage blit (layout already TRANSFER_SRC).
    VKS_LOAD_DEVICE(CmdPipelineBarrier)
    uint32_t back = (*canvas).front ^ 1u;
    VkImageMemoryBarrier bar = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.image = (*canvas).image[back];
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar.subresourceRange.levelCount = 1;
    bar.subresourceRange.layerCount = 1;
    CmdPipelineBarrier_fn(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    return true;
}

void VkSceneCanvas_shutdownModule(void) {
    VkSceneCanvas_flushRetired();
    for (size_t i = 0; i < s_slotCap; i++) {
        VkSceneCanvas *c = s_slots[i].canvas;
        if (s_slots[i].key != 0 && c) {
            // Stale bridges ride outside destroyCanvasObjects' arrays; the
            // device is idle here, so direct disposal is fence-safe. Zeroed
            // BEFORE teardown so its preserve-across-memset can't resurrect
            // already-freed handles.
            SceneRetired stale = {0};
            stale.image[0] = (*c).staleImage;
            stale.memory[0] = (*c).staleMemory;
            (*c).staleImage = VK_NULL_HANDLE;
            (*c).staleMemory = VK_NULL_HANDLE;
            destroyRetiredEntry(&stale);
            destroyCanvasObjects(c);
            s_slots[i].key = 0;
        }
        free(s_slots[i].canvas);
        s_slots[i].canvas = NULL;
    }
    free(s_slots);
    s_slots = NULL;
    s_slotCap = 0;
    s_slotCount = 0;
    free(s_retired);
    s_retired = NULL;
    s_retiredCap = 0;
}
