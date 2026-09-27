#include "vulkan/vk_device.h"
#include "vulkan/vk_graphics.h"
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static bool hasLoader(void) {
#ifdef _WIN32
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (lib == nullptr)
        return false;
    FreeLibrary(lib);
    return true;
#elif defined(__APPLE__)
    const char *paths[] = { "libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib",
        "/usr/local/lib/libMoltenVK.dylib", "libvulkan.dylib",
        "/opt/homebrew/lib/libvulkan.dylib", "/usr/local/lib/libvulkan.dylib", nullptr };
#else
    const char *paths[] = { "libvulkan.so.1", "libvulkan.so", nullptr };
#endif
    for (size_t i = 0; paths[i] != nullptr; i++) {
        void *lib = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (lib != nullptr) {
            dlclose(lib);
            return true;
        }
    }
    return false;
}

int main(void) {
    if (!hasLoader()) {
        fprintf(stderr, "Vulkan loader unavailable; skip\n");
        return 77;
    }
    assert(Device_registerRow(Vulkan_row()));
    DeviceDesc desc = { .backend = LANG_BACKEND_VULKAN };
    Device *device = Device_new(&desc);
    assert(device != nullptr);
    assert(VkGraphics_bind(device));
    assert(VkGraphics_bind(device));
    Device *other = Device_new(&desc);
    assert(other != nullptr);
    assert(!VkGraphics_bind(other));
    const Graphics *row = VkGraphics_getRow();
    assert((*row).backendId == LANG_BACKEND_VULKAN);
    assert(Graphics_registerRow(row));
    assert(Graphics_setGraphics(LANG_BACKEND_VULKAN));
    assert(Graphics_getCurrent() == row);
    assert((*row).resize(3, 2));
    assert(!(*row).present());
    assert((*row).begin());
    assert((*row).clip(nullptr));
    assert(!(*row).fillRect(nullptr, nullptr));
    assert((*row).clear(0x7F348BFFu));
    assert((*row).end());
    uint8_t pixels[24] = {0};
    assert(!VkGraphics_readback(23, pixels));
    assert(VkGraphics_readback(sizeof(pixels), pixels));
    for (size_t i = 0; i < sizeof(pixels); i += 4) {
        assert(pixels[i] == 0x7F);
        assert(pixels[i + 1] == 0x34);
        assert(pixels[i + 2] == 0x8B);
        assert(pixels[i + 3] == 0xFF);
    }
    assert((*row).begin());
    assert((*row).clear(0x10203040u));
    assert((*row).end());
    assert(VkGraphics_readback(sizeof(pixels), pixels));
    assert(pixels[0] == 0x10 && pixels[1] == 0x20 && pixels[2] == 0x30 && pixels[3] == 0x40);
    assert((*row).resize(2, 2));
    assert((*row).begin());
    assert((*row).clear(0x12345678u));
    assert((*row).end());
    assert(VkGraphics_readback(16, pixels));
    assert(pixels[0] == 0x12 && pixels[1] == 0x34 && pixels[2] == 0x56 && pixels[3] == 0x78);
    Device_destroy(device);
    assert(VkGraphics_bind(other));
    VkGraphics_unbind();
    Device_destroy(other);
    puts("offscreen Vulkan RGBA8 clear/readback OK");
    return 0;
}
