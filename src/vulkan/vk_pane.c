#include <vulkan/vulkan_core.h>
#include "vulkan/vk_pane.h"
#include "vulkan/vk.h"
#include "vulkan/vk_mac.h"
#include "vulkan/vk_guard.h"
#include "annotation/overview.h"
#include "atomic/spin.h"
#include "time/nanotime.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkPane (vulkan/vk_pane.c)
 * LEVEL: L4 — Self-Management (per-pane GPU resource lifecycle)
 * ============================================================================
 * Registry of per-CAMetalLayer Vulkan swapchains — the "panes of glass"
 * compositing model. Each visible child owns its own surface + swapchain and
 * presents independently, so the window board never rebuilds on pane motion
 * and vice versa — resize is a WindowServer layer-frame move, not a GPU
 * rebuild.
 *
 * STRUCT FIELDS (local registry):
 * ----------------------------------------------------------------------------
 *   VkPaneChain chain[VK_PANE_CHAIN_MAX];  // one per pane, fixed array
 *     cb[VK_PANE_FLIGHT], fence[VK_PANE_FLIGHT], flip, announced // flip/flop
 *     dirty // per-chain repaint demand (slot-record bit, no new class)
 *     presentCount, skipCount // lifetime present/skip diagnostics
 *   int count;
 *   VkRenderPass s_panePass;                 // BGRA/compat LOSED dedicated pass
 *   VkCommandPool s_pool;                    // per-pane CBs from one pool
 *   VkPaneRenderFn s_renderer;              // darling panel painter hook
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - VkPane_register(layer, w, h, owner)   : create surface+swapchain+CB
 *                                            (marks chain dirty: first present)
 *   - VkPane_unregister(index)              : destroy chain, release CB
 *   - VkPane_resize(index, w, h)            : swapchain rebuild (pane size)
 *                                            (marks chain dirty on rebuild)
 *   - VkPane_presentAll()                   : acquire+render+present per pane
 *                                            (Rule 39 seam guard at entry;
 *                                            skips clean chains after fence
 *                                            poll, clears demand on present)
 *   - VkPane_shutdown()                     : destroy all chains + pool
 *
 * Getters:
 *   - VkPane_ready() / VkPane_count()
 *   - VkPane_isDirty(index)           : per-chain repaint demand probe
 *   - VkPane_flightIdle()             : true when no pane submit is pending
 *                                       (Rule 39 texture-retire drain probe)
 *
 * Setters:
 *   - VkPane_setRenderer(fn)
 *   - VkPane_markDirty(index, dirty)  : per-chain repaint demand (Rule 24
 *                                       symmetric pair with isDirty)
 * ============================================================================
 */

// Device/queue/instance state owned by vulkan.c (externed like vulkan_mac.c).
extern VkDevice s_instanceDevice;
extern VkQueue s_instanceQueue;
extern PFN_vkGetDeviceProcAddr s_instanceGdpa;
extern VkInstance s_instanceInstance;
extern PFN_vkGetInstanceProcAddr s_instanceGpa;
extern VkPhysicalDevice s_instancePhys;
extern uint32_t s_instanceQueueFamily;
extern bool s_instanceDebugUtils;   // set when VK_EXT_debug_utils is live on the device

#define VK_LAYER_LOAD_DEVICE(name) \
    static PFN_vk##name name##_fn; \
    if (!name##_fn) { \
        name##_fn = (PFN_vk##name)s_instanceGdpa(s_instanceDevice, "vk" #name); \
    }

// Rule 39 seam naming: label each pane submit's MTLCommandBuffer so a device-lost
// log names the pane chain instead of the generic "vkQueueSubmit" string.
static void VkPane_nameObject(VkObjectType type, uint64_t handle, const char *name) {
    if (!s_instanceDevice || handle == 0 || !s_instanceDebugUtils)
        return;
    static PFN_vkSetDebugUtilsObjectNameEXT nameFn = nullptr;
    if (!nameFn && s_instanceGdpa)
        nameFn = (PFN_vkSetDebugUtilsObjectNameEXT)s_instanceGdpa(s_instanceDevice, "vkSetDebugUtilsObjectNameEXT");
    if (!nameFn)
        return;
    VkDebugUtilsObjectNameInfoEXT info = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    nameFn(s_instanceDevice, &info);
}

