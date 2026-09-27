#include "vulkan/vk_graphics.h"
#include "vulkan/vk_device.h"

#include <limits.h>
#include <string.h>
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * CLASS: VkGraphics. A single borrowed Vulkan device backs a retained RGBA8
 * transfer image and coherent readback buffer. Resize creates these and one
 * reusable command buffer/fence; begin/clear/end record and submit without
 * allocating. No surface or window presentation exists in this milestone.
 * A pending fence is waited for at most 100ms per call; timeout preserves all
 * resources until retry. The caller serializes access to this process row.
 */
;;OVERVIEW
/**
 * CLASS: VkGraphics (private instance; one process row)
 * STRUCT FIELDS:
 *   Device *owner; VkPhysicalDevice physical; VkDevice device; VkQueue queue;
 *   uint32_t family; VkInstance instance; PFN_vkGetInstanceProcAddr gpa;
 *   VkImage image; VkDeviceMemory imageMemory; VkBuffer buffer;
 *   VkDeviceMemory bufferMemory; VkCommandPool pool; VkCommandBuffer cmd;
 *   VkFence fence; uint32_t width, height; size_t bytes;
 *   VkImageLayout layout; bool recording, pending, completed, cleared;
 * FUNCTION REGISTRY:
 * Public Core: VkGraphics_bind, VkGraphics_unbind, VkGraphics_unbindIfDevice,
 *              VkGraphics_getRow, VkGraphics_readback.
 * Private Core: waitPending, releaseTarget, resize, begin, clear, end, clip,
 *               unsupported drawable verbs, loadFns, chooseMemory.
 */

typedef struct VkGraphics {
    Device *owner;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkInstance instance;
    PFN_vkGetInstanceProcAddr gpa;
    VkImage image;
    VkDeviceMemory imageMemory;
    VkBuffer buffer;
    VkDeviceMemory bufferMemory;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    uint32_t width;
    uint32_t height;
    size_t bytes;
    VkImageLayout layout;
    bool recording;
    bool pending;
    bool completed;
    bool cleared;
} VkGraphics;

static VkGraphics s;
#define FN(name) static PFN_##name p_##name
FN(vkGetPhysicalDeviceMemoryProperties);
FN(vkGetPhysicalDeviceFormatProperties);
FN(vkCreateImage); FN(vkDestroyImage); FN(vkGetImageMemoryRequirements);
FN(vkAllocateMemory); FN(vkFreeMemory); FN(vkBindImageMemory);
FN(vkCreateBuffer); FN(vkDestroyBuffer); FN(vkGetBufferMemoryRequirements);
FN(vkBindBufferMemory); FN(vkMapMemory); FN(vkUnmapMemory);
FN(vkCreateCommandPool); FN(vkDestroyCommandPool); FN(vkAllocateCommandBuffers);
FN(vkResetCommandBuffer); FN(vkBeginCommandBuffer); FN(vkEndCommandBuffer);
FN(vkCmdPipelineBarrier); FN(vkCmdClearColorImage); FN(vkCmdCopyImageToBuffer);
FN(vkCreateFence); FN(vkDestroyFence); FN(vkWaitForFences);
FN(vkResetFences); FN(vkQueueSubmit);
#undef FN

#define LOAD(name) do { p_##name = (PFN_##name) (*s.gpa)(s.instance, #name); if (p_##name == nullptr) return false; } while (0)
static bool loadFns(void) {
    LOAD(vkGetPhysicalDeviceMemoryProperties);
    LOAD(vkGetPhysicalDeviceFormatProperties);
    LOAD(vkCreateImage); LOAD(vkDestroyImage); LOAD(vkGetImageMemoryRequirements);
    LOAD(vkAllocateMemory); LOAD(vkFreeMemory); LOAD(vkBindImageMemory);
    LOAD(vkCreateBuffer); LOAD(vkDestroyBuffer); LOAD(vkGetBufferMemoryRequirements);
    LOAD(vkBindBufferMemory); LOAD(vkMapMemory); LOAD(vkUnmapMemory);
    LOAD(vkCreateCommandPool); LOAD(vkDestroyCommandPool); LOAD(vkAllocateCommandBuffers);
    LOAD(vkResetCommandBuffer); LOAD(vkBeginCommandBuffer); LOAD(vkEndCommandBuffer);
    LOAD(vkCmdPipelineBarrier); LOAD(vkCmdClearColorImage); LOAD(vkCmdCopyImageToBuffer);
    LOAD(vkCreateFence); LOAD(vkDestroyFence); LOAD(vkWaitForFences);
    LOAD(vkResetFences); LOAD(vkQueueSubmit);
    return true;
}
#undef LOAD

