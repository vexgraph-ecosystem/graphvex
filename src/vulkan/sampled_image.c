#include "vulkan/sampled_image.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "exception/throw.h"
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/* SampledImage owns one immutable completed GPU texture and its compatible
 * single-sampler descriptor. Image and recorded frames hold references; releasing
 * the original Image cannot free a texture still being drawn. CPU input upload
 * is cold and one-shot, never image-content geometry. A timed-out upload retains
 * staging, command pool and fence until polling proves completion. Device is
 * borrowed, and all operations require owner-thread/external synchronization. */
;;OVERVIEW
/* CLASS: SampledImage. Fields, in declaration order: owner (borrowed Device),
 * device (borrowed native device), image/memory/view (owned texture), sampler,
 * layout/pool/set (owned descriptor resources), width/height (native pixels),
 * refs (owner/frame count), commands/fence/staging/stagingMemory (owned upload),
 * pending (upload completion uncertain). No helpers with stored state.
 * Public: _0/_2/_5/chooser/zero; poll, retain, release, isReady, bindImage; width/height,
 * device/descriptor; bounded value/structure strings. Private: memoryType,
 * uploadRelease, dispose, adopt, format. Final release false retains everything
 * on timeout. Cold creation rejects null/invalid/overflow with one THROW;
 * poll/release stay silent and bounded during teardown. */
struct SampledImage {
    Device *owner;
    VkDevice device;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    VkDescriptorSetLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    uint32_t width, height, refs;
    VkCommandPool commands;
    VkFence fence;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    bool pending;
};
static const uint64_t SAMPLE_WAIT_NS = UINT64_C(100000000);
// Selects a compatible physical-device memory type containing every requested property flag.
static uint32_t memoryType(Device *owner, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties((VkPhysicalDevice) Device_physical(owner), &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryType *type = &properties.memoryTypes[i];
        if ((bits & (1u << i)) && ((*type).propertyFlags & want) == want)
            return i;
    }
    return UINT32_MAX;
}
// Releases staging resources and upload synchronization retained by the pending transfer.
static void uploadRelease(SampledImage *self) {
    VkDevice d = (*self).device;
    if ((*self).commands)
        vkDestroyCommandPool(d, (*self).commands, nullptr);
    if ((*self).fence)
        vkDestroyFence(d, (*self).fence, nullptr);
    if ((*self).staging)
        vkDestroyBuffer(d, (*self).staging, nullptr);
    if ((*self).stagingMemory)
        vkFreeMemory(d, (*self).stagingMemory, nullptr);
    (*self).commands = VK_NULL_HANDLE;
    (*self).fence = VK_NULL_HANDLE;
    (*self).staging = VK_NULL_HANDLE;
    (*self).stagingMemory = VK_NULL_HANDLE;
}
// Polls the bounded upload fence and retires staging state when the transfer completes.
bool SampledImage_poll(SampledImage *self) {
    if (!self)
        return false;
    if ((*self).pending) {
        if (vkWaitForFences((*self).device, 1, &(*self).fence, VK_TRUE, SAMPLE_WAIT_NS) != VK_SUCCESS)
            return false;
        (*self).pending = false;
        uploadRelease(self);
    }
    return true;
}
// Releases sampled-image resources, optionally destroying an internally owned VkImage.
static void dispose(SampledImage *self, bool ownedImage) {
    VkDevice d = (*self).device;
    uploadRelease(self);
    if ((*self).pool)
        vkDestroyDescriptorPool(d, (*self).pool, nullptr);
    if ((*self).layout)
        vkDestroyDescriptorSetLayout(d, (*self).layout, nullptr);
    if ((*self).sampler)
        vkDestroySampler(d, (*self).sampler, nullptr);
    if ((*self).view)
        vkDestroyImageView(d, (*self).view, nullptr);
    if (ownedImage && (*self).image)
        vkDestroyImage(d, (*self).image, nullptr);
    if (ownedImage && (*self).memory)
        vkFreeMemory(d, (*self).memory, nullptr);
    free(self);
}
// Adds a reference to a valid sampled image unless its reference count is saturated.
bool SampledImage_retain(SampledImage *self) {
    if (!self || (*self).refs == UINT32_MAX)
        return false;
    ++(*self).refs;
    return true;
}
// Drops a reference and disposes the sampled image when the count reaches zero.
bool SampledImage_release(SampledImage *self) {
    if (!self)
        return true;
    if ((*self).refs == 1) {
        if (!SampledImage_poll(self))
            return false;
        dispose(self, true);
    } else
        --(*self).refs;
    return true;
}
// Reports whether the asynchronous upload is no longer pending.
bool SampledImage_isReady(const SampledImage *self) { return self && !(*self).pending; }
// Adapts Image's opaque retain callback to a SampledImage reference increment.
static bool retainOpaque(void *resource) { return SampledImage_retain(resource); }
// Adapts Image's opaque release callback to a SampledImage reference decrement.
static bool releaseOpaque(void *resource) { return SampledImage_release(resource); }
// Binds this resource into an Image, transferring one retained reference through its callback pair.
bool SampledImage_bindImage(SampledImage *self, Image *image) {
    if (!SampledImage_isReady(self) || !image || Image_width(image) != (*self).width ||
        Image_height(image) != (*self).height) {
        THROW("SampledImage binding requires matching completed image");
        return false;
    }
    return Image_bindGpu(image, self, (*self).owner, (void*) (*self).set, retainOpaque, releaseOpaque);
}
// Returns the sampled image width, or zero for null.
uint32_t SampledImage_width(const SampledImage *self) { return self ? (*self).width : 0; }
// Returns the sampled image height, or zero for null.
uint32_t SampledImage_height(const SampledImage *self) { return self ? (*self).height : 0; }
Device *SampledImage_device(const SampledImage *self) { return self ? (*self).owner : nullptr; }
void *SampledImage_descriptor(const SampledImage *self) { return self ? (void*) (*self).set : nullptr; }
SampledImage *SampledImage_0(void) { return nullptr; }
SampledImage *SampledImage_zero(void) { return SampledImage_0(); }

