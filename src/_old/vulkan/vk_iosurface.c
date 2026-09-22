#include "vulkan/vk_iosurface.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#if defined(__APPLE__)
// libdispatch first: the Xcode SDK's IOSurface to xpc headers need
// dispatch_queue_t, and plain C never pulls Foundation.
#include <dispatch/dispatch.h>
#endif
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <vulkan/vulkan_metal.h>
#include "vk_guard.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VkIOSurface
 * ============================================================================
 * Zero-copy interop container bridging Apple IOSurfaceRef kernel allocations with
 * Vulkan VkImage handles over the VK_EXT_metal_objects extension. Eliminates CPU
 * pixel transfers during composition by binding the same underlying physical GPU
 * memory buffer simultaneously to Vulkan raster pipelines and AppKit/Metal compositors.
 *
 * All allocations enforce 32-bit BGRA8 (kCVPixelFormatType_32BGRA) pixel formats
 * compatible with native display pipelines. Supports both export workflows where
 * Vulkan renders into an internally allocated IOSurface, and wrap workflows where
 * an externally owned IOSurface is imported as a VkImage color target. Handle exports
 * are guarded against invalid device contexts via the ecosystem Vulkan Safety Nets.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkIOSurface (vulkan/vk_iosurface.c)
 * LEVEL: L4 — Self-Management (Vulkan/IOSurface GPU interop setup)
 * ============================================================================
 * SUMMARY:
 *   Vulkan and IOSurface bridge over VK_EXT_metal_objects. Exports VkImage as
 *   an IOSurfaceRef for platform compositing or wraps an externally allocated
 *   IOSurface as a VkImage, achieving zero copy across shared GPU memory.
 *
 * STRUCT FIELDS (Local to this file):
 * ----------------------------------------------------------------------------
 *   IOSurfaceRef surface; // Backing surface (owned iff ownsSurface)
 *   VkImage image;        // Vulkan image over the same GPU memory
 *   uint32_t width;       // Backing pixel width
 *   uint32_t height;      // Backing pixel height
 *   bool ownsSurface;     // True if created here, false if wrapped
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VkIOSurface_create(width, height)                  : Allocate IOSurface and import as VkImage
 *   - VkIOSurface_wrap(ioSurface, width, height)         : Wrap external IOSurface as VkImage
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - VkIOSurface_initModule(instance, gpa, phys, device): Initialize Vulkan entry points for interop
 *   - VkIOSurface_export(surf)                           : Export VkImage to IOSurface for presentation
 *   - VkIOSurface_free(surf)                             : Release image and owned IOSurface handle
 *   - VkIOSurface_createFramebuffer(surf, pass)          : Create Vulkan framebuffer targeting image
 *
 * Private Core Functions: (.c static)
 *   - makeIOSurface(width, height)                       : Allocate core Apple IOSurface handle
 *
 * Public Setters: (.h)
 *   - (none)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - VkIOSurface_getSurface(surf)                       : Query underlying IOSurfaceRef
 *   - VkIOSurface_getImage(surf)                         : Query underlying VkImage handle
 *   - VkIOSurface_width(surf)                            : Query surface pixel width
 *   - VkIOSurface_height(surf)                           : Query surface pixel height
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

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

// CONSTRUCTORS (PUBLIC & PRIVATE)

static IOSurfaceRef makeIOSurface(uint32_t width, uint32_t height);

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

VkIOSurface *VkIOSurface_wrap(void *ioSurface, uint32_t width, uint32_t height) {
    if (!ioSurface || width == 0 || height == 0) return nullptr;

    VkIOSurface *surf = (VkIOSurface*) calloc(1, sizeof(VkIOSurface));
    if (!surf) return nullptr;

    (*surf).width = width;
    (*surf).height = height;
    (*surf).ownsSurface = false;
    (*surf).surface = (IOSurfaceRef)ioSurface;
    CFRetain((*surf).surface);

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

// CORE FUNCTIONS (PUBLIC & PRIVATE)

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

bool VkIOSurface_initModule(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                            VkPhysicalDevice phys, VkDevice device) {
    s_instance = instance;
    s_phys = phys;
    s_device = device;
    s_gpa = gpa;
    s_gdpa = (PFN_vkGetDeviceProcAddr) gpa(instance, "vkGetDeviceProcAddr");
    return s_gdpa != nullptr;
}

bool VkIOSurface_export(VkIOSurface *surf) {
    if (!VkGuard_checkResource("VkIOSurface_export", s_device, false))
        return false;
    if (!surf || !(*surf).image || !(*surf).surface) return false;

    IOS_LOAD_DEVICE(ExportMetalObjectsEXT);

    VkExportMetalIOSurfaceInfoEXT exportInfo = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_IO_SURFACE_INFO_EXT,
        .image = (*surf).image,
    };

    VkExportMetalObjectsInfoEXT objectsInfo = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
        .pNext = &exportInfo,
    };

    ExportMetalObjectsEXT_fn(s_device, &objectsInfo);
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
            CFRelease((*surf).surface);
        }
    }
    free(surf);
}

VkFramebuffer VkIOSurface_createFramebuffer(const VkIOSurface *surf, VkRenderPass pass) {
    if (!surf || !(*surf).image || !pass) return VK_NULL_HANDLE;

    IOS_LOAD_DEVICE(CreateImageView);
    IOS_LOAD_DEVICE(CreateFramebuffer);

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

// SETTERS (PUBLIC & PRIVATE)

// (none)

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
void *VkIOSurface_getSurface(const VkIOSurface *surf) {
    return surf ? (void*) (*surf).surface : nullptr;
}

;;GETTER
void *VkIOSurface_getImage(const VkIOSurface *surf) {
    return surf ? (void*) (*surf).image : nullptr;
}

;;GETTER
uint32_t VkIOSurface_width(const VkIOSurface *surf) {
    return surf ? (*surf).width : 0;
}

;;GETTER
uint32_t VkIOSurface_height(const VkIOSurface *surf) {
    return surf ? (*surf).height : 0;
}