static bool waitPending(void) {
    if (!s.pending)
        return true;
    VkResult r = p_vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, 100000000ULL);
    if (r != VK_SUCCESS)
        return false;
    s.pending = false;
    s.completed = s.cleared;
    return true;
}

static void releaseTarget(void) {
    if (s.image != VK_NULL_HANDLE)
        p_vkDestroyImage(s.device, s.image, nullptr);
    if (s.buffer != VK_NULL_HANDLE)
        p_vkDestroyBuffer(s.device, s.buffer, nullptr);
    if (s.imageMemory != VK_NULL_HANDLE)
        p_vkFreeMemory(s.device, s.imageMemory, nullptr);
    if (s.bufferMemory != VK_NULL_HANDLE)
        p_vkFreeMemory(s.device, s.bufferMemory, nullptr);
    s.image = VK_NULL_HANDLE;
    s.buffer = VK_NULL_HANDLE;
    s.imageMemory = VK_NULL_HANDLE;
    s.bufferMemory = VK_NULL_HANDLE;
    s.width = s.height = 0;
    s.bytes = 0;
    s.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    s.completed = false;
}

void VkGraphics_unbind(void) {
    if (s.device == VK_NULL_HANDLE || s.recording || !waitPending())
        return; // keep borrowed resources alive while GPU work can still use them
    releaseTarget();
    if (s.fence != VK_NULL_HANDLE)
        p_vkDestroyFence(s.device, s.fence, nullptr);
    if (s.pool != VK_NULL_HANDLE)
        p_vkDestroyCommandPool(s.device, s.pool, nullptr);
    memset(&s, 0, sizeof(s));
}

bool VkGraphics_unbindIfDevice(VkDevice native) {
    if (native == VK_NULL_HANDLE || native != s.device)
        return true;
    VkGraphics_unbind();
    return s.device == VK_NULL_HANDLE;
}

bool VkGraphics_bind(Device *device) {
    if (s.device != VK_NULL_HANDLE)
        return device == s.owner;
    VkGraphics candidate = {0};
    if (!VkDevice_borrow(device, &candidate.physical, &candidate.device,
                         &candidate.queue, &candidate.family, &candidate.gpa,
                         &candidate.instance))
        return false;
    candidate.owner = device;
    s = candidate;
    if (!loadFns()) {
        memset(&s, 0, sizeof(s));
        return false;
    }
    VkFormatProperties formats;
    p_vkGetPhysicalDeviceFormatProperties(s.physical, VK_FORMAT_R8G8B8A8_UNORM, &formats);
    if (!(formats.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) ||
        !(formats.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
        memset(&s, 0, sizeof(s));
        return false;
    }
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = s.family };
    if (p_vkCreateCommandPool(s.device, &pci, nullptr, &s.pool) != VK_SUCCESS)
        goto fail;
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    if (p_vkAllocateCommandBuffers(s.device, &ai, &s.cmd) != VK_SUCCESS)
        goto fail;
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (p_vkCreateFence(s.device, &fci, nullptr, &s.fence) != VK_SUCCESS)
        goto fail;
    return true;
fail:
    VkGraphics_unbind();
    return false;
}

static bool chooseMemory(uint32_t bits, VkMemoryPropertyFlags flags, uint32_t *dest) {
    VkPhysicalDeviceMemoryProperties props;
    p_vkGetPhysicalDeviceMemoryProperties(s.physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            *dest = i;
            return true;
        }
    }
    return false;
}

static bool resize(uint32_t width, uint32_t height) {
    if (s.device == VK_NULL_HANDLE || s.recording || !waitPending() || width == 0 || height == 0 ||
        (uint64_t) width * height > SIZE_MAX / 4u)
        return false;
    if (s.width == width && s.height == height)
        return true;
    releaseTarget();
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    if (p_vkCreateImage(s.device, &ici, nullptr, &s.image) != VK_SUCCESS)
        goto fail;
    VkMemoryRequirements req;
    p_vkGetImageMemoryRequirements(s.device, s.image, &req);
    uint32_t type;
    if (!chooseMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type))
        goto fail;
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type };
    if (p_vkAllocateMemory(s.device, &mai, nullptr, &s.imageMemory) != VK_SUCCESS ||
        p_vkBindImageMemory(s.device, s.image, s.imageMemory, 0) != VK_SUCCESS)
        goto fail;
    VkDeviceSize bytes = (VkDeviceSize) width * height * 4u;
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (p_vkCreateBuffer(s.device, &bci, nullptr, &s.buffer) != VK_SUCCESS)
        goto fail;
    p_vkGetBufferMemoryRequirements(s.device, s.buffer, &req);
    if (!chooseMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &type))
        goto fail;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (p_vkAllocateMemory(s.device, &mai, nullptr, &s.bufferMemory) != VK_SUCCESS ||
        p_vkBindBufferMemory(s.device, s.buffer, s.bufferMemory, 0) != VK_SUCCESS)
        goto fail;
    s.width = width;
    s.height = height;
    s.bytes = (size_t) bytes;
    return true;
