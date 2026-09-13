#define VK_USE_PLATFORM_MACOS_MVK
#define VK_USE_PLATFORM_METAL_EXT

#include "vulkan/vk_mac.h"
#include "vulkan/vk.h"
#include "vulkan/vk_window_seam.h"
#include <vulkan/vulkan_core.h>
#include <stdlib.h> // setenv (Debug build MoltenVK env vars)
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include "time/nanotime.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Vulkan_mac (vulkan/vulkan_mac.c)
 * LEVEL: L4 — Self-Management (MoltenVK loader and surface setup)
 * ============================================================================
  * macOS-specific Vulkan backend functions.
  *
  * STRUCT FIELDS: none — procedural/stateless (operates on vulkan.c chain state via Vk_get* accessors)
  *
  * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - VkMac_loadLib(void)
 *   - VkMac_createSurface(window, instance, gpa, outSurface)
 *   - VkMac_createSurfaceForLayer(layer, instance, gpa, outSurface)
 *   - VkMac_ensureIOSurfacePass(void)
 *   - VkMac_resizeRenderTrampoline(userdata)
 *
 * Getters:
 *   - Vk_getDevice(void)
 *   - Vk_getQueue(void)
 *   - Vk_getCmdBuffer(void)
 *   - Vk_getTriPipeline(void)
 *   - Vk_getTriLayout(void)
 *   - Vk_getAnimStartNanos(void)
 *   - Vk_getGdpa(void)
 *   - Vk_getInstance(void)
 *   - Vk_getGpa(void)
 *   - Vk_getPhys(void)
 *   - Vk_getQueueFamily(void)
 *   - VkMac_getIOSurfacePass(void)
 * ============================================================================
 */


// Load a Vulkan device function pointer (void return, no error check).
#define MAC_LOAD_DEVICE_VOID(name) \
    static PFN_vk##name name##_fn; \
    name##_fn = (PFN_vk##name)Vk_getGdpa()(Vk_getDevice(), "vk" #name);

// vulkan/vulkan_mac.c — macOS-specific Vulkan backend.
;;PLATFORM_EXCLUSIVE("Mac")
;;INTENTION("MoltenVK loader, CAMetalLayer surface creation, and device accessors for macOS.")

// Cross-platform state accessors (owned by vulkan.c).
extern VkDevice s_instanceDevice;
extern VkQueue s_instanceQueue;
extern VkCommandBuffer s_instanceCmdBuffer;
extern VkPipeline s_instanceTriPipeline;
extern VkPipelineLayout s_instanceTriLayout;
extern uint64_t s_instanceAnimStartNanos;
extern PFN_vkGetDeviceProcAddr s_instanceGdpa;
extern VkInstance s_instanceInstance;
extern PFN_vkGetInstanceProcAddr s_instanceGpa;
extern VkPhysicalDevice s_instancePhys;
extern uint32_t s_instanceQueueFamily;

VkDevice Vk_getDevice(void) { return s_instanceDevice; }
VkQueue Vk_getQueue(void) { return s_instanceQueue; }
VkCommandBuffer Vk_getCmdBuffer(void) { return s_instanceCmdBuffer; }
VkPipeline Vk_getTriPipeline(void) { return s_instanceTriPipeline; }
VkPipelineLayout Vk_getTriLayout(void) { return s_instanceTriLayout; }
uint64_t Vk_getAnimStartNanos(void) { return s_instanceAnimStartNanos; }
PFN_vkGetDeviceProcAddr Vk_getGdpa(void) { return s_instanceGdpa; }
VkInstance Vk_getInstance(void) { return s_instanceInstance; }
PFN_vkGetInstanceProcAddr Vk_getGpa(void) { return s_instanceGpa; }
VkPhysicalDevice Vk_getPhys(void) { return s_instancePhys; }
uint32_t Vk_getQueueFamily(void) { return s_instanceQueueFamily; }

static VkRenderPass s_iosurfacePass = VK_NULL_HANDLE;

