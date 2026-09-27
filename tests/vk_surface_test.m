#include "vulkan/vk_device.h"
#import <QuartzCore/CAMetalLayer.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool wsiAvailable(void) {
    const char *paths[] = { "libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libMoltenVK.dylib", "libvulkan.dylib",
        "/opt/homebrew/lib/libvulkan.dylib", "/usr/local/lib/libvulkan.dylib", nullptr };
    void *lib = nullptr;
    for (size_t i = 0; paths[i] != nullptr && lib == nullptr; i++)
        lib = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
        return false;
    PFN_vkGetInstanceProcAddr gpa = (PFN_vkGetInstanceProcAddr) dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkEnumerateInstanceExtensionProperties enumerate = gpa ?
        (PFN_vkEnumerateInstanceExtensionProperties) gpa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties") : nullptr;
    uint32_t count = 0;
    bool surface = false, metal = false;
    if (enumerate != nullptr && enumerate(nullptr, &count, nullptr) == VK_SUCCESS && count != 0) {
        VkExtensionProperties *props = (VkExtensionProperties*) malloc((size_t) count * sizeof(*props));
        if (props != nullptr) {
            if (enumerate(nullptr, &count, props) == VK_SUCCESS) {
                for (uint32_t i = 0; i < count; i++) {
                    surface |= strcmp(props[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
                    metal |= strcmp(props[i].extensionName, VK_EXT_METAL_SURFACE_EXTENSION_NAME) == 0;
                }
            }
            free(props);
        }
    }
    dlclose(lib);
    return surface && metal;
}

int main(void) {
    @autoreleasepool {
        if (!Device_registerRow(Vulkan_row()))
            return 1;
        CAMetalLayer *layer = [CAMetalLayer layer];
        DeviceDesc desc = { .backend = LANG_BACKEND_VULKAN, .window = layer,
                            .width = 16, .height = 16 };
        Device *windowed = Device_new(&desc);
        if (!wsiAvailable()) {
            // Missing WSI extensions must never produce a half-created device.
            assert(windowed == nullptr);
            fprintf(stderr, "Vulkan Metal WSI unavailable; skip windowed proof\n");
            return 77;
        }
        assert(windowed != nullptr);
        assert(VkDevice_borrowSurface(windowed) != VK_NULL_HANDLE);
        assert(Device_isReady(windowed));
        assert(!Device_present(windowed)); // no swapchain in this milestone
        Device_destroy(windowed);
        desc.window = nullptr;
        Device *offscreen = Device_new(&desc);
        assert(offscreen != nullptr);
        assert(VkDevice_borrowSurface(offscreen) == VK_NULL_HANDLE);
        Device_destroy(offscreen);
    }
    return 0;
}
