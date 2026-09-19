#define VK_USE_PLATFORM_MACOS_MVK
#define VK_USE_PLATFORM_METAL_EXT

#include "vulkan/vk.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vk_pane.h"
#include "vulkan/vk_mac.h"
#include "vulkan/vk_guard.h"
#include "vulkan/vk_window_seam.h"

#include "annotation/platform_exclusive.h"
#include "annotation/intention.h"
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_macos.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "nio/mem.h"
#include "oop/type.h"
#include "time/nanotime.h"
#include "atomic/spin.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
  * CLASS: Vulkan (vulkan/vulkan.c)
  * LEVEL: L4 — Self-Management (runtime-loaded Vulkan chain setup)
  * ============================================================================
  * runtime-loaded Vulkan chain over the compositor model.
  *
  * STRUCT FIELDS (local to this file — exactly this file's class):
  * ----------------------------------------------------------------------------
  *   RetiredChain (one retired swapchain generation):
  *     VkSwapchainKHR chain;                  // retired swapchain (destroyed 3 generations later)
  *     uint32_t generation;                   // rebuild generation when retired
  *     uint32_t imageCount;                   // used slots in views[]/fbs[]
  *     VkImageView views[VK_SWAP_IMAGES_MAX]; // per-image views (die with the chain)
  *     VkFramebuffer fbs[VK_SWAP_IMAGES_MAX];// per-image framebuffers (die with the chain)
  *   Graveyard: RetiredChain *s_retired over [0, s_retiredCount), cap
  *   s_retiredCap starting at 8 and doubling via retiredGrow() (the Dynamic
  *   Scalability & Anti-Hardcoding Law); growth is index-safe (rows never
  *   leave the table) and OOM falls back to oldest-entry eviction.
  *
  *   Module statics (file-scope, own the chain across frames):
  *     VkInstance s_instance;                 // loader instance handle
  *     VkSurfaceKHR s_surface;                // CAMetalLayer-backed surface
  *     VkPhysicalDevice s_phys;               // chosen physical device
  *     VkDevice s_device;                     // logical device
  *     VkQueue s_queue;                       // graphics queue
  *     VkSwapchainKHR s_swapchain;            // live swapchain
  *
  * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Vk_init(void)
 *
  * Core Functions:
  *   - Vk_shutdown(void)
  *   - Vk_clearPresent(void)
  *   - Vk_reloadShaders(void)
  *   - Vk_fillRect(cmdBuffer, surfaceW, surfaceH, x, y, w, h, r, g, b, a)
  *   - Vk_drawTexture(cmdBuffer, surfaceW, surfaceH, x, y, w, h, r, g, b, a, textureId, mode, imgW, imgH)
  *   - Vk_drawSDFText(cmdBuffer, surfaceW, surfaceH, x, y, w, h, r, g, b, a, textureId, bold, smoothness, u0, v0, u1, v1)
  *   - Vk_drawColorGlyph(cmdBuffer, surfaceW, surfaceH, x, y, w, h, alpha, textureId, u0, v0, u1, v1)
  *   - (the Ecosystem Vulkan Safety Nets Law net: drawTexture / drawSDFText / drawColorGlyph clamp textureId
  *     against Texture_maxBoundId() so OOB ids never reach the bindless sampler)
  *   - rebuildTargets(void)
  *   - destroyTargets(void)
  *   - buildPipelines(void)
 *   - presentFrameLocked(void)     : board present chain (the Ecosystem Vulkan Safety Nets Law seam guard;
 *                                    live-drag freeze: caps-drift rebuilds
 *                                    are SKIPPED while Vk_seamIsLiveResizing
 *                                    (settle rebuilds once), OUT_OF_DATE
 *                                    rebuilds + retries inline regardless)
  *   - Vk_clearPresent(void)          : bounded present contract — silent false
  *                                    on null device/swapchain, false on the
  *                                    device-lost latch, try-lock drop with
  *                                    dirty retained; fence 100ms, acquire
  *                                    25ms, zero hot logging
  *   - Vk_clearPresentLive(void)      : live-drag variant — same single-frame
  *                                    contract, but the try-lock retries in
  *                                    ~1ms slices up to ~8ms before dropping
  *                                    (the Bounded Wait Law)
 *   - presentFrameTail(imageIndex)   : submit + present tail; present-time
 *                                    OUT_OF_DATE rebuilds once and returns
 *                                    false (retry next tick), SUBOPTIMAL is
 *                                    success without rebuild
 *   - presentNote(reason)
 *   - presentNoteCode(what, code)
 *   - presentRefenceSignaled(void)
 *   - Vk_presentFlightIdle(void)     : the Ecosystem Vulkan Safety Nets Law flight probe — true when the
 *                                      present submit fence is signaled (the
 *                                      board present CB that may sample
 *                                      bindless via the frame renderer has
 *                                      drained); consumed by the texture-retire
 *                                      guard to defer Destroy/FreeMemory under a
 *                                      flying present Submit (page-fault defect)
 *   - presentDeviceLost(where)
 *   - ensureDrawablePass(void)
  *
 * Setters:
 *   - Vk_setPreFrameRenderer(fn, userdata)
 *   - Vk_setFrameRenderer(fn, userdata)
  *   - Vk_setClearColor(r, g, b, a)
  *
 * Getters:
 *   - Vk_ready(void)
 *   - Vk_status(void)
 *   - Vk_isDeviceLost(void)
 *   - Vk_isDebugUtilsEnabled(void)   : VK_EXT_debug_utils live on the device?
  * ============================================================================
  */


// vulkan/vulkan.c — runtime-loaded Vulkan chain over the compositor model.
//
// Loader strategy: dlopen the Khronos loader (libvulkan.dylib) first, falling
// back straight to MoltenVK's ICD. Everything beyond vkGetInstanceProcAddr is
// fetched dynamically, instance-level then device-level.
//
// RENDER MODEL: each monitor owns a giant off-screen cache (see vk_view.c).
// Every present loop clears the WHOLE cache to the background color, renders
// ONE layer — the direct children of the window's container basket — at
// absolute desktop coordinates, then blits the window's region (top-left
// anchored) into the acquired swapchain image. Windows are a scissor into
// the desktop; nothing more.

static void *s_lib = nullptr;
static PFN_vkGetInstanceProcAddr s_gpa = nullptr;
static PFN_vkGetDeviceProcAddr s_gdpa = nullptr;
static char s_status[64] = "not started";

// Shared state exported to vulkan_mac.c (macOS-specific backend).
VkDevice s_instanceDevice;
VkQueue s_instanceQueue;
VkCommandBuffer s_instanceCmdBuffer;
VkPipeline s_instanceTriPipeline;
VkPipelineLayout s_instanceTriLayout;
uint64_t s_instanceAnimStartNanos;
PFN_vkGetDeviceProcAddr s_instanceGdpa;
VkInstance s_instanceInstance;
PFN_vkGetInstanceProcAddr s_instanceGpa;
VkPhysicalDevice s_instancePhys;
uint32_t s_instanceQueueFamily;
bool s_instanceDebugUtils;      // VK_EXT_debug_utils actually enabled on s_instanceDevice

static float s_clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static VkPreFrameFn s_preFrameRenderer = nullptr;
static VkFrameRenderFn s_frameRenderer = nullptr;
static void *s_frameRendererUserdata = nullptr;

void Vk_setPreFrameRenderer(VkPreFrameFn fn, void *userdata) {
    s_preFrameRenderer = fn;
    if (userdata) s_frameRendererUserdata = userdata;
}

void Vk_setFrameRenderer(VkFrameRenderFn fn, void *userdata) {
    s_frameRenderer = fn;
    if (userdata) s_frameRendererUserdata = userdata;
}

void Vk_setClearColor(float r, float g, float b, float a) {
    s_clearColor[0] = r;
    s_clearColor[1] = g;
    s_clearColor[2] = b;
    s_clearColor[3] = a;
}

#define VK_MARK(msg) do { snprintf(s_status, sizeof(s_status), msg); fprintf(stderr, "vk: %s\n", s_status); } while(0)

// Swapchain: rebuilt whenever the surface outgrows the chain (fullscreen,
// resize) or render policy drift. Each chain carries its own per-image
// views + framebuffers (the drawable pass renders straight onto acquired
// images), and those die WITH the chain — never earlier.
static bool rebuildTargets(void);
static void destroyTargets(void);
static bool buildPipelines(void);
static bool presentFrameLocked(void);

// Last-applied Window_renderGeneration. A drift means presentMode or
// transparent changed on thread 0 and the swapchain wants a rebuild.
static uint64_t s_appliedRenderGen = 0;

// Animation clock anchor for scene children (u_time seconds since init).
static uint64_t s_animStartNanos = 0;

// Serializes whole-frame submission between the present worker and thread 0's
// resize-cadence bridge. Try-lock semantics: whoever is busy loses this tick.
static SpinLock s_presentLock = SPIN_LOCK_INIT;

// Swapchain graveyard: destroying a swapchain whose present-completion
// callbacks are still queued on Metal's dispatch queues is a use-after-free
// (SIGSEGV in MVKSwapchain::beginPresentation). The Vulkan fence covers
// QUEUE work only — the CAMetal callback runs later, off-queue. So old
// chains are RETIRED here and destroyed three rebuild generations later,
// when every callback they could ever own has long since fired. Each entry
// carries the chain's image views + framebuffers so the whole generation
// dies together.
#define VK_SWAP_IMAGES_MAX 8
;;INTENTION("VK_SWAP_IMAGES_MAX bounds the harness's per-chain view/fb copies; swapchain image counts are driver-reported (2-4 in practice), and presentFrameTail guards imageIndex against the live count")

typedef struct RetiredChain {
    VkSwapchainKHR chain;
    uint32_t generation;
    uint32_t imageCount;
    VkImageView views[VK_SWAP_IMAGES_MAX];
    VkFramebuffer fbs[VK_SWAP_IMAGES_MAX];
} RetiredChain;

static RetiredChain *s_retired = NULL;
static uint32_t s_retiredCount = 0;
static uint32_t s_retiredCap = 0;

// Grow the swapchain graveyard so rapid resize churn never forces an
// oldest-entry eviction (the Dynamic Scalability & Anti-Hardcoding Law).
// Rows are index-accessed only, so realloc's move is invisible. OOM leaves
// the cap untouched: the caller falls back to the oldest-entry eviction path
// (drop-degrade per the Cold-Strict, Hot-Minimal Validation Law).
static bool retiredGrow(void) {
    if (s_retiredCount < s_retiredCap)
        return true;
    uint32_t newCap = (s_retiredCap == 0) ? 8 : s_retiredCap * 2;
    RetiredChain *nb = (RetiredChain*) realloc(s_retired, (size_t) newCap * sizeof(RetiredChain));
    if (!nb)
        return false;
    memset(nb + s_retiredCap, 0, (size_t) (newCap - s_retiredCap) * sizeof(RetiredChain));
    s_retired = nb;
    s_retiredCap = newCap;
    return true;
}
static uint32_t s_swapchainGeneration = 0;
static bool s_dumpEnabled = false;
static int s_dumpStage = 0;
static bool s_dumpEnvRead = false;

static VkInstance s_instance;
static VkSurfaceKHR s_surface;
static VkPhysicalDevice s_phys;
static uint32_t s_queueFamily = 0;
static VkDevice s_device;
static VkQueue s_queue;
static VkSwapchainKHR s_swapchain;
static VkCommandPool s_cmdPool;
static VkCommandBuffer s_cmdBuffer;

static VkSemaphore s_semAcquire;
static VkSemaphore s_semRender;
static VkFence s_fence;

// Terminal device-loss latch: once the driver reports VK_ERROR_DEVICE_LOST
// the VkDevice is dead — no fence wait, rebuild, or retry can revive it.
// Latched true, Vk_clearPresent short-circuits (no more 100ms fence burns),
// and Vk_isDeviceLost lets the title tell the truth instead of "idle".
static bool s_deviceLost = false;

static VkFormat s_format;
static VkExtent2D s_extent;
static void *s_window = nullptr;
unsigned int Vk_getFormat(void) {
    return (unsigned int) s_format;
}

// Swapchain images plus their drawable-side plumbing: a view + framebuffer
// per image lets the render pass draw straight onto the acquired image —
// the window IS the canvas now. Created per chain, retired with it.
static VkImage s_swapchainImages[VK_SWAP_IMAGES_MAX];
static VkImageView s_swapchainViews[VK_SWAP_IMAGES_MAX];
static VkFramebuffer s_swapchainFbs[VK_SWAP_IMAGES_MAX];
static uint32_t s_swapchainImageCount = 0;

// THE drawable renderpass: LOADs a pre-cleared TRANSFER_DST image (scene
// stamps survive), leaves TRANSFER_SRC for the dump path; the explicit
// final barrier walks it to PRESENT_SRC. Same format + single subpass as
// the view passes — child pipelines run here unchanged.
static VkRenderPass s_drawablePass = VK_NULL_HANDLE;
static bool ensureDrawablePass(void);

void *Vk_getDrawablePass(void) {
    return s_drawablePass;
}

// IOSurface state is owned by vulkan_mac.c (macOS-specific).

// Child pipelines, built against the monitor view's cache renderpass:
//   triangle — legacy hello-triangle scene content (push: f32 u_time @0, VS)
//   quad     — solid panel fill (push: vec4 rectNdc @0 VS, vec4 color @16 FS)
static VkPipelineLayout s_triLayout;
static VkPipeline s_triPipeline;
static VkPipelineLayout s_quadLayout;
static VkPipeline s_quadPipeline;
static VkPipelineLayout s_texLayout;
static VkPipeline s_texPipeline;
static VkPipelineLayout s_sdfLayout;
static VkPipeline s_sdfPipeline;
static bool s_pipelinesBuilt = false;

static void *s_libLoad(void) {
    void *lib = VkMac_loadLib();
    s_lib = lib;
    return lib;
}

#define VK_LOAD_GLOBAL(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = (PFN_vk##name)s_gpa(VK_NULL_HANDLE, "vk" #name); \
    if (!name##_fn) { snprintf(s_status, sizeof(s_status), "missing vk" #name); fprintf(stderr, "vk: missing vk%s\n", #name); return false; }

#define VK_LOAD_INSTANCE(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = (PFN_vk##name)s_gpa((VkInstance)s_instance, "vk" #name); \
    if (!name##_fn) { snprintf(s_status, sizeof(s_status), "missing vk" #name); fprintf(stderr, "vk: missing vk%s\n", #name); return false; }

#define VK_LOAD_DEVICE(name) \
    static PFN_vk##name name##_fn; \
    if (!name##_fn) { /* cached after first resolve: hot path must not re-query */ \
        name##_fn = s_gdpa ? (PFN_vk##name)s_gdpa(s_device, "vk" #name) : (PFN_vk##name)s_gpa(s_instance, "vk" #name); \
        if (!name##_fn) { snprintf(s_status, sizeof(s_status), "missing vk" #name); fprintf(stderr, "vk: missing vk%s\n", #name); return false; } \
    }

#define VK_LOAD_DEVICE_PTR(name) \
    static PFN_vk##name name##_fn; \
    if (!name##_fn) { \
        name##_fn = s_gdpa ? (PFN_vk##name)s_gdpa(s_device, "vk" #name) : (PFN_vk##name)s_gpa(s_instance, "vk" #name); \
        if (!name##_fn) { snprintf(s_status, sizeof(s_status), "missing vk" #name); fprintf(stderr, "vk: missing vk%s\n", #name); return nullptr; } \
    }

#define VK_LOAD_INSTANCE_VOID(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = (PFN_vk##name)s_gpa((VkInstance)s_instance, "vk" #name);

#define VK_LOAD_DEVICE_VOID(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = s_gdpa ? (PFN_vk##name)s_gdpa(s_device, "vk" #name) : (PFN_vk##name)s_gpa(s_instance, "vk" #name);

// the Ecosystem Vulkan Safety Nets Law seam naming: vkSetDebugUtilsObjectNameEXT (VK_EXT_debug_utils) gives
// every submit's MTLCommandBuffer a human name, so MoltenVK's device-lost log
// identifies the faulting seam instead of the generic "vkQueueSubmit" label.
// Gated on s_instanceDebugUtils — some MoltenVK builds are unsupported for the
// extension, and calling the loader on a non-debug device segfaults.
static void Vk_nameObject(VkObjectType type, uint64_t handle, const char *name) {
    if (!s_device || handle == 0 || !s_instanceDebugUtils) return;
    static PFN_vkSetDebugUtilsObjectNameEXT nameFn = nullptr;
    if (!nameFn && s_gdpa)
        nameFn = (PFN_vkSetDebugUtilsObjectNameEXT)s_gdpa(s_device, "vkSetDebugUtilsObjectNameEXT");
    if (!nameFn) {
        s_instanceDebugUtils = false;
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    nameFn(s_device, &info);
}

// === the Ecosystem Vulkan Safety Nets Law: Exhaustive Vulkan debug/validation in Debug builds ===
//  - VK_LAYER_KHRONOS_validation (core validation)
//  - GPU-assisted (device-side instrumentation, shader instrumentation)
//  - Best practices (MVK performance warnings, layout feedback)
//  - Debug utils object naming (seam labels)
//  - MoltenVK marker extension (MVK_CONFIG_API_DEBUG, trace logging)
//  - Strict stdbool logging of every VkResult in debug builds
// In Release these are all compiled out via the safety ribbon (the Dynamic Scalability & Anti-Hardcoding Law).
// ------------------------------------------------------------------

#if defined(DEBUG) || defined(_DEBUG)
// Validation layer + GPU-assisted + best-practices config (Debug only).
// These are passed via VkDebugUtilsMessengerEXT create flags and pNext chain.
static VkDebugUtilsMessengerEXT s_debugMessenger = VK_NULL_HANDLE;

static VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
    (void)pUserData;
    const char *severity = "INFO";
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        severity = "ERROR";
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        severity = "WARN";
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        severity = "INFO";
    const char *type = "OTHER";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
        type = "GENERAL";
    else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        type = "VALIDATION";
    else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        type = "PERFORMANCE";
    fprintf(stderr, "vk_validation [%s][%s]: %s\n", severity, type, pCallbackData->pMessage);
    return VK_FALSE; // do not abort
}

static bool createDebugMessenger(VkInstance instance, PFN_vkGetInstanceProcAddr gpa) {
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)gpa(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!vkCreateDebugUtilsMessengerEXT)
        return false;

    VkDebugUtilsMessengerCreateInfoEXT minfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vk_debugCallback,
    };

    VkResult r = vkCreateDebugUtilsMessengerEXT(instance, &minfo, nullptr, &s_debugMessenger);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "vk: debug messenger creation failed r=%d\n", r);
        return false;
    }
    return true;
}
#endif

bool Vk_init(void) {
    VK_MARK("loader+gpa");

    s_window = Vk_seamWindow();
    if (!s_window)
        return false;
    if (s_lib)
        return true;

    // 1. loader
    s_lib = s_libLoad();
    if (!s_lib) {
        snprintf(s_status, sizeof(s_status), "no loader dylib");
        return false;
    }
    s_gpa = (PFN_vkGetInstanceProcAddr)dlsym(s_lib, "vkGetInstanceProcAddr");
    if (!s_gpa) {
        snprintf(s_status, sizeof(s_status), "no gpa");
        return false;
    }

    VK_LOAD_GLOBAL(CreateInstance)
    // 2. instance — request only the extensions this driver actually exposes
    // ;;PLATFORM_EXCLUSIVE("Mac") — VK_EXT_metal_surface is macOS-only.
    VK_LOAD_GLOBAL(EnumerateInstanceExtensionProperties)
    uint32_t extCount = 0;
    EnumerateInstanceExtensionProperties_fn(nullptr, &extCount, nullptr);

    static char names[64][VK_MAX_EXTENSION_NAME_SIZE];
    VkExtensionProperties props[64];
    if (extCount > 64)
        extCount = 64;
    EnumerateInstanceExtensionProperties_fn(nullptr, &extCount, props);
    for (uint32_t i = 0; i < extCount; i++) {
        snprintf(names[i], VK_MAX_EXTENSION_NAME_SIZE, "%s", props[i].extensionName);
    }

    int surfaceExt = 0;
    const char *exts[3];
    uint32_t n = 0;
    for (uint32_t i = 0; i < extCount; i++) {
        if (strcmp(names[i], "VK_KHR_surface") == 0)
            exts[n++] = "VK_KHR_surface";
        else if (strcmp(names[i], "VK_EXT_metal_surface") == 0) {
            surfaceExt = 1;
            exts[n++] = "VK_EXT_metal_surface";
        }
        else if (strcmp(names[i], "VK_EXT_debug_utils") == 0)
            exts[n++] = "VK_EXT_debug_utils";
    }
    if (n < 2 || surfaceExt == 0) {
        snprintf(s_status, sizeof(s_status), "no surface ext (%u seen)", (unsigned)extCount);
        return false;
    }

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vex";
    app.apiVersion = VK_API_VERSION_1_2;

    // === the Ecosystem Vulkan Safety Nets Law: Validation layers in Debug builds ===
    // VK_LAYER_KHRONOS_validation + GPU-assisted + best-practices.
    // Tree-shaken by the release safety ribbon (the Dynamic Scalability & Anti-Hardcoding Law) via #if guard.
#if defined(DEBUG) || defined(_DEBUG)
    static const char *validationLayers[] = {
        "VK_LAYER_KHRONOS_validation"
    };
#endif

    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = n;
    ici.ppEnabledExtensionNames = exts;

#if defined(DEBUG) || defined(_DEBUG)
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = validationLayers;
    fprintf(stderr, "vk: validation layer VK_LAYER_KHRONOS_validation enabled (Debug)\n");
#else
    ici.enabledLayerCount = 0;
#endif

    VkResult r = CreateInstance_fn(&ici, nullptr, &s_instance);
    if (r == VK_ERROR_LAYER_NOT_PRESENT) {
        // Validation layer not installed (common without Vulkan SDK). Retry
        // without layers — validation is a Debug-build convenience, not required.
        fprintf(stderr, "vk: validation layer not present, continuing without it\n");
        ici.enabledLayerCount = 0;
        ici.ppEnabledLayerNames = nullptr;
        r = CreateInstance_fn(&ici, nullptr, &s_instance);
    }
    if (r != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "instance failed r=%d", r);
        fprintf(stderr, "vk: %s\n", s_status);
        return false;
    }

    s_gdpa = (PFN_vkGetDeviceProcAddr)s_gpa(s_instance, "vkGetDeviceProcAddr");

#if defined(DEBUG) || defined(_DEBUG)
    if (!createDebugMessenger(s_instance, s_gpa)) {
        fprintf(stderr, "vk: WARNING — debug messenger not created (layer may be absent)\n");
    }
#endif

    // 3. surface over the window's CAMetalLayer via the seam
    if (!VkMac_createSurface(s_window, s_instance, s_gpa, &s_surface)) {
        snprintf(s_status, sizeof(s_status), "surface failed");
        fprintf(stderr, "vk: %s\n", s_status);
        return false;
    }

    // 4. physical device + queue family with graphics + present support
    VK_LOAD_INSTANCE(EnumeratePhysicalDevices)
    VK_LOAD_INSTANCE(GetPhysicalDeviceQueueFamilyProperties)
    VK_LOAD_INSTANCE(GetPhysicalDeviceSurfaceSupportKHR)
    VK_LOAD_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR)
    VK_LOAD_INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR)
    VK_LOAD_INSTANCE(CreateDevice)

    uint32_t physCount = 0;
    if (EnumeratePhysicalDevices_fn(s_instance, &physCount, nullptr) != VK_SUCCESS || physCount == 0) {
        snprintf(s_status, sizeof(s_status), "no physical devices");
        return false;
    }
    VkPhysicalDevice phys[8];
    if (physCount > 8)
        physCount = 8;
    EnumeratePhysicalDevices_fn(s_instance, &physCount, phys);
    s_phys = phys[0]; // MoltenVK exposes exactly one

    uint32_t familyCount = 0;
    GetPhysicalDeviceQueueFamilyProperties_fn(s_phys, &familyCount, nullptr);
    VkQueueFamilyProperties families[16];
    if (familyCount > 16)
        familyCount = 16;
    GetPhysicalDeviceQueueFamilyProperties_fn(s_phys, &familyCount, families);

    VkBool32 presentOk = VK_FALSE;
    for (uint32_t f = 0; f < familyCount; f++) {
        if (!(families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;
        if (GetPhysicalDeviceSurfaceSupportKHR_fn(s_phys, f, s_surface, &presentOk) == VK_SUCCESS
            && presentOk == VK_TRUE) {
            s_queueFamily = f;
            break;
        }
    }
    if (!presentOk) {
        snprintf(s_status, sizeof(s_status), "no graphics+present family");
        return false;
    }

    // 5. logical device — request VK_EXT_debug_utils ONLY when this driver
    // exposes it (the Ecosystem Vulkan Safety Nets Law seam naming). An unsupported request is dropped by
    // MoltenVK yet leaves vkSetDebugUtilsObjectNameEXT on a non-debug device,
    // which segfaults on call. nDev drives enabledExtensionCount accordingly.
    VK_LOAD_INSTANCE(EnumerateDeviceExtensionProperties)
    uint32_t devExtCount = 0;
    EnumerateDeviceExtensionProperties_fn(s_phys, nullptr, &devExtCount, nullptr);
    VkExtensionProperties devProps[64];
    if (devExtCount > 64)
        devExtCount = 64;
    EnumerateDeviceExtensionProperties_fn(s_phys, nullptr, &devExtCount, devProps);
    bool hasDebugUtils = false;
    for (uint32_t i = 0; i < devExtCount; i++) {
        if (strcmp(devProps[i].extensionName, "VK_EXT_debug_utils") == 0)
            hasDebugUtils = true;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = s_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[3];
    uint32_t nDev = 0;
    devExts[nDev++] = "VK_KHR_swapchain";
    devExts[nDev++] = "VK_EXT_metal_objects";
    if (hasDebugUtils)
        devExts[nDev++] = "VK_EXT_debug_utils";
    
    VkPhysicalDeviceDescriptorIndexingFeatures idxFeat = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
    idxFeat.descriptorBindingPartiallyBound = VK_TRUE;
    idxFeat.descriptorBindingVariableDescriptorCount = VK_TRUE;
    idxFeat.runtimeDescriptorArray = VK_TRUE;
    idxFeat.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &idxFeat;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = nDev;
    dci.ppEnabledExtensionNames = devExts;

    if (CreateDevice_fn(s_phys, &dci, nullptr, &s_device) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "device failed");
        return false;
    }
    s_instanceDebugUtils = hasDebugUtils;

    VK_LOAD_DEVICE(GetDeviceQueue)
    GetDeviceQueue_fn(s_device, s_queueFamily, 0, &s_queue);

    // 6. swapchain (rebuilt on every surface resize / policy drift)
    if (!rebuildTargets())
        return false;

    if (!buildPipelines())
        return false;

    VK_LOAD_DEVICE(CreateSemaphore)
    VK_LOAD_DEVICE(CreateFence)
    VK_LOAD_DEVICE(WaitForFences)
    VK_LOAD_DEVICE(ResetFences)

    VkSemaphoreCreateInfo sci2 = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (CreateSemaphore_fn && CreateSemaphore_fn(s_device, &sci2, nullptr, &s_semAcquire) != VK_SUCCESS) return false;
    if (CreateSemaphore_fn && CreateSemaphore_fn(s_device, &sci2, nullptr, &s_semRender) != VK_SUCCESS) return false;

    VkFenceCreateInfo fci2 = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci2.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (CreateFence_fn && CreateFence_fn(s_device, &fci2, nullptr, &s_fence) != VK_SUCCESS) return false;

    s_animStartNanos = NanoTime_now();

    // Export shared state to vulkan_mac.c AFTER all init succeeds.
    s_instanceDevice = s_device;
    s_instanceQueue = s_queue;
    s_instanceCmdBuffer = s_cmdBuffer;
    s_instanceTriPipeline = s_triPipeline;
    s_instanceTriLayout = s_triLayout;
    s_instanceAnimStartNanos = s_animStartNanos;
    s_instanceGdpa = s_gdpa;
    s_instanceInstance = s_instance;
    s_instanceGpa = s_gpa;
    s_instancePhys = s_phys;
    s_instanceQueueFamily = s_queueFamily;

    // The c -> objc -> c bridge: AppKit's resize servicing drives frames at
    // the OS's own rhythm through this hook.
    Vk_seamSetResizeRenderHook((void*)VkMac_resizeRenderTrampoline, nullptr);
    return true;
}


// --- swapchain targets: created, and re-created on fullscreen/resize ---------
// Fullscreen changes the view extent; presenting to a stale chain is the bug
// that "closed" the app. Now: proactive extent check per frame, reactive
// rebuild on OUT_OF_DATE/SUBOPTIMAL.

static bool rebuildTargets(void) {
    VK_LOAD_DEVICE(CreateSwapchainKHR)
    VK_LOAD_DEVICE(DestroySwapchainKHR)
    VK_LOAD_DEVICE(GetSwapchainImagesKHR)
    VK_LOAD_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR)
    VK_LOAD_INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR)

    // NO DeviceWaitIdle here: the caller retired the previous frame through
    // its fence, and the oldSwapchain handoff lets the driver settle any
    // compositor-side presentation itself. The queue never stops.

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    if (GetPhysicalDeviceSurfaceCapabilitiesKHR_fn(s_phys, s_surface, &caps) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "caps failed");
        return false;
    }

    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
        return false;
    }

    uint32_t formatCount = 0;
    GetPhysicalDeviceSurfaceFormatsKHR_fn(s_phys, s_surface, &formatCount, nullptr);
    VkSurfaceFormatKHR formats[8];
    if (formatCount > 8)
        formatCount = 8;
    GetPhysicalDeviceSurfaceFormatsKHR_fn(s_phys, s_surface, &formatCount, formats);
    s_format = formatCount ? formats[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    for (uint32_t i = 0; i < formatCount; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            s_format = formats[i].format;
            break;
        }
    }

    VkSwapchainKHR oldSwapchain = s_swapchain;

    uint32_t imageCount = caps.minImageCount + 1;
    if (imageCount < 3) imageCount = 3;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }
    // Never request more than the harness holds; the driver may still hand
    // back more (maxImageCount 0 = unlimited), which the fetch clamps loudly.
    if (imageCount > VK_SWAP_IMAGES_MAX)
        imageCount = VK_SWAP_IMAGES_MAX;

    VkSwapchainCreateInfoKHR swci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swci.surface = s_surface;
    swci.minImageCount = imageCount; // triple-buffer bias, clamped to caps
    swci.imageFormat = s_format;
    swci.imageColorSpace = formatCount ? formats[0].colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent = caps.currentExtent;
    s_extent = caps.currentExtent;
    swci.imageArrayLayers = 1;
    // The blit is the writer now: transfer-dst is a spec-mandated supported
    // usage for swapchain images, color-attachment stays for safety.
    // Per the Ecosystem Vulkan Safety Nets Law: the drawable pass ends in
    // TRANSFER_SRC_OPTIMAL and the dump copy samples from TRANSFER_SRC, so
    // TRANSFER_SRC usage is required or MoltenVK page-faults the submit.
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform = caps.currentTransform;

    // Transparency is a request, not an order: pick the best non-opaque
    // composite mode the driver actually reports, fall back to opaque.
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (Vk_seamIsTransparent()) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            swci.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            swci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            swci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    // Pacing policy lives on the window: FIFO paces to the display,
    // IMMEDIATE submits unthrottled.
    swci.presentMode = (Vk_seamGetPresentMode() == VK_WINDOW_PRESENT_IMMEDIATE)
                       ? VK_PRESENT_MODE_IMMEDIATE_KHR
                       : VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;
    swci.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (CreateSwapchainKHR_fn(s_device, &swci, nullptr, &newSwapchain) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "swapchain failed");
        return false;
    }

    Vk_seamSetGravityTopLeft();

    // Drain retirees old enough that every Metal present-callback they own
    // has fired. Three generations of margin; no stalls.
    {
        VK_LOAD_DEVICE(DestroyImageView)
        VK_LOAD_DEVICE(DestroyFramebuffer)
        uint32_t keep = 0;
        for (uint32_t i = 0; i < s_retiredCount; i++) {
            if (s_swapchainGeneration - s_retired[i].generation >= 3) {
                if (DestroySwapchainKHR_fn)
                    DestroySwapchainKHR_fn(s_device, s_retired[i].chain, nullptr);
                for (uint32_t v = 0; v < s_retired[i].imageCount; v++) {
                    if (s_retired[i].fbs[v] != VK_NULL_HANDLE && DestroyFramebuffer_fn)
                        DestroyFramebuffer_fn(s_device, s_retired[i].fbs[v], nullptr);
                    if (s_retired[i].views[v] != VK_NULL_HANDLE && DestroyImageView_fn)
                        DestroyImageView_fn(s_device, s_retired[i].views[v], nullptr);
                }
            } else {
                s_retired[keep] = s_retired[i];
                keep++;
            }
        }
        s_retiredCount = keep;
    }

    // The old chain does NOT die here: its presentation callbacks are still
    // in flight on Metal's queues. Retire it — WITH its views/framebuffers —
    // for deferred destruction.
    if (oldSwapchain != VK_NULL_HANDLE) {
        if (s_retiredCount < s_retiredCap || retiredGrow()) {
            s_retired[s_retiredCount].chain = oldSwapchain;
            s_retired[s_retiredCount].generation = s_swapchainGeneration;
            s_retired[s_retiredCount].imageCount = s_swapchainImageCount;
            for (uint32_t v = 0; v < s_swapchainImageCount; v++) {
                s_retired[s_retiredCount].views[v] = s_swapchainViews[v];
                s_retired[s_retiredCount].fbs[v] = s_swapchainFbs[v];
            }
            s_retiredCount++;
        } else {
            // Graveyard full: retire the OLDEST entry (drop its chain without
            // destroying it — the OS releases MoltenVK's swapchain backing
            // when the CAMetalLayer is repurposed) and reuse its slot. An
            // immediate DestroySwapchainKHR here would tear down Metal
            // drawables whose present callbacks are still in flight — a
            // documented flicker source during rapid resize churn.
            VK_LOAD_DEVICE(DestroyImageView)
            VK_LOAD_DEVICE(DestroyFramebuffer)
            uint32_t oldest = 0;
            for (uint32_t i = 1; i < s_retiredCount; i++)
                if (s_retired[i].generation < s_retired[oldest].generation)
                    oldest = i;
            for (uint32_t v = 0; v < s_retired[oldest].imageCount; v++) {
                if (s_retired[oldest].fbs[v] != VK_NULL_HANDLE)
                    DestroyFramebuffer_fn(s_device, s_retired[oldest].fbs[v], nullptr);
                if (s_retired[oldest].views[v] != VK_NULL_HANDLE)
                    DestroyImageView_fn(s_device, s_retired[oldest].views[v], nullptr);
            }
            s_retired[oldest].chain = oldSwapchain;
            s_retired[oldest].generation = s_swapchainGeneration;
            s_retired[oldest].imageCount = s_swapchainImageCount;
            for (uint32_t v = 0; v < s_swapchainImageCount; v++) {
                s_retired[oldest].views[v] = s_swapchainViews[v];
                s_retired[oldest].fbs[v] = s_swapchainFbs[v];
            }
        }
    }
    s_swapchainGeneration++;
    s_swapchain = newSwapchain;

    // Fetch the raw image handles, then give each one a view + framebuffer
    // against the drawable pass — the render target of every present frame.
    s_swapchainImageCount = 0;
    GetSwapchainImagesKHR_fn(s_device, s_swapchain, &s_swapchainImageCount, nullptr);
    if (s_swapchainImageCount > VK_SWAP_IMAGES_MAX) {
        // Never silent (the Cold-Strict, Hot-Minimal Validation Law): the
        // driver handed back more images than the harness holds. Keep the
        // first VK_SWAP_IMAGES_MAX; present drops any index beyond that.
        fprintf(stderr, "[vk] swapchain reported %u images; harness holds %d — extra images unused\n",
                s_swapchainImageCount, VK_SWAP_IMAGES_MAX);
        s_swapchainImageCount = VK_SWAP_IMAGES_MAX;
    }
    GetSwapchainImagesKHR_fn(s_device, s_swapchain, &s_swapchainImageCount, s_swapchainImages);

    if (!ensureDrawablePass())
        return false;
    {
        VK_LOAD_DEVICE(CreateImageView)
        VK_LOAD_DEVICE(CreateFramebuffer)
        memset(s_swapchainViews, 0, sizeof(s_swapchainViews));
        memset(s_swapchainFbs, 0, sizeof(s_swapchainFbs));
        for (uint32_t i = 0; i < s_swapchainImageCount; i++) {
            VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vci.image = s_swapchainImages[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = s_format;
            vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.levelCount = 1;
            vci.subresourceRange.layerCount = 1;
            if (CreateImageView_fn(s_device, &vci, nullptr, &s_swapchainViews[i]) != VK_SUCCESS) {
                snprintf(s_status, sizeof(s_status), "drawable view failed");
                return false;
            }
            VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fci.renderPass = s_drawablePass;
            fci.attachmentCount = 1;
            fci.pAttachments = &s_swapchainViews[i];
            fci.width = s_extent.width;
            fci.height = s_extent.height;
            fci.layers = 1;
            if (CreateFramebuffer_fn(s_device, &fci, nullptr, &s_swapchainFbs[i]) != VK_SUCCESS) {
                snprintf(s_status, sizeof(s_status), "drawable framebuffer failed");
                return false;
            }
        }
    }

    fprintf(stderr, "vk: swapchain live %ux%u fmt=%d present=%d\n", s_extent.width, s_extent.height, (int)s_format,
            (int)(swci.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR));
    s_appliedRenderGen = Vk_seamRenderGeneration();
    return true;
}