// Load the Vulkan loader library (MoltenVK on macOS, Khronos loader fallback).
void *VkMac_loadLib(void) {
    // === Rule 39: MoltenVK debug config in Debug builds ===
#if defined(DEBUG) || defined(_DEBUG)
    // MVK_CONFIG_API_DEBUG: MoltenVK emits debug callbacks (vkDebugUtilsMessenger)
    // MVK_CONFIG_LOG_LEVEL: 3 = verbose (trace every Vulkan -> Metal translation)
    // MVK_CONFIG_TRACE_VULKAN_CALLS: logs every Vulkan entry/exit
    setenv("MVK_CONFIG_API_DEBUG", "1", 0);
    setenv("MVK_CONFIG_LOG_LEVEL", "3", 0);
    setenv("MVK_CONFIG_TRACE_VULKAN_CALLS", "1", 0);
    setenv("MVK_CONFIG_PREFETCH_TEXTURE_SAMPLERS", "0", 0); // surface every sampler bind
    setenv("VK_LAYER_ENABLES", "VK_LAYER_KHRONOS_validation", 0);
    fprintf(stderr, "vk: MoltenVK debug env vars set (Debug build)\n");
#endif

    // MoltenVK first: the ICD exports everything itself, no loader manifest
    // needed. The Khronos loader stays as fallback for manifest setups.
    const char *candidates[] = {
        "libMoltenVK.dylib",
        "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libMoltenVK.dylib",
        "libvulkan.dylib",
        "/opt/homebrew/lib/libvulkan.dylib",
        "/usr/local/lib/libvulkan.dylib",
        nullptr,
    };
    for (int i = 0; candidates[i]; i++) {
        void *lib = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (lib)
            return lib;
    }
    return nullptr;
}

// Create a VkSurfaceKHR from the window's CAMetalLayer.
bool VkMac_createSurface(void *window, VkInstance instance,
                         PFN_vkGetInstanceProcAddr gpa, VkSurfaceKHR *outSurface) {
    if (!window || !instance || !gpa || !outSurface)
        return false;

    void *metalLayer = Vk_seamMetalLayer();
    return VkMac_createSurfaceForLayer(metalLayer, instance, gpa, outSurface);
}

// Create a VkSurfaceKHR from ANY CAMetalLayer ("pane of glass" host).
bool VkMac_createSurfaceForLayer(void *layer, VkInstance instance,
                                 PFN_vkGetInstanceProcAddr gpa, VkSurfaceKHR *outSurface) {
    if (!layer || !instance || !gpa || !outSurface)
        return false;

    PFN_vkCreateMetalSurfaceEXT CreateMetalSurfaceEXT_fn =
        (PFN_vkCreateMetalSurfaceEXT)gpa(instance, "vkCreateMetalSurfaceEXT");
    if (!CreateMetalSurfaceEXT_fn)
        return false;

    VkMetalSurfaceCreateInfoEXT sci = { .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
    sci.pLayer = layer;
    VkResult sr = CreateMetalSurfaceEXT_fn(instance, &sci, nullptr, outSurface);
    if (sr != VK_SUCCESS)
        return false;

    return true;
}

// Ensure the BGRA8 offscreen/IOSurface render pass exists.
bool VkMac_ensureIOSurfacePass(void) {
    if (s_iosurfacePass != VK_NULL_HANDLE)
        return true;

    VkDevice dev = Vk_getDevice();
    MAC_LOAD_DEVICE(CreateRenderPass);

    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_B8G8R8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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

    if (CreateRenderPass_fn(dev, &rpci, nullptr, &s_iosurfacePass) != VK_SUCCESS)
        return false;

    return true;
}

VkRenderPass VkMac_getIOSurfacePass(void) {
    return s_iosurfacePass;
}

// Resize render trampoline: attempts a synchronized present on thread 0 during OS resize.
void VkMac_resizeRenderTrampoline(void *userdata) {
    (void) userdata;
    Vk_clearPresent();
}
