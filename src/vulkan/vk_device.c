#include "vulkan/vk_device.h"
#include "vulkan/vk_graphics.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
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
 * language. It loads the platform Vulkan loader, creates a
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
 * VkGraphics borrows native handles and detaches before teardown.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkDevice (vulkan/vk_device.c)
 * LEVEL: L4 — Self-Management (owns the Vulkan device across the process)
 * ============================================================================
 * SUMMARY:
 *   Vulkan DeviceRow. createState loads the loader, then builds instance,
 *   physical device, logical device and queue; destroyState reverses boot.
 *   Offscreen boot requests no WSI extensions; portability is optional.
 *
 * STRUCT FIELDS: none — the state is the private VkDeviceState helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   VkDeviceState
 *     VkLoaderHandle lib;      // platform loader handle
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
 *   - vkLoadLib / vkLoadGpa / vkCloseLib : platform loader operations
 *   - vkCreateState(desc) / vkDestroyState(state) (detach VkGraphics first)
 *   - vkPresent(state) / vkResize(state, w, h)
 *   - vkWidth(state) / vkHeight(state) / vkIsReady(state) / vkNative(state)
 * Public Getters: VkDevice_borrow(device, physical, native, queue, family, gpa, instance)
 * ============================================================================
 */

#ifdef _WIN32
typedef HMODULE VkLoaderHandle;
#else
typedef void *VkLoaderHandle;
#endif