#define VK_PANE_CHAIN_MAX 32

// Per-scene flight depth: each chain owns TWO record/submit slots and
// alternates (flip/flop) every present. While slot A's submit still flies,
// the next tick records into slot B without stalling on A's fence — each
// scene renders on its own buffering, decoupled from its own present
// latency. The present walk itself (acquire → record → submit → present)
// is unchanged.
#define VK_PANE_FLIGHT 2

typedef struct VkPaneChain {
    bool active;             // slot in use (stable index; no compaction)
    void *layer;             // CAMetalLayer* (opaque)
    void *owner;             // darling Panel* (renderer callback arg)
    VkSurfaceKHR surface;
    VkSwapchainKHR chain;
    VkExtent2D extent;       // pane pixel size (fixed at register/resize)
    uint32_t imageCount;
    VkImage images[4];
    VkImageView views[4];
    VkFramebuffer fbs[4];
    VkSemaphore semAcquire;
    VkSemaphore semRender;
    VkCommandBuffer cb[VK_PANE_FLIGHT]; // per-scene record buffers (flip/flop)
    VkFence fence[VK_PANE_FLIGHT];      // per-slot submit fences (start signaled)
    uint32_t flip;           // next slot to record (alternates each present)
    bool announced;          // first-present diagnostic already logged
    bool dirty;              // repaint demand: set on register/resize/mark,
                             // cleared after a successful present (slot record)
    uint64_t presentCount;   // lifetime successful presents (diagnostic)
    uint64_t skipCount;      // lifetime fence-poll + clean skips, stale kept (diagnostic)
    uint64_t fenceTimeoutNs; // 100ms bounded (Rule 27)
} VkPaneChain;

static VkPaneChain s_chains[VK_PANE_CHAIN_MAX] = {0};
// Registry lock (two-thread live-resize contract): the present worker runs
// VkPane_presentAll every frame while thread 0 may VkPane_resize (settle) or
// VkPane_unregister (teardown). All structural mutation and the present
// iteration serialize here; the waits inside remain bounded (Rule 27).
static SpinLock s_paneLock = SPIN_LOCK_INIT;
static int s_count = 0;
static VkRenderPass s_panePass = VK_NULL_HANDLE;
static VkFormat s_panePassFormat = VK_FORMAT_UNDEFINED;
static VkCommandPool s_pool = VK_NULL_HANDLE;
static VkPaneRenderFn s_renderer = nullptr;

bool VkPane_ready(void) {
    return s_instanceDevice != VK_NULL_HANDLE && s_count > 0;
}

int VkPane_count(void) {
    return s_count;
}

uint64_t VkPane_presentCount(int index) {
    if (index < 0 || index >= s_count)
        return 0;
    return s_chains[index].presentCount;
}

uint64_t VkPane_skipCount(int index) {
    if (index < 0 || index >= s_count)
        return 0;
    return s_chains[index].skipCount;
}

// Setters / getters for the per-chain repaint-demand bit (Rule 24 symmetric
// pair; selector first, value last per Rule 9). Lock-free single-byte stores
// matching the presentCount/skipCount diagnostic pattern: the worker reads
// and clears under the registry lock while the bridge sets from outside —
// a missed mark only delays one repaint to the next tick (drop-degrade).
void VkPane_markDirty(int index, bool dirty) {
    if (index < 0 || index >= s_count)
        return;
    VkPaneChain *chain = &s_chains[index];
    if (!(*chain).active)
        return;
    (*chain).dirty = dirty;
}

bool VkPane_isDirty(int index) {
    if (index < 0 || index >= s_count)
        return false;
    VkPaneChain *chain = &s_chains[index];
    if (!(*chain).active)
        return false;
    return (*chain).dirty;
}

