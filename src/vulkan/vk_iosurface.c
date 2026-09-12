#include "vulkan/vk_iosurface.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <vulkan/vulkan_metal.h>
#include "vk_guard.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkIOSurface (Vulkan/IOSurface interop wrapper)
 * LEVEL: L4 — Self-Management (Vulkan/IOSurface GPU interop setup)
 * ============================================================================
 * Vulkan ↔ IOSurface bridge over VK_EXT_metal_objects: a VkImage exported
 * as an IOSurfaceRef for AppKit compositing (or vice versa), zero-copy in
 * shared GPU memory in BGRA8.
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   VkIOSurface {          // Opaque interop wrapper (see vk_iosurface.h)
 *     IOSurfaceRef surface; // Backing surface (owned iff ownsSurface)
 *     VkImage image;       // Vulkan image over the same GPU memory
 *     uint32_t width;      // Backing pixel width
 *     uint32_t height;     // Backing pixel height
 *     bool ownsSurface;    // True if created here, false if wrapped
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - VkIOSurface_create(width, height)
 *     (Rule 39 net: export guards the publish seam at entry)
 *
 * Core Functions:
 *   - VkIOSurface_initModule(instance, gpa, phys, device)
 *   - VkIOSurface_wrap(ioSurface, width, height)
 *   - VkIOSurface_export(surf)
 *   - VkIOSurface_free(surf)
 *   - VkIOSurface_width(surf)
 *   - VkIOSurface_height(surf)
 *   - VkIOSurface_createFramebuffer(surf, pass)
 *
 * Getters:
 *   - VkIOSurface_getSurface(surf)
 *   - VkIOSurface_getImage(surf)
 * ============================================================================
 */


// VK_EXT_metal_objects — Vulkan ↔ IOSurface bridge.
//
// Two directions:
//   1. Create IOSurface → import as VkImage (VkIOSurface_create)
//      VkImageCreateInfo.pNext ← VkImportMetalIOSurfaceInfoEXT
//   2. Render into VkImage → export IOSurface (VkIOSurface_export)
//      vkExportMetalObjectsEXT with VkExportMetalIOSurfaceInfoEXT
//
// Both directions use BGRA8 format — safe for Vulkan color attachment
// and AppKit compositing.

struct VkIOSurface {
    IOSurfaceRef surface;
    VkImage       image;
    uint32_t      width;
    uint32_t      height;
    bool          ownsSurface; // true if we created it, false if wrapped
};

// Cached Vulkan state
static VkInstance       s_instance;
static VkPhysicalDevice s_phys;
static VkDevice         s_device;
static PFN_vkGetInstanceProcAddr s_gpa;
static PFN_vkGetDeviceProcAddr   s_gdpa;

#define IOS_LOAD_DEVICE(name) \
    static PFN_vk##name name##_fn; \
    if (!name##_fn) { \
        name##_fn = s_gdpa ? (PFN_vk##name)s_gdpa(s_device, "vk" #name) \
                           : (PFN_vk##name)s_gpa(s_instance, "vk" #name); \
    }

bool VkIOSurface_initModule(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                            VkPhysicalDevice phys, VkDevice device) {
    s_instance = instance;
    s_phys = phys;
    s_device = device;
    s_gpa = gpa;
    s_gdpa = (PFN_vkGetDeviceProcAddr)gpa(instance, "vkGetDeviceProcAddr");
    return s_gdpa != nullptr;
}

// Create IOSurface with BGRA8 format at the given size.
static IOSurfaceRef makeIOSurface(uint32_t width, uint32_t height) {
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!props) return nullptr;

    int bpr = (int)(width * 4); // BGRA8 = 4 bytes per pixel
    CFNumberRef w = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &width);
    CFNumberRef h = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &height);
    CFNumberRef bprNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bpr);
    int format = 'BGRA'; // kCVPixelFormatType_32BGRA
    CFNumberRef fmt = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &format);

    CFDictionarySetValue(props, kIOSurfaceWidth, w);
    CFDictionarySetValue(props, kIOSurfaceHeight, h);
    CFDictionarySetValue(props, kIOSurfaceBytesPerRow, bprNum);
    CFDictionarySetValue(props, kIOSurfacePixelFormat, fmt);

    IOSurfaceRef surface = IOSurfaceCreate(props);

    CFRelease(w); CFRelease(h); CFRelease(bprNum);
    CFRelease(fmt); CFRelease(props);
    return surface;
}

VkIOSurface *VkIOSurface_create(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return nullptr;

    VkIOSurface *surf = (VkIOSurface*) calloc(1, sizeof(VkIOSurface));
    if (!surf) return nullptr;

    (*surf).width = width;
    (*surf).height = height;
    (*surf).ownsSurface = true;

    // 1. Create IOSurface at the requested size, BGRA8
    (*surf).surface = makeIOSurface(width, height);
    if (!(*surf).surface) {
        free(surf);
        return nullptr;
    }

    // 2. Import IOSurface as VkImage — zero copy, same GPU memory
    //    VkImageCreateInfo.pNext ← VkImportMetalIOSurfaceInfoEXT
    IOS_LOAD_DEVICE(CreateImage);

    VkImportMetalIOSurfaceInfoEXT importInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_METAL_IO_SURFACE_INFO_EXT,
        .ioSurface = (*surf).surface,
    };

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &importInfo,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (CreateImage_fn(s_device, &ici, nullptr, &(*surf).image) != VK_SUCCESS) {
        fprintf(stderr, "vk_iosurface: import CreateImage failed\n");
        CFRelease((*surf).surface);
        free(surf);
        return nullptr;
    }

    return surf;
}