// The drawable renderpass, built once per format. LOAD-on-store: the frame's
// command stream clears the image to the board color and stamps scene
// canvases BEFORE this pass opens, so LOAD preserves them while procedural
// children draw on top — true child-order z-compositing for free.
static bool ensureDrawablePass(void) {
    if (s_drawablePass != VK_NULL_HANDLE)
        return true;
    VK_LOAD_DEVICE(CreateRenderPass)

    VkAttachmentDescription att = {0};
    att.format = s_format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
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
    if (CreateRenderPass_fn(s_device, &rpci, nullptr, &s_drawablePass) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "drawable pass failed");
        return false;
    }
    return true;
}

// IOSurface render pass is owned by vulkan_mac.c (macOS-specific).

static void destroyTargets(void) {
    if (s_device == VK_NULL_HANDLE)
        return;
    VK_LOAD_DEVICE_VOID(DeviceWaitIdle)
    VK_LOAD_DEVICE_VOID(DestroySwapchainKHR)
    VK_LOAD_DEVICE_VOID(DestroyImageView)
    VK_LOAD_DEVICE_VOID(DestroyFramebuffer)
    VK_LOAD_DEVICE_VOID(DestroyRenderPass)
    if (DeviceWaitIdle_fn)
        DeviceWaitIdle_fn(s_device);

    // Final drain: shutdown idles, so every pending present callback has
    // fired and the graveyard can be flushed unconditionally.
    for (uint32_t i = 0; i < s_retiredCount; i++) {
        if (DestroySwapchainKHR_fn)
            DestroySwapchainKHR_fn(s_device, s_retired[i].chain, nullptr);
        for (uint32_t v = 0; v < s_retired[i].imageCount; v++) {
            if (s_retired[i].fbs[v] != VK_NULL_HANDLE && DestroyFramebuffer_fn)
                DestroyFramebuffer_fn(s_device, s_retired[i].fbs[v], nullptr);
            if (s_retired[i].views[v] != VK_NULL_HANDLE && DestroyImageView_fn)
                DestroyImageView_fn(s_device, s_retired[i].views[v], nullptr);
        }
    }
    s_retiredCount = 0;
    free(s_retired);
    s_retired = NULL;
    s_retiredCap = 0;
    s_swapchainGeneration = 0;

    for (uint32_t i = 0; i < s_swapchainImageCount; i++) {
        if (s_swapchainFbs[i] != VK_NULL_HANDLE && DestroyFramebuffer_fn)
            DestroyFramebuffer_fn(s_device, s_swapchainFbs[i], nullptr);
        if (s_swapchainViews[i] != VK_NULL_HANDLE && DestroyImageView_fn)
            DestroyImageView_fn(s_device, s_swapchainViews[i], nullptr);
    }
    s_swapchainImageCount = 0;

    if (s_drawablePass != VK_NULL_HANDLE && DestroyRenderPass_fn)
        DestroyRenderPass_fn(s_device, s_drawablePass, nullptr);
    s_drawablePass = VK_NULL_HANDLE;

    if (s_swapchain != VK_NULL_HANDLE && DestroySwapchainKHR_fn)
        DestroySwapchainKHR_fn(s_device, s_swapchain, nullptr);
    s_swapchain = VK_NULL_HANDLE;

    if (s_sdfPipeline != VK_NULL_HANDLE) {
        VK_LOAD_DEVICE_VOID(DestroyPipeline)
        if (DestroyPipeline_fn) DestroyPipeline_fn(s_device, s_sdfPipeline, nullptr);
        s_sdfPipeline = VK_NULL_HANDLE;
    }
    if (s_sdfLayout != VK_NULL_HANDLE) {
        VK_LOAD_DEVICE_VOID(DestroyPipelineLayout)
        if (DestroyPipelineLayout_fn) DestroyPipelineLayout_fn(s_device, s_sdfLayout, nullptr);
        s_sdfLayout = VK_NULL_HANDLE;
    }
}

