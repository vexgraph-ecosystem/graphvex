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
 * VkInstance with only the extensions the driver actually exposes, picks a
 * physical device, and creates one logical device + graphics queue.
 *
 * Windowed devices own a surface but not yet a swapchain or renderer. When
 * both optional present-id/wait extensions and features exist, they are
 * enabled together and the device-private bounded completion seam is armed.
 *
 * The state is dialect-private (VkInstance/VkPhysicalDevice/VkDevice/VkQueue);
 * The language contract does not expose Vulkan handles. present remains
 * false until a real swapchain lands (the Cold-Strict, Hot-Minimal Validation Law).
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
 *   Offscreen boot requests no WSI extensions; windowed boot validates WSI,
 *   present queues, swapchain support and surface viability before device boot.
 *   Portability is optional in both paths.
 *
 * STRUCT FIELDS: none — the state is the private VkDeviceState helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   VkDeviceState
 *     VkLoaderHandle lib;      // platform loader handle
 *     PFN_vkGetInstanceProcAddr gpa;
 *     VkInstance instance;
 *     VkSurfaceKHR surface;  // owned, window handle borrowed
 *     VkPhysicalDevice phys;
 *     VkDevice device;
 *     VkQueue queue;
 *     uint32_t queueFamily;
 *     uint32_t width, height;  // requested native px (0 = offscreen)
 *     bool ready;
 *     bool windowed;
 *     PFN_vkWaitForPresentKHR waitForPresent; // optional enabled completion entry
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - vkLoadLib / vkLoadGpa / vkCloseLib : platform loader operations
 *   - vkCreateState(desc) / vkDestroyState(state) (detach VkGraphics first)
 *   - vkSurfaceViable(state, phys) / vkDeviceExtensions(state, phys, ...)
 *   - vkPresent(state) / vkResize(state, w, h)
 *   - vkWidth(state) / vkHeight(state) / vkIsReady(state) / vkNative(state)
 * Public Getters: VkDevice_borrow(device, physical, native, queue, family, gpa, instance),
 *                 VkDevice_borrowSurface(device), VkDevice_canWaitForPresent(device),
 *                 VkDevice_waitForPresent(device, swapchain, presentId, timeoutNs)
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
    VkSurfaceKHR surface;
    VkPhysicalDevice phys;
    VkDevice device;
    VkQueue queue;
    uint32_t queueFamily;
    uint32_t width;                     // requested native px (0 = offscreen)
    uint32_t height;
    bool ready;
    bool windowed;
    PFN_vkWaitForPresentKHR waitForPresent;
} VkDeviceState;