// Rule 39 flight probe: true only when every pane's last submit fence is
// signaled — no pane CB that sampled bindless descriptors is still executing.
// Non-blocking: GetFenceStatus poll only, under a try of the registry lock
// (a worker mid-present answers busy, safely deferring destroys rather than
// racing ResetFences). Fences start SIGNALED, so a never-submitted chain is
// idle, never falsely "flying".
bool VkPane_flightIdle(void) {
    if (!VkPane_ready())
        return true;
    if (!SpinLock_tryLock(&s_paneLock))
        return false;
    VK_LAYER_LOAD_DEVICE(GetFenceStatus)
    bool idle = true;
    for (int i = 0; i < s_count && idle; i++) {
        VkPaneChain *chain = &s_chains[i];
        if (!(*chain).active)
            continue;
        for (uint32_t s = 0; s < VK_PANE_FLIGHT && idle; s++) {
            if ((*chain).fence[s] == VK_NULL_HANDLE)
                continue;
            if (GetFenceStatus_fn && GetFenceStatus_fn(s_instanceDevice, (*chain).fence[s]) != VK_SUCCESS)
                idle = false;
        }
    }
    SpinLock_unlock(&s_paneLock);
    return idle;
}

void VkPane_setRenderer(VkPaneRenderFn fn) {
    s_renderer = fn;
}

// Dedicated pane render pass: swapchain images are PRESENT_SRC dst, cleared
// on BEGIN (pane owns its whole extent). Pass is rebuilt when the first
// registered chain negotiates a different format.
static bool ensurePanePass(VkFormat format) {
    if (s_panePass != VK_NULL_HANDLE && s_panePassFormat == format)
        return true;
    if (s_instanceGdpa == nullptr)
        return false;

    VK_LAYER_LOAD_DEVICE(CreateRenderPass)
    VK_LAYER_LOAD_DEVICE(DestroyRenderPass)

    if (s_panePass != VK_NULL_HANDLE) {
        DestroyRenderPass_fn(s_instanceDevice, s_panePass, nullptr);
        s_panePass = VK_NULL_HANDLE;
    }

    VkAttachmentDescription att = {0};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

    if (CreateRenderPass_fn(s_instanceDevice, &rpci, nullptr, &s_panePass) != VK_SUCCESS)
        return false;
    s_panePassFormat = format;
    return true;
}