// Wrap an existing IOSurfaceRef as a VkImage. The IOSurface must have been
// created with BGRA8 format. Does NOT take ownership of the IOSurfaceRef.
VkIOSurface *VkIOSurface_wrap(void *ioSurface, uint32_t width, uint32_t height) {
    if (!ioSurface || width == 0 || height == 0) return nullptr;

    VkIOSurface *surf = (VkIOSurface*) calloc(1, sizeof(VkIOSurface));
    if (!surf) return nullptr;

    (*surf).width = width;
    (*surf).height = height;
    (*surf).ownsSurface = false;
    (*surf).surface = (IOSurfaceRef)ioSurface;
    CFRetain((*surf).surface);

    // Import IOSurface as VkImage — zero copy, same GPU memory
    IOS_LOAD_DEVICE(CreateImage);

    VkImportMetalIOSurfaceInfoEXT importInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_METAL_IO_SURFACE_INFO_EXT,
        .ioSurface = (*surf).surface,
    };

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &importInfo,

        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkResult res = CreateImage_fn(s_device, &ici, nullptr, &(*surf).image);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vk_iosurface: wrap CreateImage failed with error %d (req=%dx%d, surface=%dx%d)\n", 
            res, width, height, (int)IOSurfaceGetWidth((*surf).surface), (int)IOSurfaceGetHeight((*surf).surface));
        CFRelease((*surf).surface);
        free(surf);
        return nullptr;
    }

    return surf;
}

// After Vulkan renders into the image, export the IOSurface for AppKit
// compositing. The IOSurface now contains the rendered content.
bool VkIOSurface_export(VkIOSurface *surf) {
    if (!VkGuard_checkResource("VkIOSurface_export", s_device, false))
        return false;
    if (!surf || !(*surf).image || !(*surf).surface) return false;

    IOS_LOAD_DEVICE(ExportMetalObjectsEXT);

    VkExportMetalIOSurfaceInfoEXT exportInfo = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_IO_SURFACE_INFO_EXT,
        .image = (*surf).image,
        // ioSurface is an OUT field — gets populated by the function
    };

    VkExportMetalObjectsInfoEXT objectsInfo = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
        .pNext = &exportInfo,
    };

    ExportMetalObjectsEXT_fn(s_device, &objectsInfo);

    // The IOSurfaceRef should now be the same surface we imported, but
    // "finalized" and ready for AppKit compositing.
    return exportInfo.ioSurface != nullptr;
}

void VkIOSurface_free(VkIOSurface *surf) {
    if (!surf) return;
    if ((*surf).image) {
        IOS_LOAD_DEVICE(DestroyImage);
        DestroyImage_fn(s_device, (*surf).image, nullptr);
    }
    if ((*surf).surface) {
        if ((*surf).ownsSurface) {
            CFRelease((*surf).surface);
        } else {
            CFRelease((*surf).surface); // just release our retain from wrap
        }
    }
    free(surf);
}

void *VkIOSurface_getSurface(const VkIOSurface *surf) {
    return surf ? (void*) (*surf).surface : nullptr;
}

void *VkIOSurface_getImage(const VkIOSurface *surf) {
    return surf ? (void*) (*surf).image : nullptr;
}

uint32_t VkIOSurface_width(const VkIOSurface *surf) {
    return surf ? (*surf).width : 0;
}

uint32_t VkIOSurface_height(const VkIOSurface *surf) {
    return surf ? (*surf).height : 0;
}

// Create a framebuffer for rendering into this IOSurface's VkImage.
// The render pass must be compatible (BGRA8, color attachment).
// Returns VK_NULL_HANDLE on failure.
VkFramebuffer VkIOSurface_createFramebuffer(const VkIOSurface *surf, VkRenderPass pass) {
    if (!surf || !(*surf).image || !pass) return VK_NULL_HANDLE;

    IOS_LOAD_DEVICE(CreateImageView);
    IOS_LOAD_DEVICE(CreateFramebuffer);

    // Create image view
    VkImageViewCreateInfo ivci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = (*surf).image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.levelCount = 1,
        .subresourceRange.layerCount = 1,
    };

    VkImageView view;
    if (CreateImageView_fn(s_device, &ivci, nullptr, &view) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkFramebufferCreateInfo fci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = pass,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = (*surf).width,
        .height = (*surf).height,
        .layers = 1,
    };

    VkFramebuffer fb;
    if (CreateFramebuffer_fn(s_device, &fci, nullptr, &fb) != VK_SUCCESS) {
        IOS_LOAD_DEVICE(DestroyImageView);
        DestroyImageView_fn(s_device, view, nullptr);
        return VK_NULL_HANDLE;
    }

    return fb;
}
