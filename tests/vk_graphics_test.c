#include "vulkan/vk_device.h"
#include "vulkan/vk_graphics.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    assert(VkGraphics_getRectCapacity() >= 50000);
    assert(!VkGraphics_setRectCapacity(0));
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
    // Ordered fills, straight-alpha RGB over and source-over output alpha.
    assert(Graphics_resize(4, 3));
    Rectangle full = { 0, 0, 4, 3 };
    Rectangle mid = { 1, 0, 2, 3 };
    Rectangle clipped = { 0, 0, 4, 3 };
    Rectangle clip = { 2, 1, 1, 1 };
    Brush red = { 0xFF0000FFu, 1.0f };
    Brush blue = { 0x0000FF80u, 0.5f };
    Brush green = { 0x00FF00FFu, 1.0f };
    assert(Graphics_begin());
    assert(Graphics_clear(0x00000000u));
    assert(Graphics_fillRect(&full, &red));
    assert(Graphics_fillRect(&mid, &blue));
    assert(Graphics_clip(&clip));
    Rectangle savedClip;
    assert(Graphics_getClip(&savedClip));
    Rectangle innerClip = { 2, 1, 0.5f, 0.5f };
    assert(Graphics_clip(&innerClip));
    assert(Graphics_fillRect(&clipped, &green));
    assert(Graphics_clip(&savedClip));
    assert(Graphics_clip(nullptr));
    Rectangle corner = { 3, 2, 1, 1 };
    assert(Graphics_fillRect(&corner, &blue));
    assert(Graphics_end());
    uint8_t composite[48];
    assert(VkGraphics_readback(sizeof(composite), composite));
    // color at (1,0) = red overlaid by blue with alpha 128/255 * .5.
    float alpha = (128.0f / 255.0f) * 0.5f;
    int expectRed = (int) (255.0f * (1.0f - alpha) + 0.5f);
    int expectBlue = (int) (255.0f * alpha + 0.5f);
    for (uint32_t y = 0; y < 3; y++)
        for (uint32_t x = 0; x < 4; x++) {
            const uint8_t *px = composite + ((size_t) y * 4 + x) * 4;
            if (x == 2 && y == 1) {
                assert(px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255);
            } else if (x == 1 || x == 2 || (x == 3 && y == 2)) {
                assert(abs((int) px[0] - expectRed) <= 1 && px[1] == 0);
                assert(abs((int) px[2] - expectBlue) <= 1 && px[3] == 255);
            } else {
                assert(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);
            }
        }
    // Oversized and offscreen rects clamp before integer conversion; rounded edges.
    assert(Graphics_begin());
    assert(Graphics_clear(0x12345678u));
    Rectangle huge = { -100000000.0f, -100000000.0f, 200000000.0f, 200000000.0f };
    Rectangle fractional = { 0.49f, 0.49f, 1.01f, 1.01f };
    Rectangle offscreen = { 100000000.0f, 0, 10, 10 };
    assert(Graphics_fillRect(&huge, &green));
    assert(Graphics_fillRect(&fractional, &red));
    assert(Graphics_fillRect(&offscreen, &red));
    assert(Graphics_end());
    assert(VkGraphics_readback(sizeof(composite), composite));
    assert(composite[0] == 255 && composite[1] == 0 && composite[2] == 0);
    assert(composite[4] == 255 && composite[5] == 0 && composite[6] == 0);
    assert(composite[8] == 0 && composite[9] == 255 && composite[10] == 0);
    // Translucent source over a translucent clear preserves the straight RGB
    // blend contract and updates destination alpha independently.
    assert(Graphics_begin());
    assert(Graphics_clear(0x20406080u));
    Rectangle one = { 0, 0, 1, 1 };
    assert(Graphics_fillRect(&one, &blue));
    assert(Graphics_clear(0x12345678u)); // clear after a draw preserves order
    assert(Graphics_fillRect(&one, &blue));
    assert(Graphics_end());
    assert(VkGraphics_readback(sizeof(composite), composite));
    float source = (128.0f / 255.0f) * 0.5f;
    assert(abs((int) composite[0] - (int) (0x12 * (1.0f - source) + 0.5f)) <= 1);
    assert(abs((int) composite[1] - (int) (0x34 * (1.0f - source) + 0.5f)) <= 1);
    assert(abs((int) composite[2] - (int) (0x56 * (1.0f - source) + 255.0f * source + 0.5f)) <= 1);
    assert(abs((int) composite[3] - (int) (0x78 + (255.0f - 0x78) * source + 0.5f)) <= 1);
    assert(Graphics_begin());
    assert(Graphics_clear(0x00000000u));
    assert(Graphics_fillRect(&one, &blue));
    assert(Graphics_fillRect(&one, &blue));
    assert(Graphics_end());
    assert(VkGraphics_readback(sizeof(composite), composite));
    float twice = source + source * (1.0f - source);
    assert(composite[0] == 0 && composite[1] == 0);
    assert(abs((int) composite[2] - (int) (255.0f * twice + 0.5f)) <= 1);
    assert(abs((int) composite[3] - (int) (255.0f * twice + 0.5f)) <= 1);
    assert((*row).resize(2, 2));
    assert((*row).begin());
    assert((*row).clear(0x12345678u));
    assert((*row).end());
    assert(VkGraphics_readback(16, pixels));
    assert(pixels[0] == 0x12 && pixels[1] == 0x34 && pixels[2] == 0x56 && pixels[3] == 0x78);
    // Dense, ordered 50k fills: thousands of overlapping translucent quads
    // with a clip boundary, yet only two draw calls and no instance overflow.
    assert((*row).resize(100, 100));
    uint8_t dense[100 * 100 * 4];
    Brush opaque = { 0x12AB34FFu, 1.0f };
    Brush translucent = { 0xFF000080u, 1.0f };
    Rectangle tile = { 0, 0, 1, 1 };
    Rectangle denseClip = { 0, 0, 50, 100 };
    assert((*row).begin());
    assert((*row).clear(0x000000FFu));
    for (int i = 0; i < 50000; i++) {
        if (i == 25000)
            assert((*row).clip(&denseClip));
        tile.x = (float) (i % 100);
        tile.y = (float) ((i / 100) % 100);
        assert((*row).fillRect(&tile, i < 25000 ? &opaque : &translucent));
    }
    assert((*row).end());
    assert(VkGraphics_getDrawCount() == 2);
    assert(VkGraphics_readback(sizeof(dense), dense));
    size_t inside = ((size_t) 0 * 100 + 0) * 4;
    size_t outside = ((size_t) 0 * 100 + 75) * 4;
    float a = 128.0f / 255.0f;
    int redTwice = (int) (255.0f * a + (18.0f * a + 255.0f * (1.0f - a)) * (1.0f - a) + 0.5f);
    int greenTwice = (int) (171.0f * (1.0f - a) * (1.0f - a) + 0.5f);
    assert(abs((int) dense[inside] - redTwice) <= 1 && abs((int) dense[inside + 1] - greenTwice) <= 1);
    assert(dense[outside] == 0x12 && dense[outside + 1] == 0xAB && dense[outside + 2] == 0x34);
    assert(!VkGraphics_setRectCapacity(128)); // only cold before a target exists
    Device_destroy(device);
    assert(VkGraphics_bind(other));
    VkGraphics_unbind();
    Device_destroy(other);
    puts("offscreen Vulkan RGBA8 clear/readback OK");
    return 0;
}
