#include "vulkan/vk_swapchain.h"
#include "vulkan/vk_graphics.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        assert(VkSwapchain_new(nullptr, 32, 32) == nullptr);
        assert(!VkSwapchain_destroy(nullptr));
        assert(!VkSwapchain_acquire(nullptr, nullptr, nullptr, nullptr));
        assert(Device_registerRow(Vulkan_row()));
        DeviceDesc desc = { .backend = LANG_BACKEND_VULKAN, .width = 64, .height = 64 };
        Device *offscreen = Device_new(&desc);
        assert(offscreen != nullptr);
        assert(VkSwapchain_new(offscreen, 64, 64) == nullptr);
        Device_destroy(offscreen);

        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 64, 64)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        if (!window)
            return 77;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.drawableSize = CGSizeMake(64, 64);
        [[window contentView] setWantsLayer:YES];
        [[window contentView] setLayer:layer];
        [window orderFront:nil];
        desc.window = layer;
        Device *device = Device_new(&desc);
        if (!device)
            return 77;
        if (!VkDevice_canSafelyPresent(device)) {
            Device_destroy(device);
            return 77;
        }
        assert(VkSwapchain_new(device, 0, 64) == nullptr);
        VkSwapchain *chain = VkSwapchain_new(device, 64, 64);
        assert(chain != nullptr);
        assert(VkSwapchain_getImageCount(chain) > 0);
        VkExtent2D extent = VkSwapchain_getExtent(chain);
        assert(extent.width == 64 && extent.height == 64);
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t index = 0;
        assert(VkSwapchain_acquire(chain, &image, &format, &index));
        assert(image != VK_NULL_HANDLE && format != VK_FORMAT_UNDEFINED);
        assert(index < VkSwapchain_getImageCount(chain));
        assert(VkSwapchain_isAcquired(chain));
        assert(!VkSwapchain_acquire(chain, &image, &format, &index));
        assert(image == VK_NULL_HANDLE);
        assert(!VkSwapchain_resize(chain, 32, 32));
        assert(!VkSwapchain_destroy(chain));
        assert(VkSwapchain_release(chain));
        assert(!VkSwapchain_isAcquired(chain));
        [window setContentSize:NSMakeSize(32, 32)];
        layer.drawableSize = CGSizeMake(32, 32);
        assert(VkSwapchain_resize(chain, 32, 32));
        extent = VkSwapchain_getExtent(chain);
        assert(extent.width == 32 && extent.height == 32);
        assert(VkSwapchain_acquire(chain, &image, &format, &index));
        assert(VkSwapchain_release(chain));
        assert(VkSwapchain_destroy(chain));
        assert(VkGraphics_bind(device));
        assert(Graphics_registerRow(VkGraphics_getRow()));
        assert(Graphics_setGraphics(LANG_BACKEND_VULKAN));
        assert(Graphics_resize(32, 32));
        assert(Graphics_begin());
        assert(Graphics_clear(0x203040FFu));
        Rectangle rect = { 4, 4, 20, 20 };
        Brush brush = { 0xFF0000FFu, 1.0f };
        assert(Graphics_fillRect(&rect, &brush));
        assert(Graphics_end());
        uint8_t forbiddenReadback[32 * 32 * 4];
        assert(!VkGraphics_readback(sizeof(forbiddenReadback), forbiddenReadback));
        assert(Graphics_present());
        assert(!Graphics_present());
        assert(Graphics_begin());
        assert(Graphics_clear(0x304050FFu));
        assert(Graphics_fillRect(&rect, &brush));
        assert(Graphics_end());
        assert(Graphics_present()); // reuse command buffer, semaphore and fences
        assert(!Graphics_present());
        [window setContentSize:NSMakeSize(48, 48)];
        layer.drawableSize = CGSizeMake(48, 48);
        assert(Graphics_resize(48, 48));
        assert(Graphics_begin());
        assert(Graphics_clear(0x204080FFu));
        assert(Graphics_fillRect(&rect, &brush));
        assert(Graphics_end());
        assert(Graphics_present());
        assert(!Graphics_present());
        VkGraphics_unbind();
        assert(VkGraphics_unbindIfDevice((VkDevice) Device_native(device)));
        Device_destroy(device);
        [window orderOut:nil];
        [window close];
        fprintf(stderr, "VkSwapchain Mac window GPU blit/present/idle/resize/destroy OK\n");
    }
    return 0;
}