bool Vk_ready(void) {
    return s_lib && s_device != VK_NULL_HANDLE && s_swapchain != VK_NULL_HANDLE;
}

const char *Vk_status(void) {
    return s_status;
}

void Vk_shutdown(void) {
    if (!s_lib)
        return;
    if (s_device != VK_NULL_HANDLE) {
        // Retained offscreen layer targets + per-pane CAMetalLayer chains
        // first; board targets last (the Teardown Order Law: destroy top-down, free last).
        VkLayer_shutdown();
        VkPane_shutdown();
        destroyTargets();
        s_pipelinesBuilt = false;
    }
    if (s_instance != VK_NULL_HANDLE) {
#if defined(DEBUG) || defined(_DEBUG)
        if (s_debugMessenger != VK_NULL_HANDLE) {
            VK_LOAD_INSTANCE_VOID(DestroyDebugUtilsMessengerEXT)
            if (DestroyDebugUtilsMessengerEXT_fn)
                DestroyDebugUtilsMessengerEXT_fn(s_instance, s_debugMessenger, nullptr);
            s_debugMessenger = VK_NULL_HANDLE;
        }
#endif
        VK_LOAD_INSTANCE_VOID(DestroySurfaceKHR)
        if (DestroySurfaceKHR_fn)
            DestroySurfaceKHR_fn(s_instance, s_surface, nullptr);
        VK_LOAD_INSTANCE_VOID(DestroyInstance)
        if (DestroyInstance_fn)
            DestroyInstance_fn(s_instance, nullptr);
    }
    if (s_lib) {
        dlclose(s_lib);
        s_lib = nullptr;
    }
    s_device = VK_NULL_HANDLE;
    s_swapchain = VK_NULL_HANDLE;
    s_instance = VK_NULL_HANDLE;
    s_surface = VK_NULL_HANDLE;
    s_gpa = nullptr;
    s_gdpa = nullptr;
    s_preFrameRenderer = nullptr;
    s_frameRenderer = nullptr;
    s_frameRendererUserdata = nullptr;
    snprintf(s_status, sizeof(s_status), "shutdown");
}

