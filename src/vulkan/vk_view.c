#include "vulkan/vk_view.h"

#include <stdio.h>
#include <string.h>

#include "system/display_info.h"
#include "system/display_monitor.h"
#include "vk_guard.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkView (per-monitor render cache)
 * LEVEL: L4 — Self-Management (Vulkan per-monitor view GPU setup)
 * ============================================================================
 * The per-monitor render cache (the compositor view): one giant off-screen
 * BGRA8 image per attached monitor at native resolution, rendered in absolute
 * desktop coordinates and sampled by window blits.
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   VkView {               // Opaque per-monitor cache (see vk_view.h)
 *     uint32_t displayId;  // CGDirectDisplayID this view mirrors
 *     float originX;       // Desktop origin X in global AppKit points
 *     float originY;       // Desktop origin Y in global AppKit points
 *     int32_t pointW;      // Logical point extent width
 *     int32_t pointH;      // Logical point extent height
 *     int32_t cacheW;      // Native physical pixel width (the giant buffer)
 *     int32_t cacheH;      // Native physical pixel height (the giant buffer)
 *     VkImage image;       // Cache image backing
 *     VkDeviceMemory memory; // Cache image device memory
 *     VkImageView view;    // Cache image view
 *     VkRenderPass pass;   // Single-subpass clear-on-load render pass
 *     VkFramebuffer fb;    // Framebuffer targeting the cache image
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - VkView_refreshAll(instance, gpa, phys, device)
 *   - VkView_count(void)
 *   - VkView_at(index)
 *     (the Ecosystem Vulkan Safety Nets Law net: beginPass guards the record seam at entry)
 *   - VkView_forPoint(x, y)
 *   - VkView_forMonitor(displayId)
 *   - VkView_renderPass(view)
 *   - VkView_image(view)
 *   - VkView_beginPass(view, cb, r, g, b, a)
 *   - VkView_endPass(view, cb)
 *   - VkView_shutdown(void)
 *
 * Getters:
 *   - VkView_getOriginX(view)
 *   - VkView_getOriginY(view)
 *   - VkView_getWidth(view)
 *   - VkView_getHeight(view)
 *   - VkView_getPointWidth(view)
 *   - VkView_getPointHeight(view)
 * ============================================================================
 */


// vulkan/vk_view.c — the per-monitor render cache.
//
// The giant buffer: one BGRA8 image per monitor at its native panel
// resolution (fallback: active pixel mode). Rendered into in absolute
// desktop coordinates, sampled by window blits. Cleared fully every present
// loop so a frame is always complete — no damage heuristics to trust.

#define VKV_CHECK(call, what)                                                  \
    do {                                                                       \
        if ((call) != VK_SUCCESS) {                                            \
            fprintf(stderr, "vk_view: %s failed\n", what);                     \
            return false;                                                      \
        }                                                                      \
    } while (0)

typedef struct VkView {
    // monitor identity (system/ DisplayMonitor row this view mirrors)
    uint32_t displayId;

    // desktop geometry
    float originX;      // global AppKit points
    float originY;
    int32_t pointW;     // logical point extent
    int32_t pointH;
    int32_t cacheW;     // native physical pixels (the giant buffer)
    int32_t cacheH;

    // GPU objects
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkRenderPass pass;
    VkFramebuffer fb;
} VkView;

static VkView s_views[16];
static size_t s_viewCount = 0;

// loader plumbing — fetched once per refreshAll through the caller's gpa
static PFN_vkGetInstanceProcAddr s_gpa = nullptr;
static PFN_vkGetDeviceProcAddr s_gdpa = nullptr;
static VkInstance s_instance;
static VkPhysicalDevice s_phys;
static VkDevice s_device;

#define VKV_LOAD_INSTANCE(name)                                                \
    static PFN_vk##name name##_fn = nullptr;                                      \
    if (!name##_fn)                                                            \
        name##_fn = (PFN_vk##name)s_gpa(s_instance, "vk" #name);               \
    if (!name##_fn) {                                                          \
        fprintf(stderr, "vk_view: missing vk%s\n", #name);                     \
        return false;                                                          \
    }

#define VKV_LOAD_DEVICE(name)                                                  \
    static PFN_vk##name name##_fn = nullptr;                                      \
    if (!name##_fn)                                                            \
        name##_fn = s_gdpa                                                     \
            ? (PFN_vk##name)s_gdpa(s_device, "vk" #name)                       \
            : (PFN_vk##name)s_gpa(s_instance, "vk" #name);                     \
    if (!name##_fn) {                                                          \
        fprintf(stderr, "vk_view: missing vk%s\n", #name);                     \
        return false;                                                          \
    }

#define VKV_LOAD_DEVICE_VOID(name)                                             \
    static PFN_vk##name name##_fn = nullptr;                                      \
    if (!name##_fn)                                                            \
        name##_fn = s_gdpa                                                     \
            ? (PFN_vk##name)s_gdpa(s_device, "vk" #name)                       \
            : (PFN_vk##name)s_gpa(s_instance, "vk" #name);

static uint32_t memoryTypeIndex(uint32_t requiredBits, VkMemoryPropertyFlags wanted) {
    VKV_LOAD_INSTANCE(GetPhysicalDeviceMemoryProperties)
    VkPhysicalDeviceMemoryProperties props;
    GetPhysicalDeviceMemoryProperties_fn(s_phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (!(requiredBits & (1u << i)))
            continue;
        if ((props.memoryTypes[i].propertyFlags & wanted) == wanted)
            return i;
    }
    return UINT32_MAX;
}

static void destroyViewObjects(VkView *v) {
    VKV_LOAD_DEVICE_VOID(DestroyFramebuffer)
    VKV_LOAD_DEVICE_VOID(DestroyImageView)
    VKV_LOAD_DEVICE_VOID(DestroyImage)
    VKV_LOAD_DEVICE_VOID(FreeMemory)
    VKV_LOAD_DEVICE_VOID(DestroyRenderPass)

    if (s_device == VK_NULL_HANDLE)
        return;
    if ((*v).fb != VK_NULL_HANDLE && DestroyFramebuffer_fn)
        DestroyFramebuffer_fn(s_device, (*v).fb, nullptr);
    if ((*v).view != VK_NULL_HANDLE && DestroyImageView_fn)
        DestroyImageView_fn(s_device, (*v).view, nullptr);
    if ((*v).image != VK_NULL_HANDLE && DestroyImage_fn)
        DestroyImage_fn(s_device, (*v).image, nullptr);
    if ((*v).memory != VK_NULL_HANDLE && FreeMemory_fn)
        FreeMemory_fn(s_device, (*v).memory, nullptr);
    if ((*v).pass != VK_NULL_HANDLE && DestroyRenderPass_fn)
        DestroyRenderPass_fn(s_device, (*v).pass, nullptr);
    memset(v, 0, sizeof(VkView));
}

static bool buildCache(VkView *v) {
    VKV_LOAD_DEVICE(CreateImage)
    VKV_LOAD_DEVICE(AllocateMemory)
    VKV_LOAD_DEVICE(BindImageMemory)
    VKV_LOAD_DEVICE(CreateImageView)
    VKV_LOAD_DEVICE(CreateRenderPass)
    VKV_LOAD_DEVICE(CreateFramebuffer)
    VKV_LOAD_DEVICE(GetImageMemoryRequirements)

    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent.width = (uint32_t)(*v).cacheW;
    ici.extent.height = (uint32_t)(*v).cacheH;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKV_CHECK(CreateImage_fn(s_device, &ici, nullptr, &(*v).image), "cache CreateImage");

    VkMemoryRequirements req;
    GetImageMemoryRequirements_fn(s_device, (*v).image, &req);
    uint32_t typeIdx = memoryTypeIndex(req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (typeIdx == UINT32_MAX) {
        fprintf(stderr, "vk_view: no device-local memory type\n");
        return false;
    }
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = typeIdx;
    VKV_CHECK(AllocateMemory_fn(s_device, &mai, nullptr, &(*v).memory), "cache AllocateMemory");
    VKV_CHECK(BindImageMemory_fn(s_device, (*v).image, (*v).memory, 0), "cache BindImageMemory");

    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = (*v).image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_B8G8R8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    VKV_CHECK(CreateImageView_fn(s_device, &vci, nullptr, &(*v).view), "cache CreateImageView");

    // Single subpass, clear-on-load, handed to the blit on store. Initial
    // layout UNDEFINED is honest: every pass clears the whole cache anyway.
    VkAttachmentDescription att = {0};
    att.format = VK_FORMAT_B8G8R8A8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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
    VKV_CHECK(CreateRenderPass_fn(s_device, &rpci, nullptr, &(*v).pass), "cache CreateRenderPass");

    VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = (*v).pass;
    fci.attachmentCount = 1;
    fci.pAttachments = &(*v).view;
    fci.width = (uint32_t)(*v).cacheW;
    fci.height = (uint32_t)(*v).cacheH;
    fci.layers = 1;
    VKV_CHECK(CreateFramebuffer_fn(s_device, &fci, nullptr, &(*v).fb), "cache CreateFramebuffer");

    return true;
}

bool VkView_refreshAll(VkInstance instance, PFN_vkGetInstanceProcAddr gpa,
                       VkPhysicalDevice phys, VkDevice device) {
    if (!gpa || phys == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
        return false;
    s_gpa = gpa;
    s_instance = instance;
    s_phys = phys;
    s_device = device;
    s_gdpa = (PFN_vkGetDeviceProcAddr)gpa(instance, "vkGetDeviceProcAddr");

    size_t count = 0;
    DisplayMonitor **monitors = DisplayInfo_getMonitors(&count);

    // Keep views whose monitor still exists; drop the rest.
    for (size_t i = 0; i < s_viewCount;) {
        bool alive = false;
        for (size_t m = 0; m < count; m++) {
            if (DisplayMonitor_getId(monitors[m]) == (*(&s_views[i])).displayId) {
                alive = true;
                break;
            }
        }
        if (alive) {
            i++;
            continue;
        }
        destroyViewObjects(&s_views[i]);
        s_views[i] = s_views[s_viewCount - 1];
        s_viewCount--;
    }

    bool allBuilt = true;
    for (size_t m = 0; m < count; m++) {
        DisplayMonitor *mon = monitors[m];
        uint32_t id = DisplayMonitor_getId(mon);

        VkView *existing = nullptr;
        for (size_t i = 0; i < s_viewCount; i++) {
            if ((*&s_views[i]).displayId == id) {
                existing = &s_views[i];
                break;
            }
        }
        if (existing)
            continue;
        if (s_viewCount >= sizeof(s_views) / sizeof(s_views[0]))
            break;

        VkView *v = &s_views[s_viewCount];
        memset(v, 0, sizeof(VkView));
        (*v).displayId = id;
        // Desktop origin is not part of DisplayMonitor yet (discovery does
        // not capture CGDisplayBounds); single-monitor layouts start at 0,0
        // and multi-monitor mapping slots in when discovery grows origins.
        (*v).originX = 0.0f;
        (*v).originY = 0.0f;
        (*v).pointW = DisplayMonitor_getPointWidth(mon);
        (*v).pointH = DisplayMonitor_getPointHeight(mon);

        // THE GIANT BUFFER: the ACTIVE pixel mode first — that is literally
        // the plane the window server composites, so cache pixels map 1:1 to
        // glass and every blit stays a copy. Native panel grid is the
        // fallback (it can exceed reality when macOS advertises virtual
        // scaled modes); points last as pure desperation.
        int32_t w = DisplayMonitor_getCurrentWidth(mon);
        int32_t h = DisplayMonitor_getCurrentHeight(mon);
        if (w <= 0 || h <= 0) {
            w = DisplayMonitor_getNativeWidth(mon);
            h = DisplayMonitor_getNativeHeight(mon);
        }
        if (w <= 0 || h <= 0) {
            w = DisplayMonitor_getPointWidth(mon);
            h = DisplayMonitor_getPointHeight(mon);
        }
        (*v).cacheW = w;
        (*v).cacheH = h;

        if (!buildCache(v)) {
            destroyViewObjects(v);
            allBuilt = false;
            continue;
        }
        fprintf(stderr, "vk_view: monitor %u cache %dx%d (points %dx%d)\n",
                id, (*v).cacheW, (*v).cacheH, (*v).pointW, (*v).pointH);
        s_viewCount++;
    }
    return allBuilt && s_viewCount > 0;
}

size_t VkView_count(void) {
    return s_viewCount;
}

VkView *VkView_at(size_t index) {
    return index < s_viewCount ? &s_views[index] : nullptr;
}

VkView *VkView_forPoint(float x, float y) {
    for (size_t i = 0; i < s_viewCount; i++) {
        VkView *v = &s_views[i];
        if (x >= (*v).originX && y >= (*v).originY &&
            x < (*v).originX + (float)(*v).pointW &&
            y < (*v).originY + (float)(*v).pointH)
            return v;
    }
    return nullptr;
}

VkView *VkView_forMonitor(uint32_t displayId) {
    if (displayId == 0)
        return nullptr;
    for (size_t i = 0; i < s_viewCount; i++) {
        if ((*&s_views[i]).displayId == displayId)
            return &s_views[i];
    }
    return nullptr;
}

float VkView_getOriginX(const VkView *view) {
    return view ? (*view).originX : 0.0f;
}

float VkView_getOriginY(const VkView *view) {
    return view ? (*view).originY : 0.0f;
}

int32_t VkView_getWidth(const VkView *view) {
    return view ? (*view).cacheW : 0;
}

int32_t VkView_getHeight(const VkView *view) {
    return view ? (*view).cacheH : 0;
}

int32_t VkView_getPointWidth(const VkView *view) {
    return view ? (*view).pointW : 0;
}

int32_t VkView_getPointHeight(const VkView *view) {
    return view ? (*view).pointH : 0;
}

VkRenderPass VkView_renderPass(const VkView *view) {
    return view ? (*view).pass : VK_NULL_HANDLE;
}

VkImage VkView_image(const VkView *view) {
    return view ? (*view).image : VK_NULL_HANDLE;
}

bool VkView_beginPass(VkView *view, VkCommandBuffer cb,
                      float r, float g, float b, float a) {
    if (!VkGuard_checkResource("VkView_beginPass", s_device, false))
        return false;
    if (!view || (*view).fb == VK_NULL_HANDLE)
        return false;
    VKV_LOAD_DEVICE(CmdBeginRenderPass)

    VkClearValue clear = {0};
    clear.color.float32[0] = r;
    clear.color.float32[1] = g;
    clear.color.float32[2] = b;
    clear.color.float32[3] = a;

    VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = (*view).pass;
    rbi.framebuffer = (*view).fb;
    rbi.renderArea.extent = (VkExtent2D){
        .width = (uint32_t)(*view).cacheW,
        .height = (uint32_t)(*view).cacheH
    };
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    CmdBeginRenderPass_fn(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

bool VkView_endPass(VkView *view, VkCommandBuffer cb) {
    if (!view)
        return false;
    (void)view;
    VKV_LOAD_DEVICE(CmdEndRenderPass)
    CmdEndRenderPass_fn(cb);

    // Hand the finished cache to the blit: COLOR_ATTACHMENT_OUT ->
    // TRANSFER_READ on the image itself (renderpass already left it at
    // TRANSFER_SRC layout).
    VKV_LOAD_DEVICE(CmdPipelineBarrier)
    VkImageMemoryBarrier bar = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.image = (*view).image;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar.subresourceRange.levelCount = 1;
    bar.subresourceRange.layerCount = 1;
    CmdPipelineBarrier_fn(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    return true;
}

void VkView_shutdown(void) {
    for (size_t i = 0; i < s_viewCount; i++)
        destroyViewObjects(&s_views[i]);
    s_viewCount = 0;
}