// Per-pane swapchain: FIFO pacing (display-synced 60fps), transparent alpha
// honored when the window requests it (negotiated per-surface caps).
static bool buildPaneSwapchain(VkPaneChain *chain, bool transparent) {
    VK_LAYER_LOAD_DEVICE(CreateSwapchainKHR)
    VK_LAYER_LOAD_DEVICE(DestroySwapchainKHR)
    VK_LAYER_LOAD_DEVICE(GetSwapchainImagesKHR)
    VK_LAYER_LOAD_DEVICE(CreateImageView)
    VK_LAYER_LOAD_DEVICE(CreateFramebuffer)
    (void) DestroySwapchainKHR_fn;

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR capsFn =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)s_instanceGpa(s_instanceInstance,
                                                                     "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (!capsFn || capsFn(s_instancePhys, chain->surface, &caps) != VK_SUCCESS)
        return false;

    // A fresh CAMetalLayer with zero bounds may report a zero currentExtent
    // until the composite loop lays it out. The caller's registered size is
    // the ground truth for the pane's FIXED pixel grid — never fail on it.
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
        caps.currentExtent = chain->extent;
    }

    VkFormat paneFormat = VK_FORMAT_B8G8R8A8_UNORM;
    {
        uint32_t fmtCount = 0;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fmtFn =
            (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)s_instanceGpa(s_instanceInstance,
                                                                    "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (fmtFn) {
            fmtFn(s_instancePhys, chain->surface, &fmtCount, nullptr);
            VkSurfaceFormatKHR fmts[8];
            if (fmtCount > 8) fmtCount = 8;
            fmtFn(s_instancePhys, chain->surface, &fmtCount, fmts);
            if (fmtCount > 0)
                paneFormat = fmts[0].format;
            for (uint32_t i = 0; i < fmtCount; i++) {
                if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
                    paneFormat = fmts[i].format;
                    break;
                }
            }
        }
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (imageCount < 3) imageCount = 3;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    uint32_t w = caps.currentExtent.width;
    uint32_t h = caps.currentExtent.height;
    if (chain->extent.width > 0 && chain->extent.height > 0
        && (w == 0 || h == 0)) {
        w = chain->extent.width;
        h = chain->extent.height;
    }

    // The pane pass must be rebuilt when the negotiated format differs.
    if (!ensurePanePass(paneFormat))
        return false;

    VkSwapchainKHR oldSwapchain = chain->chain;
    chain->chain = VK_NULL_HANDLE;

    VkSwapchainCreateInfoKHR swci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swci.surface = chain->surface;
    swci.minImageCount = imageCount;
    swci.imageFormat = paneFormat;
    swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent = (VkExtent2D){ .width = w, .height = h };
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (transparent) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            swci.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            swci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;
    swci.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newChain = VK_NULL_HANDLE;
    if (CreateSwapchainKHR_fn(s_instanceDevice, &swci, nullptr, &newChain) != VK_SUCCESS) {
        if (oldSwapchain != VK_NULL_HANDLE)
            DestroySwapchainKHR_fn(s_instanceDevice, oldSwapchain, nullptr);
        return false;
    }
    chain->chain = newChain;
    chain->extent = (VkExtent2D){ .width = w, .height = h };
    chain->imageCount = 0;
    GetSwapchainImagesKHR_fn(s_instanceDevice, chain->chain, &chain->imageCount, nullptr);
    if (chain->imageCount > 4) chain->imageCount = 4;
    GetSwapchainImagesKHR_fn(s_instanceDevice, chain->chain, &chain->imageCount, chain->images);

    for (uint32_t i = 0; i < chain->imageCount; i++) {
        chain->views[i] = VK_NULL_HANDLE;
        chain->fbs[i] = VK_NULL_HANDLE;
        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = chain->images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = paneFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (CreateImageView_fn(s_instanceDevice, &vci, nullptr, &chain->views[i]) != VK_SUCCESS)
            return false;

        VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = s_panePass;
        fci.attachmentCount = 1;
        fci.pAttachments = &chain->views[i];
        fci.width = w;
        fci.height = h;
        fci.layers = 1;
        if (CreateFramebuffer_fn(s_instanceDevice, &fci, nullptr, &chain->fbs[i]) != VK_SUCCESS)
            return false;
    }

    return true;
}

static void destroyPaneSwapchain(VkPaneChain *chain) {
    if (!chain || chain->chain == VK_NULL_HANDLE)
        return;

    VK_LAYER_LOAD_DEVICE(DestroyFramebuffer)
    VK_LAYER_LOAD_DEVICE(DestroyImageView)
    VK_LAYER_LOAD_DEVICE(DestroySwapchainKHR)

    for (uint32_t i = 0; i < chain->imageCount; i++) {
        if (chain->fbs[i] != VK_NULL_HANDLE) {
            DestroyFramebuffer_fn(s_instanceDevice, chain->fbs[i], nullptr);
            chain->fbs[i] = VK_NULL_HANDLE;
        }
        if (chain->views[i] != VK_NULL_HANDLE) {
            DestroyImageView_fn(s_instanceDevice, chain->views[i], nullptr);
            chain->views[i] = VK_NULL_HANDLE;
        }
    }
    chain->imageCount = 0;
    if (chain->chain != VK_NULL_HANDLE) {
        DestroySwapchainKHR_fn(s_instanceDevice, chain->chain, nullptr);
        chain->chain = VK_NULL_HANDLE;
    }
}

// Tear down a chain whose registration failed partway (count not yet
// incremented — unregister() can't be used with an out-of-range index).
static void abortRegistration(VkPaneChain *chain) {
    destroyPaneSwapchain(chain);
    VK_LAYER_LOAD_DEVICE(DestroySemaphore)
    VK_LAYER_LOAD_DEVICE(DestroyFence)
    VK_LAYER_LOAD_DEVICE(FreeCommandBuffers)
    if ((*chain).semAcquire != VK_NULL_HANDLE)
        DestroySemaphore_fn(s_instanceDevice, (*chain).semAcquire, nullptr);
    if ((*chain).semRender != VK_NULL_HANDLE)
        DestroySemaphore_fn(s_instanceDevice, (*chain).semRender, nullptr);
    for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            DestroyFence_fn(s_instanceDevice, (*chain).fence[s], nullptr);
    }
    if (s_pool != VK_NULL_HANDLE)
        FreeCommandBuffers_fn(s_instanceDevice, s_pool, VK_PANE_FLIGHT, (*chain).cb);
    if ((*chain).surface != VK_NULL_HANDLE) {
        PFN_vkDestroySurfaceKHR ds =
            (PFN_vkDestroySurfaceKHR)s_instanceGpa(s_instanceInstance, "vkDestroySurfaceKHR");
        if (ds) ds(s_instanceInstance, (*chain).surface, nullptr);
    }
    memset(chain, 0, sizeof(*chain));
}