// Milestone-1 clear+present lands next: acquire -> renderpass(loadOp=CLEAR)
// -> submit -> present. The chain above is its prerequisite.


// --- hello triangle (Legacy: vulkan/TriangleRenderer.java + your spv blobs) --
//
// Full-screen triangle shader pair with a float push constant u_time. The
// fragment stage paints the animated gradient and the bouncing glow triangle;
// the vertex stage needs no vertex buffers at all.

#include <stdlib.h>

#include <mach-o/dyld.h>

// Bundle-aware spv lookup: inside the .app's Resources next to the executable,
// then cwd-relative for dev runs from the repo root, then the CMake-provided
// source dir (generated per machine — never hardcoded).
static unsigned char *loadSpv(const char *path, size_t *outSize);

static unsigned char *loadSpvAny(const char *name, size_t *outSize) {
    char path[512];
    uint32_t exeSize = sizeof(path);
    if (_NSGetExecutablePath(path, &exeSize) == 0) {
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = 0;
            // 1. Inside .app bundle: Contents/MacOS/.. -> Contents/Resources/spv/
            char candidate[512];
            snprintf(candidate, sizeof(candidate), "%s/../Resources/spv/%s", path, name);
            unsigned char *code = loadSpv(candidate, outSize);
            if (code)
                return code;

            // 2. Next to executable: <exe_dir>/spv/<name>
            snprintf(candidate, sizeof(candidate), "%s/spv/%s", path, name);
            code = loadSpv(candidate, outSize);
            if (code)
                return code;

            // 3. Executable in build/ or cmake-build-debug/: <exe_dir>/../src/vulkan/spv/<name>
            snprintf(candidate, sizeof(candidate), "%s/../src/vulkan/spv/%s", path, name);
            code = loadSpv(candidate, outSize);
            if (code)
                return code;
        }
    }

    // 4. Direct CWD-relative
    unsigned char *code = loadSpv(name, outSize);
    if (code)
        return code;

    // 5. CWD-relative src/vulkan/spv/
    snprintf(path, sizeof(path), "src/vulkan/spv/%s", name);
    code = loadSpv(path, outSize);
    if (code)
        return code;

    // 6. CWD-relative ../src/vulkan/spv/
    snprintf(path, sizeof(path), "../src/vulkan/spv/%s", name);
    code = loadSpv(path, outSize);
    if (code)
        return code;

#ifdef ANTI_SPV_DIR
    snprintf(path, sizeof(path), "%s/%s", ANTI_SPV_DIR, name);
    code = loadSpv(path, outSize);
    if (code)
        return code;
#endif
    return nullptr;
}


static unsigned char *loadSpv(const char *path, size_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size % 4 != 0) {
        fclose(f);
        return nullptr;
    }
    unsigned char *bytes = (unsigned char*) malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return nullptr;
    }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        return nullptr;
    }
    fclose(f);
    *outSize = (size_t)size;
    return bytes;
}

static VkShaderModule createShaderModule(const char *name, const char *unused) {
    (void)unused;
    VK_LOAD_DEVICE_PTR(CreateShaderModule)
    size_t size = 0;
    unsigned char *code = loadSpvAny(name, &size);
    if (!code)
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = (const uint32_t*) code;

    VkShaderModule module = VK_NULL_HANDLE;
    if (CreateShaderModule_fn(s_device, &ci, nullptr, &module) != VK_SUCCESS) {
        free(code);
        return VK_NULL_HANDLE;
    }
    free(code); // driver may reference code until pipeline creation; safe for v1 demo
    return module;
}

// --- Vk_fillRect: the default panel draw as a public primitive --------------
// The quad path extracted verbatim so Panel_RenderFn overrides can point at
// real content instead of poking renderer internals. Each call is self-
// contained: viewport spans the whole drawable (u_rectNdc places the quad in
// clip space), the scissor clips to THIS rect — stacking several fills in
// one handler just works.
void Vk_fillRect(void *cmdBuffer, float surfaceW, float surfaceH,
                 float x, float y, float w, float h,
                 float r, float g, float b, float a) {
    if (!cmdBuffer || w <= 0.0f || h <= 0.0f || surfaceW <= 0.0f || surfaceH <= 0.0f)
        return;
    if (!s_pipelinesBuilt || s_quadPipeline == VK_NULL_HANDLE)
        return;

    VK_LOAD_DEVICE_VOID(CmdBindPipeline)
    VK_LOAD_DEVICE_VOID(CmdSetViewport)
    VK_LOAD_DEVICE_VOID(CmdSetScissor)
    VK_LOAD_DEVICE_VOID(CmdPushConstants)
    VK_LOAD_DEVICE_VOID(CmdDraw)

    float drawW = surfaceW;
    float drawH = surfaceH;
    // Clip against the drawable here: handlers receive pre-clipped rects,
    // but defensive clipping keeps stacked sub-rects honest for free.
    float fx = x < 0.0f ? 0.0f : x;
    float fy = y < 0.0f ? 0.0f : y;
    if (fx + w > drawW) w = drawW - fx;
    if (fy + h > drawH) h = drawH - fy;
    if (w <= 0.0f || h <= 0.0f)
        return;

    VkViewport viewport = {0};
    viewport.width = drawW;
    viewport.height = -drawH; // Negative for top-down UI map to bottom-up frame
    viewport.y = drawH;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {0};
    scissor.offset.x = (int32_t)fx;
    scissor.offset.y = (int32_t)(drawH - fy - h);
    scissor.extent.width = (uint32_t)w;
    scissor.extent.height = (uint32_t)h;

    float ndc[8]; // rectNdc.xyzw + color.rgba, push-constant block
    ndc[0] = fx / drawW * 2.0f - 1.0f;
    ndc[1] = fy / drawH * 2.0f - 1.0f;
    ndc[2] = w / drawW * 2.0f;
    ndc[3] = h / drawH * 2.0f;
    ndc[4] = r;
    ndc[5] = g;
    ndc[6] = b;
    ndc[7] = a;
    CmdBindPipeline_fn(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, s_quadPipeline);
    CmdSetViewport_fn(cmdBuffer, 0, 1, &viewport);
    CmdSetScissor_fn(cmdBuffer, 0, 1, &scissor);
    CmdPushConstants_fn(cmdBuffer, s_quadLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, 32, ndc);
    CmdDraw_fn(cmdBuffer, 6, 1, 0, 0);
}