fail:
    releaseTarget();
    return false;
}

static bool begin(void) {
    if (s.image == VK_NULL_HANDLE || s.recording || !waitPending() ||
        p_vkResetCommandBuffer(s.cmd, 0) != VK_SUCCESS)
        return false;
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    if (p_vkBeginCommandBuffer(s.cmd, &bi) != VK_SUCCESS)
        return false;
    s.recording = true;
    s.cleared = false;
    s.completed = false;
    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = s.layout, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = s.image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        .srcAccessMask = s.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
            s.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT };
    p_vkCmdPipelineBarrier(s.cmd, s.layout == VK_IMAGE_LAYOUT_UNDEFINED ?
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    return true;
}

static bool clear(uint32_t color) {
    if (!s.recording)
        return false;
    VkClearColorValue value = { .float32 = {
        (float) ((color >> 24) & 255u) / 255.0f,
        (float) ((color >> 16) & 255u) / 255.0f,
        (float) ((color >> 8) & 255u) / 255.0f,
        (float) (color & 255u) / 255.0f } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    p_vkCmdClearColorImage(s.cmd, s.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    s.cleared = true;
    return true;
}

static bool end(void) {
    if (!s.recording)
        return false;
    if (s.cleared) {
        VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s.image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        p_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region = { .bufferOffset = 0,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { s.width, s.height, 1 } };
        p_vkCmdCopyImageToBuffer(s.cmd, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 s.buffer, 1, &region);
    }
    s.recording = false;
    if (p_vkEndCommandBuffer(s.cmd) != VK_SUCCESS)
        return false;
    if (p_vkResetFences(s.device, 1, &s.fence) != VK_SUCCESS)
        return false;
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &s.cmd };
    if (p_vkQueueSubmit(s.queue, 1, &submit, s.fence) != VK_SUCCESS) {
        // Fence is unsignaled; recreate it before any later submit.
        p_vkDestroyFence(s.device, s.fence, nullptr);
        s.fence = VK_NULL_HANDLE;
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (p_vkCreateFence(s.device, &fci, nullptr, &s.fence) != VK_SUCCESS)
            return false;
        return false;
    }
    s.pending = true;
    s.layout = s.cleared ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    return true;
}

bool VkGraphics_readback(size_t capacity, uint8_t *dest) {
    if (dest == nullptr || s.image == VK_NULL_HANDLE || s.recording ||
        capacity < s.bytes || !waitPending() || !s.completed)
        return false;
    void *mapped = nullptr;
    if (p_vkMapMemory(s.device, s.bufferMemory, 0, s.bytes, 0, &mapped) != VK_SUCCESS)
        return false;
    memcpy(dest, mapped, s.bytes);
    p_vkUnmapMemory(s.device, s.bufferMemory);
    return true;
}

static bool present(void) { return false; }
static bool clip(const Rectangle *rect) { return rect == nullptr && s.recording; }
static bool fillRect(const Rectangle *rect, const Brush *brush) { (void) rect; (void) brush; return false; }
static bool drawRect(const Rectangle *rect, const Stroke *stroke) { (void) rect; (void) stroke; return false; }
static bool fillCircle(float x, float y, float radius, const Brush *brush) { (void) x; (void) y; (void) radius; (void) brush; return false; }
static bool drawCircle(float x, float y, float radius, const Stroke *stroke) { (void) x; (void) y; (void) radius; (void) stroke; return false; }
static bool fillPath(const Shape *shape, const Brush *brush) { (void) shape; (void) brush; return false; }
static bool drawPath(const Shape *shape, const Stroke *stroke) { (void) shape; (void) stroke; return false; }
static bool drawText(const Rectangle *rect, const char *text, const Brush *brush) { (void) rect; (void) text; (void) brush; return false; }
static bool drawImage(const Image *image, const Rectangle *dst) { (void) image; (void) dst; return false; }

static const Graphics row = {
    .backendId = LANG_BACKEND_VULKAN, .begin = begin, .end = end,
    .present = present, .resize = resize, .clear = clear, .clip = clip,
    .fillRect = fillRect, .drawRect = drawRect, .fillCircle = fillCircle,
    .drawCircle = drawCircle, .fillPath = fillPath, .drawPath = drawPath,
    .drawText = drawText, .drawImage = drawImage
};
const Graphics *VkGraphics_getRow(void) { return &row; }