int VkPane_register(void *layer, int width, int height, void *owner) {
    if (!layer || width <= 0 || height <= 0)
        return -1;
    if (!s_instanceDevice || !s_instanceInstance)
        return -1;

    SpinLock_lock(&s_paneLock);

    if (s_count >= VK_PANE_CHAIN_MAX) {
        SpinLock_unlock(&s_paneLock);
        return -1;
    }

    VK_LAYER_LOAD_DEVICE(CreateCommandPool)
    VK_LAYER_LOAD_DEVICE(AllocateCommandBuffers)
    VK_LAYER_LOAD_DEVICE(CreateSemaphore)
    VK_LAYER_LOAD_DEVICE(CreateFence)

    if (s_pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo cpci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = s_instanceQueueFamily,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };
        if (CreateCommandPool_fn(s_instanceDevice, &cpci, nullptr, &s_pool) != VK_SUCCESS) {
            SpinLock_unlock(&s_paneLock);
            return -1;
        }
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!VkMac_createSurfaceForLayer(layer, s_instanceInstance, s_instanceGpa, &surface)) {
        SpinLock_unlock(&s_paneLock);
        return -1;
    }

    // Reuse the first free slot (stable indices — the compositor maps
    // chainIndex -> Panel and must never see a slot move).
    int idx = -1;
    for (int i = 0; i < s_count; i++) {
        if (!s_chains[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_count >= VK_PANE_CHAIN_MAX) {
            SpinLock_unlock(&s_paneLock);
            return -1;
        }
        idx = s_count;
        s_count++;
    }

    VkPaneChain *chain = &s_chains[idx];
    memset(chain, 0, sizeof(*chain));
    (*chain).active = true;
    (*chain).layer = layer;
    (*chain).owner = owner;
    (*chain).surface = surface;
    // Pane pixel size is fixed NOW (register args), even though the layer's
    // frame is still zero until the composite loop lays it out. This is the
    // registered ground truth the swapchain builder falls back to when caps
    // report a zero extent (MoltenVK sizing a frameless CAMetalLayer).
    (*chain).extent = (VkExtent2D){ .width = (uint32_t)width, .height = (uint32_t)height };
    (*chain).fenceTimeoutNs = 100000000ULL;
    (*chain).dirty = true; // registration demands the first present

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VK_PANE_FLIGHT,
    };
    if (AllocateCommandBuffers_fn(s_instanceDevice, &cbai, (*chain).cb) != VK_SUCCESS) {
        abortRegistration(chain);
        SpinLock_unlock(&s_paneLock);
        return -1;
    }
    for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
        char paneName[32];
        snprintf(paneName, sizeof(paneName), "pane present %d%c", idx, (char)('A' + s));
        VkPane_nameObject(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)(*chain).cb[s], paneName);
    }

    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (CreateSemaphore_fn(s_instanceDevice, &sci, nullptr, &(*chain).semAcquire) != VK_SUCCESS ||
        CreateSemaphore_fn(s_instanceDevice, &sci, nullptr, &(*chain).semRender) != VK_SUCCESS) {
        abortRegistration(chain);
        SpinLock_unlock(&s_paneLock);
        return -1;
    }

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
        if (CreateFence_fn(s_instanceDevice, &fci, nullptr, &(*chain).fence[s]) != VK_SUCCESS) {
            abortRegistration(chain);
            SpinLock_unlock(&s_paneLock);
            return -1;
        }
    }

    if (!buildPaneSwapchain(chain, true)) {
        (*chain).active = false;
        abortRegistration(chain);
        SpinLock_unlock(&s_paneLock);
        return -1;
    }

    fprintf(stderr, "vk: pane %d live %dx%d (chain %d)\n", idx, width, height, idx);
    SpinLock_unlock(&s_paneLock);
    return idx;
}