static SampledImage *adopt(Device *owner, VkImage image, VkDeviceMemory memory,
                           uint32_t width, uint32_t height) {
    SampledImage *self = calloc(1, sizeof *self);
    if (!self)
        return nullptr;
    (*self).owner = owner;
    (*self).device = (VkDevice) Device_native(owner);
    (*self).image = image;
    (*self).memory = memory;
    (*self).width = width;
    (*self).height = height;
    (*self).refs = 1;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size};
    VkDevice d = (*self).device;
    if (vkCreateImageView(d, &view, nullptr, &(*self).view) != VK_SUCCESS ||
        vkCreateSampler(d, &sampler, nullptr, &(*self).sampler) != VK_SUCCESS ||
        vkCreateDescriptorSetLayout(d, &layout, nullptr, &(*self).layout) != VK_SUCCESS ||
        vkCreateDescriptorPool(d, &pool, nullptr, &(*self).pool) != VK_SUCCESS)
        goto failed;
    VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = (*self).pool, .descriptorSetCount = 1, .pSetLayouts = &(*self).layout};
    if (vkAllocateDescriptorSets(d, &allocate, &(*self).set) != VK_SUCCESS)
        goto failed;
    VkDescriptorImageInfo info = {(*self).sampler, (*self).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = (*self).set, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &info};
    vkUpdateDescriptorSets(d, 1, &write, 0, nullptr);
    return self;
