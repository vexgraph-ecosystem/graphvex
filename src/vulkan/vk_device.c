#include "vulkan/vk_device.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VkDevice (vulkan/vk_device.c)
 * ============================================================================
 * The Vulkan dialect of the Device contract — the first real device of the
 * language. It dlopens MoltenVK (falling back to the Khronos loader), creates a
 * VkInstance with only the extensions the driver actually exposes, picks the
 * first physical device, and creates one logical device + graphics queue.
 *
 * This slice is DEVICE-ONLY by design: no surface, no swapchain, no render. The
 * seam Surface and the master renderer (vulkan/vk_render.c) are built on top in
 * the next slices, so the device is provable in isolation first.
 *
 * The state is dialect-private (VkInstance/VkPhysicalDevice/VkDevice/VkQueue);
 * callers only ever see the opaque Device. present/resize are cold-false until
 * the surface lands (the Cold-Strict, Hot-Minimal Validation Law).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkDevice (vulkan/vk_device.c)
 * LEVEL: L4 — Self-Management (owns the Vulkan device across the process)
 * ============================================================================
 * SUMMARY:
 *   Vulkan DeviceRow. createState loads the loader, builds instance -> physical
 *   device -> logical device -> queue; destroyState tears them down top-down.
 *
 * STRUCT FIELDS: none — the state is the private VkDeviceState helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   VkDeviceState
 *     void *lib;               // dlopen'd loader handle
 *     PFN_vkGetInstanceProcAddr gpa;
 *     VkInstance instance;
 *     VkPhysicalDevice phys;
 *     VkDevice device;
 *     VkQueue queue;
 *     uint32_t queueFamily;
 *     uint32_t width, height;  // requested native px (0 = offscreen)
 *     bool ready;
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - vkLoadLib(void)                : dlopen MoltenVK / Khronos loader
 *   - vkCreateState(desc) / vkDestroyState(state)
 *   - vkPresent(state) / vkResize(state, w, h)
 *   - vkWidth(state) / vkHeight(state) / vkIsReady(state) / vkNative(state)
 * ============================================================================
 */

// SLOT RECORD state for LANG_BACKEND_VULKAN (owned by the row's createState).
typedef struct VkDeviceState {
    void *lib;                          // dlopen'd loader handle
    PFN_vkGetInstanceProcAddr gpa;
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamily;
    uint32_t width;                     // requested native px (0 = offscreen)
    uint32_t height;
    bool ready;
} VkDeviceState;

#define VK_DEV_MAX_EXT 64

// dlopen MoltenVK first (the ICD exports everything itself), Khronos loader as
// fallback for manifest setups. Mirrors the old reference's candidate list.
static void *vkLoadLib(void) {
    const char *candidates[] = {
        "libMoltenVK.dylib",
        "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libMoltenVK.dylib",
        "libvulkan.dylib",
        "/opt/homebrew/lib/libvulkan.dylib",
        "/usr/local/lib/libvulkan.dylib",
        nullptr,
    };
    for (int i = 0; candidates[i] != nullptr; i++) {
        void *lib = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (lib != nullptr)
            return lib;
    }
    return nullptr;
}