bool VkPane_unregister(int index) {
    SpinLock_lock(&s_paneLock);

    if (index < 0 || index >= s_count) {
        SpinLock_unlock(&s_paneLock);
        return false;
    }

    VkPaneChain *chain = &s_chains[index];

    // Bounded wait for in-flight present before teardown (Rule 27): both
    // flight slots drain, so neither record buffer can be RETIRED under.
    VK_LAYER_LOAD_DEVICE(WaitForFences)
    if ((*chain).chain != VK_NULL_HANDLE) {
        for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
            if ((*chain).fence[s] != VK_NULL_HANDLE)
                WaitForFences_fn(s_instanceDevice, 1, &(*chain).fence[s], VK_TRUE, 100000000ULL);
        }
    }
    destroyPaneSwapchain(chain);

    VK_LAYER_LOAD_DEVICE(DestroySemaphore)
    VK_LAYER_LOAD_DEVICE(DestroyFence)
    VK_LAYER_LOAD_DEVICE(FreeCommandBuffers)
    if ((*chain).semAcquire != VK_NULL_HANDLE)
        DestroySemaphore_fn(s_instanceDevice, (*chain).semAcquire, nullptr);
    if ((*chain).semRender != VK_NULL_HANDLE)
        DestroySemaphore_fn(s_instanceDevice, (*chain).semRender, nullptr);
    for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            DestroyFence_fn(s_instanceDevice, (*chain).fence[s], nullptr);
    }
    if (s_pool != VK_NULL_HANDLE)
        FreeCommandBuffers_fn(s_instanceDevice, s_pool, VK_PANE_FLIGHT, (*chain).cb);

    if ((*chain).surface != VK_NULL_HANDLE) {
        PFN_vkDestroySurfaceKHR ds =
            (PFN_vkDestroySurfaceKHR)s_instanceGpa(s_instanceInstance, "vkDestroySurfaceKHR");
        if (ds) ds(s_instanceInstance, (*chain).surface, nullptr);
    }

    // Slot stays reserved (stable indices for the compositor), just inert.
    (*chain).active = false;
    SpinLock_unlock(&s_paneLock);
    return true;
}

bool VkPane_resize(int index, int width, int height) {
    SpinLock_lock(&s_paneLock);

    if (index < 0 || index >= s_count || width <= 0 || height <= 0) {
        SpinLock_unlock(&s_paneLock);
        return false;
    }

    VkPaneChain *chain = &s_chains[index];
    if (!(*chain).active) {
        SpinLock_unlock(&s_paneLock);
        return false;
    }
    if (chain->extent.width == (uint32_t)width && chain->extent.height == (uint32_t)height) {
        SpinLock_unlock(&s_paneLock);
        return true; // fixed pane: no rebuild on window resize (Rule 11)
    }

    // Bound the wait on in-flight presents (both flight slots) before
    // tearing down the swapchain.
    VK_LAYER_LOAD_DEVICE(WaitForFences)
    for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            WaitForFences_fn(s_instanceDevice, 1, &(*chain).fence[s], VK_TRUE, (*chain).fenceTimeoutNs);
    }
    destroyPaneSwapchain(chain);
    bool ok = buildPaneSwapchain(chain, true);
    if (ok)
        (*chain).dirty = true; // rebuilt chain demands a repaint
    SpinLock_unlock(&s_paneLock);
    return ok;
}