static bool vkDeviceExtensions(VkDeviceState *s, VkPhysicalDevice phys,
                               bool *presentId, bool *presentWait) {
    PFN_vkEnumerateDeviceExtensionProperties enumerate =
        (PFN_vkEnumerateDeviceExtensionProperties) (*s).gpa((*s).instance, "vkEnumerateDeviceExtensionProperties");
    if (enumerate == nullptr)
        return false;
    uint32_t count = 0;
    if (enumerate(phys, nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
        return false;
    VkExtensionProperties *extensions = (VkExtensionProperties*) malloc((size_t) count * sizeof(*extensions));
    if (extensions == nullptr)
        return false;
    VkResult result = enumerate(phys, nullptr, &count, extensions);
    bool found = false;
    *presentId = false;
    *presentWait = false;
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                found = true;
            if (strcmp(extensions[i].extensionName, VK_KHR_PRESENT_ID_EXTENSION_NAME) == 0)
                *presentId = true;
            if (strcmp(extensions[i].extensionName, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0)
                *presentWait = true;
        }
    }
    free(extensions);
    return found;
}

static bool vkSurfaceViable(VkDeviceState *s, VkPhysicalDevice phys) {
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR capabilities =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) (*s).gpa((*s).instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR formats =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR) (*s).gpa((*s).instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR modes =
        (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR) (*s).gpa((*s).instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (capabilities == nullptr || formats == nullptr || modes == nullptr)
        return false;
    VkSurfaceCapabilitiesKHR caps;
    uint32_t formatCount = 0, modeCount = 0;
    return capabilities(phys, (*s).surface, &caps) == VK_SUCCESS &&
           caps.minImageCount != 0 &&
           formats(phys, (*s).surface, &formatCount, nullptr) == VK_SUCCESS && formatCount != 0 &&
           modes(phys, (*s).surface, &modeCount, nullptr) == VK_SUCCESS && modeCount != 0;
}

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
        if ((*s).surface != VK_NULL_HANDLE) {
            PFN_vkDestroySurfaceKHR destroySurface =
                (PFN_vkDestroySurfaceKHR) (*s).gpa((*s).instance, "vkDestroySurfaceKHR");
            if (destroySurface != nullptr)
                destroySurface((*s).instance, (*s).surface, nullptr);
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
        (*state).windowed = (*desc).window != nullptr;
    }
#if !defined(__APPLE__) && !defined(_WIN32)
    if ((*state).windowed)
        goto fail;
#endif

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
    bool portability = false, surfaceExt = false, platformExt = false;
    for (uint32_t i = 0; i < extCount; i++) {
        if (strcmp(props[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
            portability = true;
        if (strcmp(props[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0)
            surfaceExt = true;
#ifdef __APPLE__
        if (strcmp(props[i].extensionName, VK_EXT_METAL_SURFACE_EXTENSION_NAME) == 0)
            platformExt = true;
#elif defined(_WIN32)
        if (strcmp(props[i].extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0)
            platformExt = true;
#endif
    }
    free(props);
    if ((*state).windowed && (!surfaceExt || !platformExt)) {
        fprintf(stderr, "vk_device: required WSI instance extensions missing\n");
        goto fail;
    }

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vex";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    const char *instanceExts[3];
    uint32_t enabled = 0;
    if (portability)
        instanceExts[enabled++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    if ((*state).windowed) {
        instanceExts[enabled++] = VK_KHR_SURFACE_EXTENSION_NAME;
#ifdef __APPLE__
        instanceExts[enabled++] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
#elif defined(_WIN32)
        instanceExts[enabled++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#endif
    }
    ici.enabledExtensionCount = enabled;
    ici.ppEnabledExtensionNames = enabled ? instanceExts : nullptr;
    ici.flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;

    if (createInstance(&ici, nullptr, &(*state).instance) != VK_SUCCESS) {
        fprintf(stderr, "vk_device: instance create failed\n");
        goto fail;
    }

    if ((*state).windowed) {
#ifdef __APPLE__
        PFN_vkCreateMetalSurfaceEXT createSurface =
            (PFN_vkCreateMetalSurfaceEXT) (*state).gpa((*state).instance, "vkCreateMetalSurfaceEXT");
        VkMetalSurfaceCreateInfoEXT info = { .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
        info.pLayer = (*desc).window;
#elif defined(_WIN32)
        PFN_vkCreateWin32SurfaceKHR createSurface =
            (PFN_vkCreateWin32SurfaceKHR) (*state).gpa((*state).instance, "vkCreateWin32SurfaceKHR");
        VkWin32SurfaceCreateInfoKHR info = { .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        info.hwnd = (HWND) (*desc).window;
        info.hinstance = (HINSTANCE) GetWindowLongPtrW(info.hwnd, GWLP_HINSTANCE);
        if (info.hinstance == nullptr)
            goto fail;
#endif
#if defined(__APPLE__) || defined(_WIN32)
        if (createSurface == nullptr || createSurface((*state).instance, &info, nullptr, &(*state).surface) != VK_SUCCESS)
            goto fail;
#endif
    }

    // For WSI choose a device with a graphics+present queue and swapchain support.
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
    VkPhysicalDevice *devices = (VkPhysicalDevice*) malloc((size_t) physCount * sizeof(*devices));
    if (devices == nullptr)
        goto fail;
    VkResult physResult = enumPhys((*state).instance, &physCount, devices);
    bool found = false;
    bool presentIdExt = false, presentWaitExt = false;
    if (physResult == VK_SUCCESS || physResult == VK_INCOMPLETE) {
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR support = nullptr;
        if ((*state).windowed)
            support = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR) (*state).gpa((*state).instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        for (uint32_t p = 0; p < physCount && !found; p++) {
            VkPhysicalDevice phys = devices[p];
            bool idExt = false, waitExt = false;
            if ((*state).windowed && (support == nullptr ||
                                      !vkDeviceExtensions(state, phys, &idExt, &waitExt) ||
                                      !vkSurfaceViable(state, phys)))
                continue;
            uint32_t familyCount = 0;
            getFamilies(phys, &familyCount, nullptr);
            if (familyCount == 0)
                continue;
            VkQueueFamilyProperties *families =
                (VkQueueFamilyProperties*) malloc((size_t) familyCount * sizeof(*families));
            if (families == nullptr)
                continue;
            getFamilies(phys, &familyCount, families);
            for (uint32_t f = 0; f < familyCount; f++) {
                if (!(families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) || families[f].queueCount == 0)
                    continue;
                if ((*state).windowed) {
                    VkBool32 present = VK_FALSE;
                    if (support(phys, f, (*state).surface, &present) != VK_SUCCESS || !present)
                        continue;
                }
                (*state).phys = phys;
                (*state).queueFamily = f;
                presentIdExt = idExt;
                presentWaitExt = waitExt;
                found = true;
                break;
            }
            free(families);
        }
    }
    free(devices);
    if (!found) {
        fprintf(stderr, "vk_device: no compatible graphics/present queue or surface\n");
        goto fail;
    }

    // Optional completion features are only requested as a pair. Extension
    // advertisement alone is not feature support; never infer it from OS.
    VkPhysicalDevicePresentIdFeaturesKHR idFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR
    };
    VkPhysicalDevicePresentWaitFeaturesKHR waitFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR
    };
    bool boundedPresent = false;
    if ((*state).windowed && presentIdExt && presentWaitExt) {
        PFN_vkGetPhysicalDeviceFeatures2 features2 =
            (PFN_vkGetPhysicalDeviceFeatures2) (*state).gpa((*state).instance, "vkGetPhysicalDeviceFeatures2");
        if (features2 != nullptr) {
            VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            features.pNext = &idFeature;
            idFeature.pNext = &waitFeature;
            features2((*state).phys, &features);
            boundedPresent = idFeature.presentId == VK_TRUE && waitFeature.presentWait == VK_TRUE;
        }
    }

    // Offscreen has no device extensions; windowed devices require swapchain.
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = (*state).queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    const char *deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                 VK_KHR_PRESENT_ID_EXTENSION_NAME,
                                 VK_KHR_PRESENT_WAIT_EXTENSION_NAME };
    dci.enabledExtensionCount = (*state).windowed ? (boundedPresent ? 3u : 1u) : 0u;
    dci.ppEnabledExtensionNames = (*state).windowed ? deviceExts : nullptr;
    if (boundedPresent)
        dci.pNext = &idFeature;

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

    if (boundedPresent) {
        PFN_vkGetDeviceProcAddr deviceProc =
            (PFN_vkGetDeviceProcAddr) (*state).gpa((*state).instance, "vkGetDeviceProcAddr");
        if (deviceProc != nullptr)
            (*state).waitForPresent = (PFN_vkWaitForPresentKHR) deviceProc((*state).device, "vkWaitForPresentKHR");
    }

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

VkSurfaceKHR VkDevice_borrowSurface(const Device *device) {
    VkDeviceState *s = (VkDeviceState*) Device_stateForBackend(device, LANG_BACKEND_VULKAN);
    return s ? (*s).surface : VK_NULL_HANDLE;
}

bool VkDevice_canWaitForPresent(const Device *device) {
    VkDeviceState *s = (VkDeviceState*) Device_stateForBackend(device, LANG_BACKEND_VULKAN);
    return s != nullptr && (*s).ready && (*s).windowed && (*s).waitForPresent != nullptr;
}

VkResult VkDevice_waitForPresent(const Device *device, VkSwapchainKHR swapchain,
                                 uint64_t presentId, uint64_t timeoutNs) {
    VkDeviceState *s = (VkDeviceState*) Device_stateForBackend(device, LANG_BACKEND_VULKAN);
    if (s == nullptr || !(*s).ready || !(*s).windowed || (*s).waitForPresent == nullptr)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (swapchain == VK_NULL_HANDLE || presentId == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    const uint64_t maxWaitNs = 100000000ull;
    return (*s).waitForPresent((*s).device, swapchain, presentId,
                               timeoutNs < maxWaitNs ? timeoutNs : maxWaitNs);
}