// SLOT RECORD state for LANG_BACKEND_VULKAN (owned by the row's createState).
typedef struct VkDeviceState {
    VkLoaderHandle lib;
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

#ifdef _WIN32
static VkLoaderHandle vkLoadLib(void) {
    return LoadLibraryA("vulkan-1.dll");
}
static PFN_vkGetInstanceProcAddr vkLoadGpa(VkLoaderHandle lib) {
    return (PFN_vkGetInstanceProcAddr) GetProcAddress(lib, "vkGetInstanceProcAddr");
}
static void vkCloseLib(VkLoaderHandle lib) {
    FreeLibrary(lib);
}
#else
static VkLoaderHandle vkLoadLib(void) {
#ifdef __APPLE__
    const char *candidates[] = {
        "libMoltenVK.dylib",
        "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libMoltenVK.dylib",
        "libvulkan.dylib",
        "/opt/homebrew/lib/libvulkan.dylib",
        "/usr/local/lib/libvulkan.dylib",
        nullptr,
    };
#else
    const char *candidates[] = { "libvulkan.so.1", "libvulkan.so", nullptr };
#endif
    for (size_t i = 0; candidates[i] != nullptr; i++) {
        void *lib = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (lib != nullptr)
            return lib;
    }
    return nullptr;
}
static PFN_vkGetInstanceProcAddr vkLoadGpa(VkLoaderHandle lib) {
    return (PFN_vkGetInstanceProcAddr) dlsym(lib, "vkGetInstanceProcAddr");
}
static void vkCloseLib(VkLoaderHandle lib) {
    dlclose(lib);
}
#endif

static void vkDestroyState(void *state) {
    if (state == nullptr)
        return;
    VkDeviceState *s = (VkDeviceState*) state;
    (void) VkGraphics_unbindIfDevice((*s).device);
    if ((*s).instance != VK_NULL_HANDLE && (*s).gpa != nullptr) {
        if ((*s).device != VK_NULL_HANDLE) {
            PFN_vkDestroyDevice destroyDevice =
                (PFN_vkDestroyDevice) (*s).gpa((*s).instance, "vkDestroyDevice");
            if (destroyDevice != nullptr)
                destroyDevice((*s).device, nullptr);
        }
        PFN_vkDestroyInstance destroyInstance =
            (PFN_vkDestroyInstance) (*s).gpa((*s).instance, "vkDestroyInstance");
        if (destroyInstance != nullptr)
            destroyInstance((*s).instance, nullptr);
    }
    if ((*s).lib != nullptr)
        vkCloseLib((*s).lib);
    free(s);
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
        fprintf(stderr, "vk_device: no Vulkan loader\n");
        goto fail;
    }
    (*state).gpa = vkLoadGpa((*state).lib);
    if ((*state).gpa == nullptr) {
        fprintf(stderr, "vk_device: no vkGetInstanceProcAddr\n");
        goto fail;
    }

    PFN_vkCreateInstance createInstance =
        (PFN_vkCreateInstance) (*state).gpa(VK_NULL_HANDLE, "vkCreateInstance");
    PFN_vkEnumerateInstanceExtensionProperties enumExts =
        (PFN_vkEnumerateInstanceExtensionProperties) (*state).gpa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (createInstance == nullptr || enumExts == nullptr) {
        fprintf(stderr, "vk_device: global entry points missing\n");
        goto fail;
    }

    // Offscreen bootstrap requests only optional portability enumeration.
    uint32_t extCount = 0;
    if (enumExts(nullptr, &extCount, nullptr) != VK_SUCCESS)
        goto fail;
    VkExtensionProperties *props = nullptr;
    if (extCount != 0) {
        props = (VkExtensionProperties*) malloc((size_t) extCount * sizeof(*props));
        if (props == nullptr)
            goto fail;
        if (enumExts(nullptr, &extCount, props) != VK_SUCCESS) {
            free(props);
            goto fail;
        }
    }
    bool portability = false;
    for (uint32_t i = 0; i < extCount; i++) {
        if (strcmp(props[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
            portability = true;
    }
    free(props);

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vex";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    const char *portabilityExt = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    ici.enabledExtensionCount = portability ? 1u : 0u;
    ici.ppEnabledExtensionNames = portability ? &portabilityExt : nullptr;
    ici.flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;

    if (createInstance(&ici, nullptr, &(*state).instance) != VK_SUCCESS) {
        fprintf(stderr, "vk_device: instance create failed\n");
        goto fail;
    }

    // Select the first physical device, then locate its graphics queue family.
    PFN_vkEnumeratePhysicalDevices enumPhys =
        (PFN_vkEnumeratePhysicalDevices) (*state).gpa((*state).instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getFamilies =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties) (*state).gpa((*state).instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice createDevice =
        (PFN_vkCreateDevice) (*state).gpa((*state).instance, "vkCreateDevice");
    if (enumPhys == nullptr || getFamilies == nullptr || createDevice == nullptr) {
        fprintf(stderr, "vk_device: instance entry points missing\n");
        goto fail;
    }

    uint32_t physCount = 0;
    if (enumPhys((*state).instance, &physCount, nullptr) != VK_SUCCESS || physCount == 0) {
        fprintf(stderr, "vk_device: no physical devices\n");
        goto fail;
    }
    uint32_t firstCount = 1;
    VkResult physResult = enumPhys((*state).instance, &firstCount, &(*state).phys);
    if ((physResult != VK_SUCCESS && physResult != VK_INCOMPLETE) || firstCount == 0)
        goto fail;

    uint32_t familyCount = 0;
    getFamilies((*state).phys, &familyCount, nullptr);
    if (familyCount == 0)
        goto fail;
    VkQueueFamilyProperties *families =
        (VkQueueFamilyProperties*) malloc((size_t) familyCount * sizeof(*families));
    if (families == nullptr)
        goto fail;
    getFamilies((*state).phys, &familyCount, families);
    bool found = false;
    for (uint32_t f = 0; f < familyCount; f++) {
        if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && families[f].queueCount != 0) {
            (*state).queueFamily = f;
            found = true;
            break;
        }
    }
    free(families);
    if (!found) {
        fprintf(stderr, "vk_device: no graphics queue family\n");
        goto fail;
    }

    // Logical device and graphics queue; presentation extensions come later.
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = (*state).queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    // Presentation extensions belong to the future surface seam, not offscreen boot.

    if (createDevice((*state).phys, &dci, nullptr, &(*state).device) != VK_SUCCESS) {
        fprintf(stderr, "vk_device: logical device create failed\n");
        goto fail;
    }
    PFN_vkGetDeviceQueue getQueue =
        (PFN_vkGetDeviceQueue) (*state).gpa((*state).instance, "vkGetDeviceQueue");
    if (getQueue == nullptr)
        goto fail;
    getQueue((*state).device, (*state).queueFamily, 0, &(*state).queue);
    if ((*state).queue == VK_NULL_HANDLE)
        goto fail;

    (*state).ready = true;
    return state;
fail:
    vkDestroyState(state);
    return nullptr;
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

bool VkDevice_borrow(const Device *device, VkPhysicalDevice *physical,
                     VkDevice *native, VkQueue *queue, uint32_t *family,
                     PFN_vkGetInstanceProcAddr *gpa, VkInstance *instance) {
    VkDeviceState *s = (VkDeviceState*) Device_stateForBackend(device, LANG_BACKEND_VULKAN);
    if (s == nullptr || physical == nullptr || native == nullptr || queue == nullptr ||
        family == nullptr || gpa == nullptr || instance == nullptr)
        return false;
    *physical = (*s).phys;
    *native = (*s).device;
    *queue = (*s).queue;
    *family = (*s).queueFamily;
    *gpa = (*s).gpa;
    *instance = (*s).instance;
    return true;
}