bool VkPane_presentAll(void) {
    // Rule 39 seam guard: panes submit to the shared queue every frame; a
    // dead/nulled device must never be handed pane work. Debug net only.
    if (!VkGuard_check("VkPane_presentAll", Vk_getDevice(), Vk_getQueue(), Vk_isDeviceLost()))
        return false;
    if (!s_renderer || s_count == 0 || !s_instanceDevice)
        return false;

    SpinLock_lock(&s_paneLock);
    if (!s_renderer || s_count == 0 || !s_instanceDevice) {
        SpinLock_unlock(&s_paneLock);
        return false;
    }
    VK_LAYER_LOAD_DEVICE(ResetCommandBuffer)
    VK_LAYER_LOAD_DEVICE(BeginCommandBuffer)
    VK_LAYER_LOAD_DEVICE(EndCommandBuffer)
    VK_LAYER_LOAD_DEVICE(CmdBeginRenderPass)
    VK_LAYER_LOAD_DEVICE(CmdEndRenderPass)
    VK_LAYER_LOAD_DEVICE(QueueSubmit)
    VK_LAYER_LOAD_DEVICE(QueuePresentKHR)
    VK_LAYER_LOAD_DEVICE(AcquireNextImageKHR)
    VK_LAYER_LOAD_DEVICE(WaitForFences)
    VK_LAYER_LOAD_DEVICE(GetFenceStatus)
    VK_LAYER_LOAD_DEVICE(ResetFences)
    VK_LAYER_LOAD_DEVICE(CreateFence)
    VK_LAYER_LOAD_DEVICE(DestroyFence)

    bool presented = false;
    for (int i = 0; i < s_count; i++) {
        VkPaneChain *chain = &s_chains[i];
        if (!(*chain).active || (*chain).chain == VK_NULL_HANDLE)
            continue;

        // Flip/flop: record into the free slot while the other still flies.
        // A scene stalled on its own late submit no longer parks its next
        // record — each scene renders on its own buffering.
        uint32_t slot = (*chain).flip & 1u;
        VkCommandBuffer cb = (*chain).cb[slot];
        VkFence fence = (*chain).fence[slot];
        if (cb == VK_NULL_HANDLE || fence == VK_NULL_HANDLE)
            continue;

        // Stale-frame reuse: when this slot's last submit still flies, skip
        // the chain and keep its last drawable on screen — never block the
        // walk (a sequential bounded wait here parks every later chain, and
        // during live resize the layers must keep moving while scenes catch
        // up next tick). Non-blocking poll only: drop-degrade per Rule 27,
        // zero logging per Rule 35 hot-minimal.
        if (GetFenceStatus_fn(s_instanceDevice, fence) != VK_SUCCESS) {
            (*chain).skipCount++;
            continue;
        }

        // Clean-chain skip: no repaint demand since the last successful
        // present — keep the stale drawable, never re-record. Scenes regain
        // demand every tick through the bridge (tree-dirty propagation);
        // static panes rest here until their panel dirties again.
        if (!(*chain).dirty) {
            (*chain).skipCount++;
            continue;
        }

        uint32_t imageIndex = 0;
        VkResult ar = AcquireNextImageKHR_fn(s_instanceDevice, (*chain).chain, 25000000ULL,
                                             (*chain).semAcquire, VK_NULL_HANDLE, &imageIndex);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
            // LIVE RESIZE NOTE: this path is UNREACHABLE during live resize.
            // Pane pixel extent is FIXED at register/resize time (Rule 11):
            // VkPane_resize is a no-op when the requested size is unchanged,
            // so the swapchain never reports OUT_OF_DATE mid-drag. The board
            // swapchain handles live resize; panes present at 60fps into their
            // fixed chains via VkPane_presentAll (never paused). This rebuild
            // only triggers on true pane drift (settle or explicit resize).
            destroyPaneSwapchain(chain);
            if (buildPaneSwapchain(chain, true))
                ar = AcquireNextImageKHR_fn(s_instanceDevice, (*chain).chain, 25000000ULL,
                                            (*chain).semAcquire, VK_NULL_HANDLE, &imageIndex);
            else
                continue;
        }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR)
            continue;

        ResetCommandBuffer_fn(cb, 0);
        VkCommandBufferBeginInfo bbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        if (BeginCommandBuffer_fn(cb, &bbi) != VK_SUCCESS)
            continue;

        VkClearValue clear = {0};
        VkRenderPassBeginInfo rpbi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = s_panePass,
            .framebuffer = (*chain).fbs[imageIndex],
            .renderArea.extent = (*chain).extent,
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        CmdBeginRenderPass_fn(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        s_renderer(cb, (int)(*chain).extent.width, (int)(*chain).extent.height, (*chain).owner);
        CmdEndRenderPass_fn(cb);
        if (EndCommandBuffer_fn(cb) != VK_SUCCESS)
            continue;

        ResetFences_fn(s_instanceDevice, 1, &fence);
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &(*chain).semAcquire,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &(*chain).semRender,
        };
        if (QueueSubmit_fn(s_instanceQueue, 1, &si, fence) != VK_SUCCESS) {
            // Failed submits queue nothing: the just-reset fence would never
            // signal again, wedging this chain on its stale frame forever.
            // Recreate it signaled so the next tick retries (same refence
            // contract the compositor batch honors).
            if (CreateFence_fn && DestroyFence_fn) {
                VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                VkFence fresh = VK_NULL_HANDLE;
                if (CreateFence_fn(s_instanceDevice, &fci, nullptr, &fresh) == VK_SUCCESS) {
                    DestroyFence_fn(s_instanceDevice, fence, nullptr);
                    (*chain).fence[slot] = fresh;
                }
            }
            continue;
        }

        VkPresentInfoKHR pi = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &(*chain).semRender,
            .swapchainCount = 1,
            .pSwapchains = &(*chain).chain,
            .pImageIndices = &imageIndex,
        };
        if (QueuePresentKHR_fn(s_instanceQueue, &pi) == VK_SUCCESS) {
            presented = true;
            (*chain).flip ^= 1u;
            (*chain).presentCount++;
            (*chain).dirty = false; // demand satisfied; bridge re-arms next tick
            if (!(*chain).announced) {
                (*chain).announced = true;
                fprintf(stderr, "vk: pane %d first present %dx%d\n",
                        i, (int)(*chain).extent.width, (int)(*chain).extent.height);
            }
        }
    }
    SpinLock_unlock(&s_paneLock);
    return presented;
}