// --- collage verification: read the monitor cache back to a TGA -------------
// ANTI_VK_DUMP=1. Phase 1 records a cache->buffer copy INSIDE the present
// command buffer (correct layout by construction, GPU-ordered); phase 2,
// next frame after the fence proves it retired, maps and writes
// /tmp/vk_cache_dump.tga. No timing or layout assumptions.
static VkBuffer s_dumpBuffer;
static VkDeviceMemory s_dumpMem;
static VkDeviceSize s_dumpSize = 0;
static int32_t s_dumpW = 0, s_dumpH = 0;

static bool dumpAllocStage(uint32_t width, uint32_t height) {
    VK_LOAD_DEVICE(CreateBuffer)
    VK_LOAD_DEVICE(GetBufferMemoryRequirements)
    VK_LOAD_DEVICE(AllocateMemory)
    VK_LOAD_DEVICE(BindBufferMemory)
    VK_LOAD_INSTANCE(GetPhysicalDeviceMemoryProperties)

    s_dumpW = (int32_t)width;
    s_dumpH = (int32_t)height;
    s_dumpSize = (VkDeviceSize)s_dumpW * s_dumpH * 4;

    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = s_dumpSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (CreateBuffer_fn(s_device, &bci, nullptr, &s_dumpBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    GetBufferMemoryRequirements_fn(s_device, s_dumpBuffer, &req);
    VkPhysicalDeviceMemoryProperties props;
    GetPhysicalDeviceMemoryProperties_fn(s_phys, &props);
    uint32_t typeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (!(req.memoryTypeBits & (1u << i)))
            continue;
        if ((props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            typeIdx = i;
            break;
        }
    }
    if (typeIdx == UINT32_MAX)
        return false;

    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = typeIdx;
    if (AllocateMemory_fn(s_device, &mai, nullptr, &s_dumpMem) != VK_SUCCESS)
        return false;
    BindBufferMemory_fn(s_device, s_dumpBuffer, s_dumpMem, 0);
    return true;
}

static bool dumpRecordCopy(VkImage image, VkCommandBuffer cb) {
    // Called mid-recording, immediately after the drawable pass: the image
    // is in TRANSFER_SRC_OPTIMAL and the barrier ordering is already correct.
    VK_LOAD_DEVICE(CmdCopyImageToBuffer)
    VkBufferImageCopy region = {0};
    region.bufferRowLength = (uint32_t)s_dumpW;
    region.bufferImageHeight = (uint32_t)s_dumpH;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)s_dumpW;
    region.imageExtent.height = (uint32_t)s_dumpH;
    region.imageExtent.depth = 1;
    CmdCopyImageToBuffer_fn(cb, image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            s_dumpBuffer, 1, &region);
    return true;
}

static bool dumpWriteFile(void) {
    VK_LOAD_DEVICE(MapMemory)
    VK_LOAD_DEVICE(UnmapMemory)
    VK_LOAD_DEVICE(FreeMemory)
    VK_LOAD_DEVICE(DestroyBuffer)

    void *mapped = nullptr;
    if (MapMemory_fn(s_device, s_dumpMem, 0, s_dumpSize, 0, &mapped) == VK_SUCCESS) {
        FILE *f = fopen("/tmp/vk_cache_dump.tga", "wb");
        if (f) {
            uint8_t hdr[18] = {0};
            hdr[2] = 2;                 // uncompressed true-color
            hdr[12] = (uint8_t)(s_dumpW & 0xFF);  hdr[13] = (uint8_t)(s_dumpW >> 8);
            hdr[14] = (uint8_t)(s_dumpH & 0xFF);  hdr[15] = (uint8_t)(s_dumpH >> 8);
            hdr[16] = 32;               // bits per pixel
            hdr[17] = 0x20;             // top-left origin
            fwrite(hdr, 1, 18, f);
            fwrite(mapped, 1, (size_t)s_dumpSize, f);
            fclose(f);
            fprintf(stderr, "vk:dump -> /tmp/vk_cache_dump.tga (%dx%d)\n", s_dumpW, s_dumpH);
        }
        UnmapMemory_fn(s_device, s_dumpMem);
    } else {
        fprintf(stderr, "vk:dump map failed\n");
    }
    DestroyBuffer_fn(s_device, s_dumpBuffer, nullptr);
    FreeMemory_fn(s_device, s_dumpMem, nullptr);
    s_dumpBuffer = VK_NULL_HANDLE;
    s_dumpMem = VK_NULL_HANDLE;
    return true;
}


// Build the tri + quad child pipelines against an already-live drawable
// pass. Shared by init (buildPipelines) and Phase-3 live reload
// (Vk_reloadShaders): same bytecode, no device/surface/window teardown.
// Shader modules are destroyed right after each pipeline bakes — the
// driver holds its own copy, so repeated reloads cannot accumulate leaks.
static bool buildTriQuad(VkRenderPass pass) {
    VK_LOAD_DEVICE(CreatePipelineLayout)
    VK_LOAD_DEVICE(CreateGraphicsPipelines)
    VK_LOAD_DEVICE(DestroyShaderModule)

    // --- shared pipeline skeleton ---------------------------------------
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // push-rect quads face either way
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend = {0};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    // --- triangle child (legacy hello-triangle scene content) ------------
    VkShaderModule triVert = createShaderModule("hello_triangle_vert.spv", nullptr);
    VkShaderModule triFrag = createShaderModule("hello_triangle_frag.spv", nullptr);
    if (triVert == VK_NULL_HANDLE || triFrag == VK_NULL_HANDLE) {
        snprintf(s_status, sizeof(s_status), "triangle spv not found");
        fprintf(stderr, "vk: triangle spv not found\n");
        return false;
    }

    VkPushConstantRange triPush = {0};
    triPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    triPush.offset = 0;
    triPush.size = 4; // float u_time

    VkPipelineLayoutCreateInfo tlci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    tlci.pushConstantRangeCount = 1;
    tlci.pPushConstantRanges = &triPush;
    if (CreatePipelineLayout_fn(s_device, &tlci, nullptr, &s_triLayout) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "tri layout failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo tstages[2] = {{0}, {0}};
    tstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    tstages[0].module = triVert;
    tstages[0].pName = "main";
    tstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    tstages[1].module = triFrag;
    tstages[1].pName = "main";

    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;   // legacy triangle winding
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    VkGraphicsPipelineCreateInfo tpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    tpci.stageCount = 2;
    tpci.pStages = tstages;
    tpci.pVertexInputState = &vi;
    tpci.pInputAssemblyState = &ia;
    tpci.pViewportState = &vp;
    tpci.pRasterizationState = &rs;
    tpci.pMultisampleState = &ms;
    tpci.pColorBlendState = &cb;
    tpci.pDynamicState = &ds;
    tpci.layout = s_triLayout;
    tpci.renderPass = pass;
    tpci.subpass = 0;
    if (CreateGraphicsPipelines_fn(s_device, VK_NULL_HANDLE, 1, &tpci, nullptr, &s_triPipeline) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "tri pipeline failed");
        DestroyShaderModule_fn(s_device, triVert, nullptr);
        DestroyShaderModule_fn(s_device, triFrag, nullptr);
        return false;
    }

    // --- quad child (solid panel fill) ------------------------------------
    VkShaderModule quadVert = createShaderModule("solid_quad_vert.spv", nullptr);
    VkShaderModule quadFrag = createShaderModule("solid_quad_frag.spv", nullptr);
    if (quadVert == VK_NULL_HANDLE || quadFrag == VK_NULL_HANDLE) {
        snprintf(s_status, sizeof(s_status), "quad spv not found");
        fprintf(stderr, "vk: quad spv not found\n");
        return false;
    }

    VkPushConstantRange quadPush[2] = {{0}, {0}};
    quadPush[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    quadPush[0].offset = 0;
    quadPush[0].size = 16; // vec4 u_rectNdc
    quadPush[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    quadPush[1].offset = 16;
    quadPush[1].size = 16; // vec4 u_color

    VkPipelineLayoutCreateInfo qlci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    qlci.pushConstantRangeCount = 2;
    qlci.pPushConstantRanges = quadPush;
    if (CreatePipelineLayout_fn(s_device, &qlci, nullptr, &s_quadLayout) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "quad layout failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo qstages[2] = {{0}, {0}};
    qstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    qstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    qstages[0].module = quadVert;
    qstages[0].pName = "main";
    qstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    qstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    qstages[1].module = quadFrag;
    qstages[1].pName = "main";

    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.cullMode = VK_CULL_MODE_NONE;
    VkGraphicsPipelineCreateInfo qpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    qpci.stageCount = 2;
    qpci.pStages = qstages;
    qpci.pVertexInputState = &vi;
    qpci.pInputAssemblyState = &ia;
    qpci.pViewportState = &vp;
    qpci.pRasterizationState = &rs;
    qpci.pMultisampleState = &ms;
    qpci.pColorBlendState = &cb;
    qpci.pDynamicState = &ds;
    qpci.layout = s_quadLayout;
    qpci.renderPass = pass;
    qpci.subpass = 0;
    if (CreateGraphicsPipelines_fn(s_device, VK_NULL_HANDLE, 1, &qpci, nullptr, &s_quadPipeline) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "quad pipeline failed");
        return false;
    }
    DestroyShaderModule_fn(s_device, quadVert, nullptr);
    DestroyShaderModule_fn(s_device, quadFrag, nullptr);
    DestroyShaderModule_fn(s_device, triVert, nullptr);
    DestroyShaderModule_fn(s_device, triFrag, nullptr);
    return true;
}

// Build both child pipelines against the monitor view's cache renderpass,
// plus the command pool and primary buffer that record each present loop.
// Runs once at init — no more lazy first-frame building, no retry leaks.
static bool buildPipelines(void) {
    VK_LOAD_DEVICE(CreateCommandPool)
    VK_LOAD_DEVICE(AllocateCommandBuffers)

    // Renderpass compatibility: the drawable pass carries the same format +
    // single-subpass shape the pipelines were designed against, so they run
    // unchanged whether targeting a scene canvas or the window itself.
    if (s_drawablePass == VK_NULL_HANDLE) {
        snprintf(s_status, sizeof(s_status), "no drawable renderpass");
        return false;
    }
    if (!buildTriQuad(s_drawablePass))
        return false;

    // --- command plumbing --------------------------------------------------
    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = s_queueFamily;
    if (CreateCommandPool_fn(s_device, &cpci, nullptr, &s_cmdPool) != VK_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "cmdpool failed");
        return false;
    }
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = s_cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    AllocateCommandBuffers_fn(s_device, &cbai, &s_cmdBuffer);
    Vk_nameObject(VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)s_cmdBuffer, "board present");

    s_pipelinesBuilt = true;
    return true;
}

// Phase-3 live shader reload: rebuild tri/quad against the still-live
// drawable pass with zero device/surface/window teardown. tex/sdf are
// lazy-ensured on next draw, so nulling them schedules their rebuild for
// free. Destroy-then-rebuild (not A/B) keeps VRAM flat; on failure the
// pipelines stay down and present drops frames until the next retry —
// the caller re-baselines its watcher only on success, so a bad .spv
// retries instead of bricking the frame loop. Try-lock: a busy worker
// wins the tick, reload retries 250ms later.
bool Vk_reloadShaders(void) {
    if (s_device == VK_NULL_HANDLE || s_drawablePass == VK_NULL_HANDLE)
        return false;
    VK_LOAD_DEVICE(DeviceWaitIdle)
    VK_LOAD_DEVICE(DestroyPipeline)
    VK_LOAD_DEVICE(DestroyPipelineLayout)
    if (!SpinLock_tryLock(&s_presentLock))
        return false;
    DeviceWaitIdle_fn(s_device);
    s_pipelinesBuilt = false;
    if (s_triPipeline != VK_NULL_HANDLE) {
        DestroyPipeline_fn(s_device, s_triPipeline, nullptr);
        s_triPipeline = VK_NULL_HANDLE;
    }
    if (s_triLayout != VK_NULL_HANDLE) {
        DestroyPipelineLayout_fn(s_device, s_triLayout, nullptr);
        s_triLayout = VK_NULL_HANDLE;
    }
    if (s_quadPipeline != VK_NULL_HANDLE) {
        DestroyPipeline_fn(s_device, s_quadPipeline, nullptr);
        s_quadPipeline = VK_NULL_HANDLE;
    }
    if (s_quadLayout != VK_NULL_HANDLE) {
        DestroyPipelineLayout_fn(s_device, s_quadLayout, nullptr);
        s_quadLayout = VK_NULL_HANDLE;
    }
    if (s_texPipeline != VK_NULL_HANDLE) {
        DestroyPipeline_fn(s_device, s_texPipeline, nullptr);
        s_texPipeline = VK_NULL_HANDLE;
    }
    if (s_texLayout != VK_NULL_HANDLE) {
        DestroyPipelineLayout_fn(s_device, s_texLayout, nullptr);
        s_texLayout = VK_NULL_HANDLE;
    }
    if (s_sdfPipeline != VK_NULL_HANDLE) {
        DestroyPipeline_fn(s_device, s_sdfPipeline, nullptr);
        s_sdfPipeline = VK_NULL_HANDLE;
    }
    if (s_sdfLayout != VK_NULL_HANDLE) {
        DestroyPipelineLayout_fn(s_device, s_sdfLayout, nullptr);
        s_sdfLayout = VK_NULL_HANDLE;
    }
    bool ok = buildTriQuad(s_drawablePass);
    s_pipelinesBuilt = ok;
    SpinLock_unlock(&s_presentLock);
    fprintf(stderr, "vk: shaders reloaded (%s)\n", ok ? "ok" : "FAILED");
    return ok;
}

// Dropped-present note: every early false out of the present path means a
// dirtied tree may wait (or be cleared) without painting. Throttled to one
// line per 2s so a wedged GPU names itself instead of failing silently.
static void presentNote(const char *reason) {
    static uint64_t lastNs = 0;
    uint64_t now = NanoTime_now();
    if (lastNs != 0 && now - lastNs < 2000000000ULL)
        return;
    lastNs = now;
    fprintf(stderr, "vk: present dropped (%s)\n", reason ? reason : "?");
    fflush(stderr);
}

// Numeric-code variant: the VkResult value distinguishes device-lost from
// OOM from surface-lost on the next repro without any extra probing.
static void presentNoteCode(const char *what, int code) {
    char buf[112];
    snprintf(buf, sizeof(buf), "%s (vk %d)", what ? what : "?", code);
    presentNote(buf);
}

// Rebuild the frame fence in the SIGNALED state after a failed submit left
// it reset with no pending work. An unsignaled fence with nothing queued
// never signals again — every future WaitForFences would time out and the
// app would sit at 0fps forever. Recreating signaled restores the
// wait -> reset -> submit cadence on the very next pass (same pattern as
// the signaled creation at init). Create-first: if creation fails the old
// fence is kept (wedged, but no new crash).
static void presentRefenceSignaled(void) {
    VK_LOAD_DEVICE_VOID(DestroyFence)
    VK_LOAD_DEVICE_VOID(CreateFence)
    if (!DestroyFence_fn || !CreateFence_fn)
        return;
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fresh = VK_NULL_HANDLE;
    if (CreateFence_fn(s_device, &fci, nullptr, &fresh) != VK_SUCCESS)
        return;
    VkFence old = s_fence;
    s_fence = fresh;
    if (old != VK_NULL_HANDLE)
        DestroyFence_fn(s_device, old, nullptr);
}

// Terminal latch: the driver killed the device. Announced once, unthrottled
// (this is the end of the GPU for this process — a restart is required),
// then every present short-circuits. Rebuilds and fence recoveries are
// skipped: they cannot succeed on a lost device.
static void presentDeviceLost(const char *where) {
    if (s_deviceLost)
        return;
    s_deviceLost = true;
    snprintf(s_status, sizeof(s_status), "device lost");
    fprintf(stderr, "vk: DEVICE LOST at %s — restart required (no recovery on a lost VkDevice)\n",
            where ? where : "?");
    fflush(stderr);
}

bool Vk_isDeviceLost(void) {
    return s_deviceLost;
}

// the Ecosystem Vulkan Safety Nets Law flight probe: the present fence s_fence starts SIGNALED and is
// pending only between a successful submit and that frame's drain, so this
// non-blocking poll reports "no present CB executing" truthfully — never
// presented => idle, submit in flight/device lost => busy (defers destroys),
// frame drained => idle. Never waits, never allocates.
bool Vk_presentFlightIdle(void) {
    if (!Vk_ready())
        return true;
    if (s_deviceLost)
        return false;
    VK_LOAD_DEVICE(GetFenceStatus)
    if (!GetFenceStatus_fn)
        return true;
    return GetFenceStatus_fn(s_device, s_fence) == VK_SUCCESS;
}

bool Vk_isDebugUtilsEnabled(void) {
    return s_instanceDebugUtils;
}

bool Vk_clearPresent(void) {
    // Cold null guard (the Cold-Strict, Hot-Minimal Validation Law): silent
    // false, zero logging — teardown/inflight races retire here, never crash.
    if (s_device == VK_NULL_HANDLE || s_swapchain == VK_NULL_HANDLE)
        return false;
    // Dead device: every wait/rebuild below would burn CPU behind a lying
    // title. Short-circuit; the latch announcement already named the site.
    if (s_deviceLost)
        return false;
    // Resize-cadence calls arrive on thread 0 while the worker may be mid-
    // frame. Try-lock: the busy side wins, the other drops this tick — the
    // regular loop always carries fresher state one tick later.
    if (!SpinLock_tryLock(&s_presentLock)) {
        presentNote("present lock busy");
        return false;
    }
    bool ok = presentFrameLocked();
    SpinLock_unlock(&s_presentLock);
    return ok;
}

bool Vk_clearPresentLive(void) {
    // Same cold guards as the resting path: silent false, zero logging —
    // teardown/inflight races retire here, never crash.
    if (s_device == VK_NULL_HANDLE || s_swapchain == VK_NULL_HANDLE)
        return false;
    if (s_deviceLost)
        return false;
    // Live-drag contention: the worker may hold the present lock mid-frame
    // while thread 0 owns the modal drag step. Retry the TRY-LOCK ONLY in
    // ~1ms slices up to an ~8ms budget (half a 60Hz vsync), then drop with
    // dirty retained — the next drag step retries with fresher state. The
    // fence (100ms) and acquire (25ms) bounds inside presentFrameLocked run
    // exactly once per call, never retried (the Bounded Wait Law: bounded
    // total, no modal-loop freeze, no hot spin).
    for (int i = 0; i < 8; i++) {
        if (SpinLock_tryLock(&s_presentLock))
            break;
        if (i == 7)
            return false;
        struct timespec slice = {0, 1000000L};
        nanosleep(&slice, nullptr);
    }
    bool ok = presentFrameLocked();
    SpinLock_unlock(&s_presentLock);
    return ok;
}

static bool presentFrameTail(uint32_t imageIndex);

static bool presentFrameLocked(void) {
    // the Ecosystem Vulkan Safety Nets Law seam guard: never touch the driver on a broken health chain.
    // Debug net (NDEBUG-stripped); release ships the Cold-Strict, Hot-Minimal Validation Law hot-minimal only.
    if (!VkGuard_check("presentFrameLocked", s_device, s_queue, s_deviceLost))
        return false;
    if (!Vk_ready() || !s_pipelinesBuilt) {
        presentNote("not ready");
        return false;
    }

    // Minimize gate: the genie miniaturize/restore animation re-snapshots the
    // layer; presenting through it paints a "trail of windows" ghost trail.
    // Drop the frame (keep dirty state — the caller retries after restore),
    // never rebuild, never touch the driver while off-screen.
    if (Vk_seamIsMinimized()) {
        presentNote("minimized (present suppressed)");
        return false;
    }

    // Live-drag freeze (sticky, no churn): while the seam reports a live
    // resize, surface caps drift off the chain every step and the extent
    // block below is SKIPPED — no swapchain destroy/create, no fence-chasing
    // rebuild latency, so the presented frame can never lag one step behind
    // the edge. The pass presents the current chain TopLeft-pinned (shrink
    // drags are pixel-perfect; grow drags pin with a clear strip that fills
    // on settle) and the FIRST settled present rebuilds exactly once. The
    // render-gen block above keeps its own live gate; OUT_OF_DATE stays
    // reactive (driver truth, not churn). Fence (100ms) and acquire (25ms)
    // bounds stay (the Bounded Wait Law / the Ecosystem Vulkan Safety Nets
    // Law).

    // Retire the PREVIOUS frame through its fence BEFORE touching the chain.
    // Bounded wait: if the surface died (e.g. fullscreen close yanked the
    // drawable), the fence may never signal. Hanging here forever parks the
    // present worker inside Vk_clearPresent so pthread_join never returns and
    // teardown freezes with a ghost window. Drop the frame instead.
    VK_LOAD_DEVICE(WaitForFences)
    VK_LOAD_DEVICE(ResetCommandBuffer)
    VK_LOAD_DEVICE(AcquireNextImageKHR)
    VK_LOAD_DEVICE(GetFenceStatus)
    VK_LOAD_DEVICE(ResetFences)
    VkResult wr = WaitForFences_fn(s_device, 1, &s_fence, VK_TRUE, 100000000ULL);
    if (wr == VK_ERROR_DEVICE_LOST) {
        presentDeviceLost("fence wait");
        return false;
    }
    if (wr != VK_SUCCESS) {
        // Signal landed after the timeout expired: reset and keep the frame
        // instead of dropping work the GPU already finished.
        if (GetFenceStatus_fn && GetFenceStatus_fn(s_device, s_fence) == VK_SUCCESS) {
            if (ResetFences_fn)
                ResetFences_fn(s_device, 1, &s_fence);
        } else {
            presentNote("fence wait timeout");
            return false;
        }
    }
    ResetCommandBuffer_fn(s_cmdBuffer, 0);

    if (!s_dumpEnvRead) {
        s_dumpEnvRead = true;
        s_dumpEnabled = getenv("ANTI_VK_DUMP") != nullptr;
    }
    if (s_dumpEnabled && s_dumpStage == 1) {
        dumpWriteFile();
        s_dumpStage = 2;
    }

    // Policy drift (presentMode / transparent changed) wants a fresh chain.
    // Policy is not size: it applies off-drag. (Extent drift — the actual
    // resize signal — rebuilds in the caps block below regardless of drag.)
    if (s_window) {
        uint64_t renderGen = Vk_seamRenderGeneration();
        if (renderGen != s_appliedRenderGen && !Vk_seamIsLiveResizing() && !rebuildTargets()) {
            presentNote("render-gen rebuild failed");
            return false;
        }
    }

    // Live caps: the surface outgrowing the chain is THE resize signal.
    VK_LOAD_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR)
    static uint64_t s_lastRebuildNs = 0;
    static int s_hzInit = 0;
    static int64_t s_minRebuildGapNs = 0;
    if (!s_hzInit) {
        s_hzInit = 1;
        const char *hzEnv = getenv("ANTI_RESIZE_HZ");
        // Default 0 (immediate): rebuild the swapchain on EVERY extent
        // drift so each drag step renders and presents at the NEW size
        // (the Continuous Real-Time Live Resize Law — no dropped frames,
        // no settle-only jump). Set ANTI_RESIZE_HZ=N to cap rebuilds per
        // second (e.g. 30 for programmatic resize floods); when throttled,
        // the pass still falls through to present at the current chain
        // extent, top-left pinned, never frozen. The CAMetalLayer
        // panes never rebuild anyway (the Pane-of-Glass Law).
        int hz = hzEnv ? atoi(hzEnv) : 0;
        s_minRebuildGapNs = hz > 0 ? (int64_t)(1000000000LL / hz) : 0;
    }
    bool dragLive = Vk_seamIsLiveResizing();
    VkSurfaceCapabilitiesKHR live;
    memset(&live, 0, sizeof(live));
    if (!dragLive
        && GetPhysicalDeviceSurfaceCapabilitiesKHR_fn(s_phys, s_surface, &live) == VK_SUCCESS
        && (live.currentExtent.width != s_extent.width || live.currentExtent.height != s_extent.height)) {
        uint64_t nowNs = NanoTime_now();
        bool throttled = s_lastRebuildNs != 0 && s_minRebuildGapNs > 0
            && nowNs - s_lastRebuildNs < (uint64_t)s_minRebuildGapNs;
        if (!throttled) {
            if (!rebuildTargets()) {
                presentNote("extent rebuild failed");
                return false;
            }
            s_lastRebuildNs = NanoTime_now();
        } else {
            // ANTI_RESIZE_HZ throttle active: present at the current chain
            // extent instead of DROPPING the frame — top-left pinned, live,
            // never stretched, never frozen (the Continuous Real-Time Live
            // Resize Law). The next drift past the gap rebuilds the chain.
            presentNote("rebuild throttled (present at current extent)");
        }
    } else if (!dragLive) {
        s_lastRebuildNs = 0;
    }

    if (s_preFrameRenderer) {
        s_preFrameRenderer(s_window, (int)s_extent.width, (int)s_extent.height, s_frameRendererUserdata);
    }

    // The seam canvas is the window's PRIMARY on-screen layer (the Window
    // Compositing Layer Order Law, managed exception): it composites the two
    // retained board images (scene bottom, content top) plus any legacy
    // direct-UI paint, so it ALWAYS presents on demand — even when DIRECT
    // pane chains exist (the panes are separate CAMetalLayers above it and
    // present through VkPane_presentAll on their own demand, never replacing
    // this pass). Boards inverse: board VkLayers are retained offscreen
    // targets, NOT panes, so VkPane_count() no longer gates this chain.

    uint32_t imageIndex = 0;
    VkResult ar = AcquireNextImageKHR_fn(s_device, s_swapchain, 25000000ULL /* ~1 frame */,
                                         s_semAcquire, VK_NULL_HANDLE, &imageIndex);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!rebuildTargets()) {
            presentNote("out-of-date rebuild failed");
            return false;
        }
        ar = AcquireNextImageKHR_fn(s_device, s_swapchain, 25000000ULL,
                                    s_semAcquire, VK_NULL_HANDLE, &imageIndex);
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
            presentNote("acquire retry failed");
            return false;
        }
        return presentFrameTail(imageIndex);
    }
    if (ar == VK_TIMEOUT || ar == VK_NOT_READY) {
        presentNote("acquire timeout");
        return false;
    }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        // Hard acquire error (surface lost / OOM): the chain the fence waits
        // on may already be gone, so try one rebuild before giving up — a
        // lost surface recovers here. Device loss latches terminal above and
        // in the retry below. Bounded: exactly one rebuild + one retry; the
        // caller's dirty-retry carries fresher state next tick.
        if (ar == VK_ERROR_DEVICE_LOST) {
            presentDeviceLost("acquire");
            return false;
        }
        presentNoteCode("acquire failed", (int) ar);
        if (!rebuildTargets()) {
            presentNote("acquire rebuild failed");
            return false;
        }
        ar = AcquireNextImageKHR_fn(s_device, s_swapchain, 25000000ULL,
                                    s_semAcquire, VK_NULL_HANDLE, &imageIndex);
        if (ar == VK_ERROR_DEVICE_LOST) {
            presentDeviceLost("acquire retry");
            return false;
        }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
            presentNoteCode("acquire retry failed", (int) ar);
            return false;
        }
        return presentFrameTail(imageIndex);
    }

    return presentFrameTail(imageIndex);
}