static void *vkCreateState(const DeviceDesc *desc) {
    VkDeviceState *state = (VkDeviceState*) calloc(1, sizeof(VkDeviceState));
    if (state == nullptr)
        return nullptr;
    if (desc != nullptr) {
        (*state).width = (*desc).width;
        (*state).height = (*desc).height;
    }

    // 1. loader + gpa
    (*state).lib = vkLoadLib();
    if ((*state).lib == nullptr) {
        fprintf(stderr, "vk_device: no loader dylib\n");
        free(state);
        return nullptr;
    }
    (*state).gpa = (PFN_vkGetInstanceProcAddr) dlsym((*state).lib, "vkGetInstanceProcAddr");
    if ((*state).gpa == nullptr) {
        fprintf(stderr, "vk_device: no vkGetInstanceProcAddr\n");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance) (*state).gpa(VK_NULL_HANDLE, "vkCreateInstance");
    PFN_vkEnumerateInstanceExtensionProperties enumExts =
        (PFN_vkEnumerateInstanceExtensionProperties) (*state).gpa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (createInstance == nullptr || enumExts == nullptr) {
        fprintf(stderr, "vk_device: global entry points missing\n");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }

    // 2. instance — request only the extensions the driver exposes.
    uint32_t extCount = 0;
    enumExts(nullptr, &extCount, nullptr);
    VkExtensionProperties props[VK_DEV_MAX_EXT];
    if (extCount > VK_DEV_MAX_EXT)
        extCount = VK_DEV_MAX_EXT;
    enumExts(nullptr, &extCount, props);

    const char *exts[4];
    uint32_t n = 0;
    for (uint32_t i = 0; i < extCount; i++) {
        if (strcmp(props[i].extensionName, "VK_KHR_surface") == 0)
            exts[n++] = "VK_KHR_surface";
        else if (strcmp(props[i].extensionName, "VK_EXT_metal_surface") == 0)
            exts[n++] = "VK_EXT_metal_surface";
        else if (strcmp(props[i].extensionName, "VK_KHR_portability_enumeration") == 0)
            exts[n++] = "VK_KHR_portability_enumeration";
        else if (strcmp(props[i].extensionName, "VK_EXT_debug_utils") == 0)
            exts[n++] = "VK_EXT_debug_utils";
    }

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vex";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = n;
    ici.ppEnabledExtensionNames = exts;
    ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    if (createInstance(&ici, nullptr, &(*state).instance) != VK_SUCCESS) {
        fprintf(stderr, "vk_device: instance create failed\n");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }

    // 3. physical device (MoltenVK exposes one).
    PFN_vkEnumeratePhysicalDevices enumPhys =
        (PFN_vkEnumeratePhysicalDevices) (*state).gpa((*state).instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getFamilies =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties) (*state).gpa((*state).instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice createDevice =
        (PFN_vkCreateDevice) (*state).gpa((*state).instance, "vkCreateDevice");
    if (enumPhys == nullptr || getFamilies == nullptr || createDevice == nullptr) {
        fprintf(stderr, "vk_device: instance entry points missing\n");
        (*state).gpa((*state).instance, "vkDestroyInstance");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }

    uint32_t physCount = 0;
    if (enumPhys((*state).instance, &physCount, nullptr) != VK_SUCCESS || physCount == 0) {
        fprintf(stderr, "vk_device: no physical devices\n");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }
    VkPhysicalDevice phys[8];
    if (physCount > 8)
        physCount = 8;
    enumPhys((*state).instance, &physCount, phys);
    (*state).phys = phys[0];

    uint32_t familyCount = 0;
    getFamilies((*state).phys, &familyCount, nullptr);
    VkQueueFamilyProperties families[16];
    if (familyCount > 16)
        familyCount = 16;
    getFamilies((*state).phys, &familyCount, families);
    bool found = false;
    for (uint32_t f = 0; f < familyCount; f++) {
        if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            (*state).queueFamily = f;
            found = true;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "vk_device: no graphics queue family\n");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }

    // 4. logical device + queue.
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = (*state).queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[2];
    uint32_t nDev = 0;
    devExts[nDev++] = "VK_KHR_swapchain";
    devExts[nDev++] = "VK_EXT_metal_objects";

    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = nDev;
    dci.ppEnabledExtensionNames = devExts;

    if (createDevice((*state).phys, &dci, nullptr, &(*state).device) != VK_SUCCESS) {
        fprintf(stderr, "vk_device: logical device create failed\n");
        dlclose((*state).lib);
        free(state);
        return nullptr;
    }
    PFN_vkGetDeviceQueue getQueue =
        (PFN_vkGetDeviceQueue) (*state).gpa((*state).instance, "vkGetDeviceQueue");
    if (getQueue != nullptr)
        getQueue((*state).device, (*state).queueFamily, 0, &(*state).queue);

    (*state).ready = true;
    return state;
}

static void vkDestroyState(void *state) {
    if (state == nullptr)
        return;
    VkDeviceState *s = (VkDeviceState*) state;
    if ((*s).instance != VK_NULL_HANDLE && (*s).gpa != nullptr) {
        PFN_vkDestroyDevice destroyDevice =
            (PFN_vkDestroyDevice) (*s).gpa((*s).instance, "vkDestroyDevice");
        PFN_vkDestroyInstance destroyInstance =
            (PFN_vkDestroyInstance) (*s).gpa((*s).instance, "vkDestroyInstance");
        if (destroyDevice != nullptr && (*s).device != VK_NULL_HANDLE)
            destroyDevice((*s).device, nullptr);
        if (destroyInstance != nullptr)
            destroyInstance((*s).instance, nullptr);
    }
    if ((*s).lib != nullptr)
        dlclose((*s).lib);
    free(s);
}

static bool vkPresent(void *state) {
    (void) state;
    return false; // surface/present lands with the seam slice.
}

static bool vkResize(void *state, uint32_t width, uint32_t height) {
    if (state == nullptr || width == 0 || height == 0)
        return false;
    (*((VkDeviceState*) state)).width = width;
    (*((VkDeviceState*) state)).height = height;
    return true;
}

static uint32_t vkWidth(const void *state) {
    return state ? (*((const VkDeviceState*) state)).width : 0u;
}

static uint32_t vkHeight(const void *state) {
    return state ? (*((const VkDeviceState*) state)).height : 0u;
}

static bool vkIsReady(const void *state) {
    return state != nullptr && (*((const VkDeviceState*) state)).ready;
}

static void *vkNative(const void *state) {
    return state ? (void*) (*((const VkDeviceState*) state)).device : nullptr;
}

static const DeviceRow kVulkanRow = {
    .backend = LANG_BACKEND_VULKAN,
    .name = "vulkan",
    .createState = vkCreateState,
    .destroyState = vkDestroyState,
    .present = vkPresent,
    .resize = vkResize,
    .width = vkWidth,
    .height = vkHeight,
    .isReady = vkIsReady,
    .native = vkNative,
};

const DeviceRow *Vulkan_row(void) {
    return &kVulkanRow;
}