void VkPane_shutdown(void) {
    if (!s_instanceDevice)
        return;

    PFN_vkDeviceWaitIdle devWait = (PFN_vkDeviceWaitIdle)s_instanceGdpa(s_instanceDevice, "vkDeviceWaitIdle");
    if (devWait)
        devWait(s_instanceDevice);

    while (s_count > 0) {
        VkPaneChain *chain = &s_chains[s_count - 1];
        destroyPaneSwapchain(chain);
        VK_LAYER_LOAD_DEVICE(DestroySemaphore)
        VK_LAYER_LOAD_DEVICE(DestroyFence)
        VK_LAYER_LOAD_DEVICE(FreeCommandBuffers)
        if ((*chain).semAcquire != VK_NULL_HANDLE)
            DestroySemaphore_fn(s_instanceDevice, (*chain).semAcquire, nullptr);
        if ((*chain).semRender != VK_NULL_HANDLE)
            DestroySemaphore_fn(s_instanceDevice, (*chain).semRender, nullptr);
        for (uint32_t s = 0; s < VK_PANE_FLIGHT; s++) {
            if ((*chain).fence[s] != VK_NULL_HANDLE)
                DestroyFence_fn(s_instanceDevice, (*chain).fence[s], nullptr);
        }
        if (s_pool != VK_NULL_HANDLE)
            FreeCommandBuffers_fn(s_instanceDevice, s_pool, VK_PANE_FLIGHT, (*chain).cb);
        if ((*chain).surface != VK_NULL_HANDLE) {
            PFN_vkDestroySurfaceKHR ds =
                (PFN_vkDestroySurfaceKHR)s_instanceGpa(s_instanceInstance, "vkDestroySurfaceKHR");
            if (ds) ds(s_instanceInstance, (*chain).surface, nullptr);
        }
        memset(chain, 0, sizeof(VkPaneChain));
        s_count--;
    }

    if (s_pool != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyCommandPool)
        DestroyCommandPool_fn(s_instanceDevice, s_pool, nullptr);
        s_pool = VK_NULL_HANDLE;
    }
    if (s_panePass != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyRenderPass)
        DestroyRenderPass_fn(s_instanceDevice, s_panePass, nullptr);
        s_panePass = VK_NULL_HANDLE;
    }
    s_renderer = nullptr;
}