failed:
    dispose(self, false);
    return nullptr;
}
SampledImage *SampledImage_5(Device *owner, void *image, void *memory, uint32_t width, uint32_t height) {
    if (!Device_isValid(owner) || !image || !memory || !width || !height) {
        THROW("SampledImage adoption requires a completed device-owned texture");
        return nullptr;
    }
    SampledImage *self = adopt(owner, (VkImage) image, (VkDeviceMemory) memory, width, height);
    if (!self)
        THROW("SampledImage descriptor allocation failed");
    return self;
}
SampledImage *SampledImage_2(Device *owner, const Image *source) {
    uint32_t w = Image_width(source), h = Image_height(source);
    size_t stride = Image_stride(source);
    if (!Device_isValid(owner) || !Image_isValid(source) || !Image_pixels(source) ||
        Image_format(source) != IMAGE_FORMAT_RGBA8 || !w || !h ||
        (uint64_t) w * h > SIZE_MAX / 4u || stride < (size_t) w * 4u || stride > SIZE_MAX / h) {
        THROW("SampledImage upload rejected invalid RGBA shadow");
        return nullptr;
    }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties((VkPhysicalDevice) Device_physical(owner), &properties);
    VkPhysicalDeviceLimits *limits = &properties.limits;
    if (w > (*limits).maxImageDimension2D || h > (*limits).maxImageDimension2D) {
        THROW("SampledImage upload exceeds device extent");
        return nullptr;
    }
    VkDevice d = (VkDevice) Device_native(owner);
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo texture = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {w,h,1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    SampledImage *self = nullptr;
    if (vkCreateImage(d, &texture, nullptr, &image) != VK_SUCCESS)
        goto failed;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(d, image, &requirements);
    uint32_t type = memoryType(owner, requirements.memoryTypeBits, 0);
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    if (type == UINT32_MAX || vkAllocateMemory(d, &allocation, nullptr, &memory) != VK_SUCCESS ||
        vkBindImageMemory(d, image, memory, 0) != VK_SUCCESS)
        goto failed;
    self = adopt(owner, image, memory, w, h);
    if (!self)
        goto failed;
    VkDeviceSize Bytes = (VkDeviceSize) w * h * 4u;
    VkBufferCreateInfo staging = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = Bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    if (vkCreateBuffer(d, &staging, nullptr, &(*self).staging) != VK_SUCCESS)
        goto failed;
    vkGetBufferMemoryRequirements(d, (*self).staging, &requirements);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(owner, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(d, &allocation, nullptr, &(*self).stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(d, (*self).staging, (*self).stagingMemory, 0) != VK_SUCCESS)
        goto failed;
    void *mappedPixelBytes = nullptr;
    if (vkMapMemory(d, (*self).stagingMemory, 0, Bytes, 0, &mappedPixelBytes) != VK_SUCCESS)
        goto failed;
    for (uint32_t y = 0; y < h; ++y)
        memcpy((uint8_t*) mappedPixelBytes + (size_t) y * w * 4u,
            Image_pixels(source) + (size_t) y * stride, (size_t) w * 4u);
    vkUnmapMemory(d, (*self).stagingMemory);
    VkCommandPoolCreateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = Device_queueFamily(owner)};
    VkCommandBufferAllocateInfo commandInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateCommandPool(d, &commands, nullptr, &(*self).commands) != VK_SUCCESS ||
        vkCreateFence(d, &fence, nullptr, &(*self).fence) != VK_SUCCESS)
        goto failed;
    commandInfo.commandPool = (*self).commands;
    if (vkAllocateCommandBuffers(d, &commandInfo, &command) != VK_SUCCESS)
        goto failed;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
        goto failed;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .imageExtent = {w,h,1}};
    vkCmdCopyBufferToImage(command, (*self).staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    if (vkEndCommandBuffer(command) != VK_SUCCESS)
        goto failed;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    VkResult result = vkQueueSubmit((VkQueue) Device_queue(owner), 1, &submit, (*self).fence);
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
        goto failed;
    (*self).pending = true;
    SampledImage_poll(self);
    return self;
failed:
    if (self)
        dispose(self, true);
    else {
        if (image)
            vkDestroyImage(d, image, nullptr);
        if (memory)
            vkFreeMemory(d, memory, nullptr);
    }
    THROW("SampledImage GPU upload allocation or recording failed");
    return nullptr;
}
// Writes the bounded value or one-level field projection of a sampled image.
static void format(const SampledImage *self, bool structure, char *dest, size_t cap, bool *truncated) {
    int n = 0;
    if (!dest || !cap) {
        if (truncated)
            *truncated = true;
        return;
    }
    if (!self)
        n = snprintf(dest, cap, "nullptr");
    else if (!structure)
        n = snprintf(dest, cap, "SampledImage(%ux%u,refs=%u,pending=%d)",
            (*self).width, (*self).height, (*self).refs, (*self).pending);
    else
        n = snprintf(dest, cap, "SampledImage{owner=%p,device=%p,image=%p,memory=%p,view=%p,sampler=%p,layout=%p,pool=%p,set=%p,width=%u,height=%u,refs=%u,commands=%p,fence=%p,staging=%p,stagingMemory=%p,pending=%d}",
            (void*) (*self).owner, (void*) (*self).device, (void*) (*self).image, (void*) (*self).memory,
            (void*) (*self).view, (void*) (*self).sampler, (void*) (*self).layout, (void*) (*self).pool,
            (void*) (*self).set, (*self).width, (*self).height, (*self).refs, (void*) (*self).commands,
            (void*) (*self).fence, (void*) (*self).staging, (void*) (*self).stagingMemory, (*self).pending);
    if (truncated)
        *truncated = n < 0 || (size_t) n >= cap;
}
// Formats a concise bounded summary of the sampled image.
void SampledImage_toString(const SampledImage *self, char *dest, size_t cap, bool *truncated) { format(self,false,dest,cap,truncated); }
// Formats the sampled-image fields into caller-provided bounded storage.
void SampledImage_toStringStruct(const SampledImage *self, char *dest, size_t cap, bool *truncated) { format(self,true,dest,cap,truncated); }