static bool presentFrameTail(uint32_t imageIndex) {
    // the Ecosystem Vulkan Safety Nets Law seam guard at the board submit/present boundary (device + queue).
    if (!VkGuard_check("presentFrameTail", s_device, s_queue, s_deviceLost))
        return false;
    // Hot-path minimal guard (the Cold-Strict, Hot-Minimal Validation Law):
    // the acquired index comes from the driver's image count, which can
    // exceed the harness arrays if the driver hands back more images than
    // requested. Drop-degrade — never index out of bounds.
    if (imageIndex >= s_swapchainImageCount)
        return false;
    VK_LOAD_DEVICE(BeginCommandBuffer)
    VK_LOAD_DEVICE(EndCommandBuffer)
    VK_LOAD_DEVICE(CmdBeginRenderPass)
    VK_LOAD_DEVICE(CmdEndRenderPass)
    VK_LOAD_DEVICE(CmdBindPipeline)
    VK_LOAD_DEVICE(CmdPushConstants)
    VK_LOAD_DEVICE(CmdSetViewport)
    VK_LOAD_DEVICE(CmdSetScissor)
    VK_LOAD_DEVICE(CmdDraw)
    VK_LOAD_DEVICE(CmdClearColorImage)
    VK_LOAD_DEVICE(CmdPipelineBarrier)
    VK_LOAD_DEVICE(QueueSubmit)
    VK_LOAD_DEVICE(ResetFences)
    VK_LOAD_DEVICE(QueuePresentKHR)

    VkCommandBufferBeginInfo bbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginCommandBuffer_fn(s_cmdBuffer, &bbi);

    // 1. Prep drawable layout: walk to TRANSFER_DST and clear
    VkImageMemoryBarrier toPrep = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPrep.srcAccessMask = 0;
    toPrep.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPrep.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toPrep.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPrep.image = s_swapchainImages[imageIndex];
    toPrep.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPrep.subresourceRange.levelCount = 1;
    toPrep.subresourceRange.layerCount = 1;
    CmdPipelineBarrier_fn(s_cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPrep);

    {
        VkClearValue cc = {0};
        cc.color.float32[0] = s_clearColor[0];
        cc.color.float32[1] = s_clearColor[1];
        cc.color.float32[2] = s_clearColor[2];
        cc.color.float32[3] = s_clearColor[3];
        VkImageSubresourceRange rng = {0};
        rng.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rng.levelCount = 1;
        rng.layerCount = 1;
        CmdClearColorImage_fn(s_cmdBuffer, s_swapchainImages[imageIndex],
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              &cc.color, 1, &rng);
    }

    // 2. Render pass: invoke frame renderer hook (clear-only when none installed)
    {
        VkRenderPassBeginInfo rbi2 = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rbi2.renderPass = s_drawablePass;
        rbi2.framebuffer = s_swapchainFbs[imageIndex];
        rbi2.renderArea.offset = (VkOffset2D){0, 0};
        rbi2.renderArea.extent = s_extent;
        CmdBeginRenderPass_fn(s_cmdBuffer, &rbi2, VK_SUBPASS_CONTENTS_INLINE);
    }

    if (s_frameRenderer)
        s_frameRenderer(s_cmdBuffer, (int)s_extent.width, (int)s_extent.height, s_frameRendererUserdata);

    CmdEndRenderPass_fn(s_cmdBuffer);

    if (s_dumpEnabled && s_dumpStage == 0) {
        if (dumpAllocStage(s_extent.width, s_extent.height)) {
            dumpRecordCopy(s_swapchainImages[imageIndex], s_cmdBuffer);
            s_dumpStage = 1;
        } else {
            s_dumpStage = 2;
        }
    }

    // 4. Walk TRANSFER_SRC -> PRESENT_SRC
    VkImageMemoryBarrier toPresent = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image = s_swapchainImages[imageIndex];
    toPresent.subresourceRange = toPrep.subresourceRange;
    CmdPipelineBarrier_fn(s_cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    EndCommandBuffer_fn(s_cmdBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &s_semAcquire;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cmdBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &s_semRender;
    ResetFences_fn(s_device, 1, &s_fence);
    VkResult sr = QueueSubmit_fn(s_queue, 1, &si, s_fence);
    if (sr == VK_ERROR_DEVICE_LOST) {
        presentDeviceLost("submit");
        return false;
    }
    if (sr != VK_SUCCESS) {
        // A failed submit leaves the just-reset fence with no pending work:
        // it would never signal again and every future frame would die at
        // the fence wait (permanent 0fps). Recreate it signaled so the next
        // pass restores the cadence; skip the present (nothing was queued,
        // so s_semRender never signals).
        presentNoteCode("submit failed", (int) sr);
        presentRefenceSignaled();
        return false;
    }

    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &s_semRender;
    pi.swapchainCount = 1;
    pi.pSwapchains = &s_swapchain;
    pi.pImageIndices = &imageIndex;
    VkResult pr = QueuePresentKHR_fn(s_queue, &pi);
    if (pr == VK_ERROR_DEVICE_LOST) {
        presentDeviceLost("present");
        return false;
    }
    // Present-time OUT_OF_DATE: the chain this frame just painted is stale —
    // rebuild once so the next tick acquires fresh (the false return keeps
    // demand armed and the caller retries; exactly one rebuild, no spin).
    // SUBOPTIMAL presented a valid image: success, no rebuild.
    if (pr == VK_ERROR_OUT_OF_DATE_KHR) {
        s_appliedRenderGen = 0;
        rebuildTargets();
        return false;
    }
    if (pr == VK_SUBOPTIMAL_KHR)
        return true;
    if (pr != VK_SUCCESS)
        presentNoteCode("present failed", (int) pr);
    return pr == VK_SUCCESS;
}

void Vk_drawTexture(void *cmdBuffer, float surfaceW, float surfaceH,
                    float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int32_t textureId,
                    PictureMode mode,
                    float imgW, float imgH) {
    if (!cmdBuffer) return;
    // the Ecosystem Vulkan Safety Nets Law hot-minimal clamp: an OOB bindless id (-1 -> 0xFFFFFFFF) reaches
    // u_textures[nonuniformEXT(...)] unclamped and faults the GPU. Skip the quad.
    extern int32_t Texture_maxBoundId(void);
    if (textureId < 0 || (uint32_t)textureId >= (uint32_t)Texture_maxBoundId())
        return;

    // Lazily build the texture pipeline on first call — must use the IOSurface
    // renderpass (BGRA8), which is itself lazy and not available inside buildPipelines().
    if (s_texPipeline == VK_NULL_HANDLE) {
        extern bool VkMac_ensureIOSurfacePass(void);
        extern VkRenderPass VkMac_getIOSurfacePass(void);
        if (!VkMac_ensureIOSurfacePass()) return;
        VkRenderPass iosurfPass = VkMac_getIOSurfacePass();

        VK_LOAD_DEVICE_VOID(CreatePipelineLayout)
        VK_LOAD_DEVICE_VOID(CreateGraphicsPipelines)

        // Vertex: offset=0 size=16 (rectNdc)
        // Fragment: offset=16 size=48 (color[16] + texId[4] + pad[4] + imgSize[8] + quadSize[8] + mode[4] + fillParam[4])
        VkPushConstantRange texPush[2];
        texPush[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        texPush[0].offset = 0;
        texPush[0].size = 16;
        texPush[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        texPush[1].offset = 16;
        texPush[1].size = 48;

        extern void *Texture_getDescriptorSetLayout(void);
        VkDescriptorSetLayout bindlessLayout = (VkDescriptorSetLayout)Texture_getDescriptorSetLayout();

        VkPipelineLayoutCreateInfo tlci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        tlci.pushConstantRangeCount = 2;
        tlci.pPushConstantRanges = texPush;
        tlci.setLayoutCount = 1;
        tlci.pSetLayouts = &bindlessLayout;
        if (CreatePipelineLayout_fn(s_device, &tlci, nullptr, &s_texLayout) != VK_SUCCESS) return;

        VkShaderModule texVert = createShaderModule("texture_quad_vert.spv", nullptr);
        VkShaderModule texFrag = createShaderModule("texture_quad_frag.spv", nullptr);
        if (texVert == VK_NULL_HANDLE || texFrag == VK_NULL_HANDLE) return;

        // Re-use the same fixed-function skeleton as the quad pipeline
        VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAtt = {0};
        blendAtt.colorWriteMask = 0xF;
        blendAtt.blendEnable = VK_TRUE;
        blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
        blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb2 = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb2.attachmentCount = 1; cb2.pAttachments = &blendAtt;
        VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        ds.dynamicStateCount = 2; ds.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo tstages[2] = {{0},{0}};
        tstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; tstages[0].module = texVert; tstages[0].pName = "main";
        tstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; tstages[1].module = texFrag; tstages[1].pName = "main";

        VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gpci.stageCount = 2; gpci.pStages = tstages;
        gpci.pVertexInputState = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb2;
        gpci.pDynamicState = &ds;
        gpci.layout = s_texLayout;
        gpci.renderPass = iosurfPass;  // BGRA8, matches IOSurface panels
        gpci.subpass = 0;

        if (CreateGraphicsPipelines_fn(s_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &s_texPipeline) != VK_SUCCESS) {
            s_texPipeline = VK_NULL_HANDLE;
            return;
        }
        printf("vk: texture pipeline built (IOSurface pass)\n");
    }

    VkCommandBuffer cb = (VkCommandBuffer)cmdBuffer;

    VK_LOAD_DEVICE_VOID(CmdBindPipeline)
    VK_LOAD_DEVICE_VOID(CmdPushConstants)
    VK_LOAD_DEVICE_VOID(CmdDraw)
    VK_LOAD_DEVICE_VOID(CmdBindDescriptorSets)
    VK_LOAD_DEVICE_VOID(CmdSetViewport)
    VK_LOAD_DEVICE_VOID(CmdSetScissor)

    CmdBindPipeline_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_texPipeline);

    float scLeft = x < 0.0f ? 0.0f : x;
    float scRight = (x + w > surfaceW) ? surfaceW : (x + w);
    float scBottom = y < 0.0f ? 0.0f : y;
    float scTop = (y + h > surfaceH) ? surfaceH : (y + h);

    if (scRight <= scLeft || scTop <= scBottom)
        return;

    VkViewport viewport = { .x = 0.0f, .y = surfaceH, .width = surfaceW, .height = -surfaceH, .maxDepth = 1.0f };
    VkRect2D scissor = {
        .offset.x = (int32_t) scLeft,
        .offset.y = (int32_t) (surfaceH - scTop),
        .extent.width = (uint32_t) (scRight - scLeft),
        .extent.height = (uint32_t) (scTop - scBottom)
    };
    CmdSetViewport_fn(cb, 0, 1, &viewport);
    CmdSetScissor_fn(cb, 0, 1, &scissor);

    extern void *Texture_getDescriptorSet(void);
    VkDescriptorSet bindlessSet = (VkDescriptorSet) Texture_getDescriptorSet();
    if (bindlessSet != VK_NULL_HANDLE && CmdBindDescriptorSets_fn)
        CmdBindDescriptorSets_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_texLayout,
                                 0, 1, &bindlessSet, 0, nullptr);

    if (!CmdPushConstants_fn || !CmdDraw_fn)
        return;

    struct {
        float    rect[4];       // offset 0
        float    color[4];      // offset 16
        uint32_t texId;         // offset 32
        uint32_t pad;           // offset 36
        float    imgSize[2];    // offset 40
        float    quadSize[2];   // offset 48
        uint32_t mode;          // offset 56
    } push;

    push.rect[0] = (x / surfaceW) * 2.0f - 1.0f;
    push.rect[1] = (y / surfaceH) * 2.0f - 1.0f;
    push.rect[2] = (w / surfaceW) * 2.0f;
    push.rect[3] = (h / surfaceH) * 2.0f;
    push.color[0] = r; push.color[1] = g; push.color[2] = b; push.color[3] = a;
    push.texId      = (uint32_t)textureId;
    push.pad        = 0;
    push.imgSize[0] = imgW;
    push.imgSize[1] = imgH;
    push.quadSize[0] = w;
    push.quadSize[1] = h;
    push.mode       = (uint32_t)mode;

    CmdPushConstants_fn(cb, s_texLayout, VK_SHADER_STAGE_VERTEX_BIT,   0,  16, push.rect);
    CmdPushConstants_fn(cb, s_texLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 16, 44, push.color);

    CmdDraw_fn(cb, 6, 1, 0, 0);
}

static bool ensureSdfPipeline(void) {
    if (s_sdfPipeline != VK_NULL_HANDLE) return true;
    if (s_device == VK_NULL_HANDLE) return false;

    extern void *Texture_getDescriptorSetLayout(void);
    VkDescriptorSetLayout setLayout = (VkDescriptorSetLayout) Texture_getDescriptorSetLayout();
    if (setLayout == VK_NULL_HANDLE) return false;

    VkShaderModule sdfVertMod = createShaderModule("text_sdf_vert.spv", nullptr);
    VkShaderModule sdfFragMod = createShaderModule("text_sdf_frag.spv", nullptr);
    if (sdfVertMod == VK_NULL_HANDLE || sdfFragMod == VK_NULL_HANDLE) return false;

    VK_LOAD_DEVICE(CreatePipelineLayout)
    VK_LOAD_DEVICE(CreateGraphicsPipelines)

    VkPushConstantRange sdfPcr = {0};
    sdfPcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    sdfPcr.offset = 0;
    sdfPcr.size = 64;

    VkPipelineLayoutCreateInfo sdfLci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    sdfLci.pushConstantRangeCount = 1;
    sdfLci.pPushConstantRanges = &sdfPcr;
    sdfLci.setLayoutCount = 1;
    sdfLci.pSetLayouts = &setLayout;

    if (CreatePipelineLayout_fn(s_device, &sdfLci, nullptr, &s_sdfLayout) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo sdfStages[2] = {{0}, {0}};
    sdfStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sdfStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    sdfStages[0].module = sdfVertMod;
    sdfStages[0].pName = "main";
    sdfStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sdfStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    sdfStages[1].module = sdfFragMod;
    sdfStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState sdfBlend = {0};
    sdfBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    sdfBlend.blendEnable = VK_TRUE;
    sdfBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    sdfBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    sdfBlend.colorBlendOp = VK_BLEND_OP_ADD;
    sdfBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    sdfBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    sdfBlend.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo sdfCb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    sdfCb.attachmentCount = 1;
    sdfCb.pAttachments = &sdfBlend;

    VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    VkRenderPass targetPass = s_drawablePass;
    if (targetPass == VK_NULL_HANDLE) {
        extern bool VkMac_ensureIOSurfacePass(void);
        extern VkRenderPass VkMac_getIOSurfacePass(void);
        if (VkMac_ensureIOSurfacePass()) {
            targetPass = VkMac_getIOSurfacePass();
        }
    }

    VkGraphicsPipelineCreateInfo sdfPci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    sdfPci.stageCount = 2;
    sdfPci.pStages = sdfStages;
    sdfPci.pVertexInputState = &vi;
    sdfPci.pInputAssemblyState = &ia;
    sdfPci.pViewportState = &vp;
    sdfPci.pRasterizationState = &rs;
    sdfPci.pMultisampleState = &ms;
    sdfPci.pColorBlendState = &sdfCb;
    sdfPci.pDynamicState = &ds;
    sdfPci.layout = s_sdfLayout;
    sdfPci.renderPass = targetPass;
    sdfPci.subpass = 0;

    if (CreateGraphicsPipelines_fn(s_device, VK_NULL_HANDLE, 1, &sdfPci, nullptr, &s_sdfPipeline) != VK_SUCCESS) {
        s_sdfPipeline = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void Vk_drawSDFText(void *cmdBuffer, float surfaceW, float surfaceH,
                    float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int32_t textureId, float bold, float smoothness,
                    float u0, float v0, float u1, float v1) {
    if (!ensureSdfPipeline()) return;
    // the Ecosystem Vulkan Safety Nets Law hot-minimal clamp: an OOB bindless id (-1 -> 0xFFFFFFFF) reaches
    // u_textures[nonuniformEXT(...)] unclamped and faults the GPU. Skip the quad.
    extern int32_t Texture_maxBoundId(void);
    if (textureId < 0 || (uint32_t)textureId >= (uint32_t)Texture_maxBoundId())
        return;
    VkCommandBuffer cb = (VkCommandBuffer) cmdBuffer;

    VK_LOAD_DEVICE_VOID(CmdBindPipeline)
    VK_LOAD_DEVICE_VOID(CmdPushConstants)
    VK_LOAD_DEVICE_VOID(CmdDraw)
    VK_LOAD_DEVICE_VOID(CmdSetViewport)
    VK_LOAD_DEVICE_VOID(CmdSetScissor)
    VK_LOAD_DEVICE_VOID(CmdBindDescriptorSets)

    CmdBindPipeline_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_sdfPipeline);

    extern void *Texture_getDescriptorSet(void);
    VkDescriptorSet bindlessSet = (VkDescriptorSet)Texture_getDescriptorSet();
    CmdBindDescriptorSets_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_sdfLayout, 0, 1, &bindlessSet, 0, nullptr);

    float px = x, py = y;
    float pw = w, ph = h;
    float dx = (px / surfaceW) * 2.0f - 1.0f;
    float dy = (py / surfaceH) * 2.0f - 1.0f;
    float dw = (pw / surfaceW) * 2.0f;
    float dh = (ph / surfaceH) * 2.0f;

    struct {
        float x, y, w, h;
        float cr, cg, cb, ca;
        uint32_t texId;
        float bold;
        float smoothness;
        float pad;
        float u0, v0, u1, v1;
    } pc;
    pc.x = dx; pc.y = dy; pc.w = dw; pc.h = dh;
    pc.cr = r; pc.cg = g; pc.cb = b; pc.ca = a;
    pc.texId = (uint32_t)textureId;
    pc.bold = bold;
    pc.smoothness = smoothness;
    pc.pad = 0;
    pc.u0 = u0; pc.v0 = v0; pc.u1 = u1; pc.v1 = v1;

    CmdPushConstants_fn(cb, s_sdfLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    VkViewport vp = {0, 0, surfaceW, surfaceH, 0.0f, 1.0f};
    CmdSetViewport_fn(cb, 0, 1, &vp);

    VkRect2D sc;
    sc.offset.x = (int32_t)(px > 0 ? px : 0);
    sc.offset.y = (int32_t)(py > 0 ? py : 0);
    int32_t scRight = (int32_t)(px + pw);
    int32_t scBottom = (int32_t)(py + ph);
    if (scRight > (int32_t)surfaceW) scRight = (int32_t)surfaceW;
    if (scBottom > (int32_t)surfaceH) scBottom = (int32_t)surfaceH;
    sc.extent.width = scRight > sc.offset.x ? scRight - sc.offset.x : 0;
    sc.extent.height = scBottom > sc.offset.y ? scBottom - sc.offset.y : 0;
    if (sc.extent.width == 0 || sc.extent.height == 0) return;
    CmdSetScissor_fn(cb, 0, 1, &sc);

    CmdDraw_fn(cb, 6, 1, 0, 0);
}

void Vk_drawColorGlyph(void *cmdBuffer, float surfaceW, float surfaceH,
                       float x, float y, float w, float h, float alpha,
                       int32_t textureId,
                       float u0, float v0, float u1, float v1) {
    if (!ensureSdfPipeline()) return;
    // the Ecosystem Vulkan Safety Nets Law hot-minimal clamp (see Vk_drawSDFText): never let an OOB id reach
    // the bindless sampler.
    extern int32_t Texture_maxBoundId(void);
    if (textureId < 0 || (uint32_t)textureId >= (uint32_t)Texture_maxBoundId())
        return;
    VkCommandBuffer cb = (VkCommandBuffer) cmdBuffer;

    VK_LOAD_DEVICE_VOID(CmdBindPipeline)
    VK_LOAD_DEVICE_VOID(CmdPushConstants)
    VK_LOAD_DEVICE_VOID(CmdDraw)
    VK_LOAD_DEVICE_VOID(CmdSetViewport)
    VK_LOAD_DEVICE_VOID(CmdSetScissor)
    VK_LOAD_DEVICE_VOID(CmdBindDescriptorSets)

    CmdBindPipeline_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_sdfPipeline);

    extern void *Texture_getDescriptorSet(void);
    VkDescriptorSet bindlessSet = (VkDescriptorSet) Texture_getDescriptorSet();
    CmdBindDescriptorSets_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_sdfLayout, 0, 1, &bindlessSet, 0, nullptr);

    float px = x, py = y;
    float pw = w, ph = h;
    float dx = (px / surfaceW) * 2.0f - 1.0f;
    float dy = (py / surfaceH) * 2.0f - 1.0f;
    float dw = (pw / surfaceW) * 2.0f;
    float dh = (ph / surfaceH) * 2.0f;

    struct {
        float x, y, w, h;
        float cr, cg, cb, ca;
        uint32_t texId;
        float bold;
        float smoothness;
        float pad;
        float u0, v0, u1, v1;
    } pc;
    pc.x = dx; pc.y = dy; pc.w = dw; pc.h = dh;
    pc.cr = 1.0f; pc.cg = 1.0f; pc.cb = 1.0f; pc.ca = alpha;
    pc.texId = (uint32_t)textureId;
    pc.bold = 0.0f;
    pc.smoothness = -1.0f; // color-glyph sentinel: raw RGBA branch in text_sdf.frag
    pc.pad = 0;
    pc.u0 = u0; pc.v0 = v0; pc.u1 = u1; pc.v1 = v1;

    CmdPushConstants_fn(cb, s_sdfLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    VkViewport vp = {0, 0, surfaceW, surfaceH, 0.0f, 1.0f};
    CmdSetViewport_fn(cb, 0, 1, &vp);

    VkRect2D sc;
    sc.offset.x = (int32_t)(px > 0 ? px : 0);
    sc.offset.y = (int32_t)(py > 0 ? py : 0);
    int32_t scRight = (int32_t)(px + pw);
    int32_t scBottom = (int32_t)(py + ph);
    if (scRight > (int32_t)surfaceW) scRight = (int32_t)surfaceW;
    if (scBottom > (int32_t)surfaceH) scBottom = (int32_t)surfaceH;
    sc.extent.width = scRight > sc.offset.x ? scRight - sc.offset.x : 0;
    sc.extent.height = scBottom > sc.offset.y ? scBottom - sc.offset.y : 0;
    if (sc.extent.width == 0 || sc.extent.height == 0) return;
    CmdSetScissor_fn(cb, 0, 1, &sc);

    CmdDraw_fn(cb, 6, 1, 0, 0);
}
