#include "vulkan/vk_graphics.h"
#include "vulkan/vk_device.h"
#include "vulkan/vk_swapchain.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * CLASS: VkGraphics. A single borrowed Vulkan device backs a retained RGBA8
 * transfer/render image and coherent readback buffer. Resize creates these and
 * one reusable command buffer/fence; begin/clear/clip/fillRect/end record
 * without allocating. Precompiled SPIR-V is read on resize, never per draw.
 * Windowed devices own a single VkSwapchain and borrow the completed image
 * for a GPU-only blit; no CPU map or readback occurs on the present path.
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
 *   VkImageLayout layout; VkRenderPass pass; VkFramebuffer framebuffer;
 *   VkImageView view; VkPipelineLayout pipelineLayout; VkPipeline pipeline;
 *   bool drawing, clipEnabled; Rectangle clipRect;
 *   bool recording, pending, completed, cleared, dirty;
 *   VkSwapchain *chain; // optional windowed presentation owner
 * FUNCTION REGISTRY:
 * Public Core: VkGraphics_bind, VkGraphics_unbind, VkGraphics_unbindIfDevice,
 *              VkGraphics_getRow, VkGraphics_readback, VkGraphics_borrowPresentImage.
 * Private Core: waitPending, releaseTarget, resize, begin, clear, end, clip,
 *               fillRect, startDrawing, stopDrawing, buildPipeline, loadShader,
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
    VkRenderPass pass;
    VkFramebuffer framebuffer;
    VkImageView view;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    bool drawing;
    bool clipEnabled;
    Rectangle clipRect;
    bool recording;
    bool pending;
    bool completed;
    bool cleared;
    bool dirty;
    VkSwapchain *chain;
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
FN(vkCreateRenderPass); FN(vkDestroyRenderPass);
FN(vkCreateImageView); FN(vkDestroyImageView);
FN(vkCreateFramebuffer); FN(vkDestroyFramebuffer);
FN(vkCreateShaderModule); FN(vkDestroyShaderModule);
FN(vkCreatePipelineLayout); FN(vkDestroyPipelineLayout);
FN(vkCreateGraphicsPipelines); FN(vkDestroyPipeline);
FN(vkCmdBeginRenderPass); FN(vkCmdEndRenderPass);
FN(vkCmdBindPipeline); FN(vkCmdSetViewport); FN(vkCmdSetScissor);
FN(vkCmdPushConstants); FN(vkCmdDraw);
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
    LOAD(vkCreateRenderPass); LOAD(vkDestroyRenderPass);
    LOAD(vkCreateImageView); LOAD(vkDestroyImageView);
    LOAD(vkCreateFramebuffer); LOAD(vkDestroyFramebuffer);
    LOAD(vkCreateShaderModule); LOAD(vkDestroyShaderModule);
    LOAD(vkCreatePipelineLayout); LOAD(vkDestroyPipelineLayout);
    LOAD(vkCreateGraphicsPipelines); LOAD(vkDestroyPipeline);
    LOAD(vkCmdBeginRenderPass); LOAD(vkCmdEndRenderPass);
    LOAD(vkCmdBindPipeline); LOAD(vkCmdSetViewport); LOAD(vkCmdSetScissor);
    LOAD(vkCmdPushConstants); LOAD(vkCmdDraw);
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
    if (s.framebuffer != VK_NULL_HANDLE)
        p_vkDestroyFramebuffer(s.device, s.framebuffer, nullptr);
    if (s.view != VK_NULL_HANDLE)
        p_vkDestroyImageView(s.device, s.view, nullptr);
    s.view = VK_NULL_HANDLE;
    if (s.pipeline != VK_NULL_HANDLE)
        p_vkDestroyPipeline(s.device, s.pipeline, nullptr);
    s.framebuffer = VK_NULL_HANDLE;
    s.pipeline = VK_NULL_HANDLE;
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
    if (s.chain && !VkSwapchain_destroy(s.chain))
        return;
    s.chain = nullptr;
    releaseTarget();
    if (s.pipelineLayout != VK_NULL_HANDLE)
        p_vkDestroyPipelineLayout(s.device, s.pipelineLayout, nullptr);
    if (s.pass != VK_NULL_HANDLE)
        p_vkDestroyRenderPass(s.device, s.pass, nullptr);
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
        !(formats.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) ||
        !(formats.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
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
    VkAttachmentDescription attachment = { .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference ref = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &ref };
    VkRenderPassCreateInfo rp = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &sub };
    if (p_vkCreateRenderPass(s.device, &rp, nullptr, &s.pass) != VK_SUCCESS)
        goto fail;
    VkPushConstantRange push = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0, .size = 32 };
    VkPipelineLayoutCreateInfo pl = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
    if (p_vkCreatePipelineLayout(s.device, &pl, nullptr, &s.pipelineLayout) != VK_SUCCESS)
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

static void stopDrawing(void);
static bool startDrawing(void);
static bool buildPipeline(void);

static VkShaderModule loadShader(const char *name) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", GRAPHVEX_SPV_DIR, name);
    if (n <= 0 || (size_t) n >= sizeof(path))
        return VK_NULL_HANDLE;
    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return VK_NULL_HANDLE;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return VK_NULL_HANDLE; }
    long length = ftell(file);
    if (length <= 0 || length % 4 != 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return VK_NULL_HANDLE;
    }
    uint32_t *words = (uint32_t*) malloc((size_t) length);
    if (words == nullptr) { fclose(file); return VK_NULL_HANDLE; }
    bool ok = fread(words, 1, (size_t) length, file) == (size_t) length;
    fclose(file);
    VkShaderModule module = VK_NULL_HANDLE;
    if (ok) {
        VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = (size_t) length, .pCode = words };
        (void) p_vkCreateShaderModule(s.device, &ci, nullptr, &module);
    }
    free(words);
    return module;
}

static bool buildPipeline(void) {
    VkShaderModule vert = loadShader("solid_quad_vert.spv");
    VkShaderModule frag = loadShader("solid_quad_frag.spv");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        if (vert != VK_NULL_HANDLE) p_vkDestroyShaderModule(s.device, vert, nullptr);
        if (frag != VK_NULL_HANDLE) p_vkDestroyShaderModule(s.device, frag, nullptr);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" }
    };
    VkPipelineVertexInputStateCreateInfo input = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1 };
    VkDynamicState states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = states };
    VkPipelineRasterizationStateCreateInfo raster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blend = { .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend };
    VkGraphicsPipelineCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input,
        .pInputAssemblyState = &assembly, .pViewportState = &vp,
        .pRasterizationState = &raster, .pMultisampleState = &ms,
        .pColorBlendState = &cb, .pDynamicState = &dyn,
        .layout = s.pipelineLayout, .renderPass = s.pass };
    VkResult result = p_vkCreateGraphicsPipelines(s.device, VK_NULL_HANDLE, 1, &ci, nullptr, &s.pipeline);
    p_vkDestroyShaderModule(s.device, frag, nullptr);
    p_vkDestroyShaderModule(s.device, vert, nullptr);
    return result == VK_SUCCESS;
}

static bool startDrawing(void) {
    if (s.drawing)
        return true;
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = s.image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    p_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &b);
    VkRenderPassBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = s.pass, .framebuffer = s.framebuffer,
        .renderArea = { { 0, 0 }, { s.width, s.height } } };
    p_vkCmdBeginRenderPass(s.cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    p_vkCmdBindPipeline(s.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipeline);
    VkViewport viewport = { .x = 0, .y = 0, .width = (float) s.width, .height = (float) s.height,
        .minDepth = 0, .maxDepth = 1 };
    p_vkCmdSetViewport(s.cmd, 0, 1, &viewport);
    s.drawing = true;
    return true;
}

static void stopDrawing(void) {
    if (!s.drawing)
        return;
    p_vkCmdEndRenderPass(s.cmd);
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = s.image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    p_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &b);
    s.drawing = false;
}

static bool resize(uint32_t width, uint32_t height) {
    if (s.device == VK_NULL_HANDLE || s.recording || !waitPending() || width == 0 || height == 0 ||
        (uint64_t) width * height > SIZE_MAX / 4u)
        return false;
    if (VkDevice_canSafelyPresent(s.owner)) {
        if (s.chain) {
            if (!VkSwapchain_resize(s.chain, width, height))
                return false;
        } else {
            s.chain = VkSwapchain_new(s.owner, width, height);
            if (!s.chain)
                return false;
        }
        s.dirty = false; // an explicit resize discards an unpresented old frame
    }
    if (s.width == width && s.height == height)
        return true;
    releaseTarget();
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
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
    VkImageView view = VK_NULL_HANDLE;
    VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = s.image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (p_vkCreateImageView(s.device, &vi, nullptr, &view) != VK_SUCCESS)
        goto fail;
    VkFramebufferCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = s.pass, .attachmentCount = 1, .pAttachments = &view,
        .width = width, .height = height, .layers = 1 };
    VkResult fr = p_vkCreateFramebuffer(s.device, &fi, nullptr, &s.framebuffer);
    s.view = view;
    if (fr != VK_SUCCESS)
        goto fail;
    if (!buildPipeline())
        goto fail;
    return true;
fail:
    releaseTarget();
    return false;
}

static bool begin(void) {
    if (s.image == VK_NULL_HANDLE || s.recording || (s.chain && s.dirty) || !waitPending() ||
        p_vkResetCommandBuffer(s.cmd, 0) != VK_SUCCESS)
        return false;
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    if (p_vkBeginCommandBuffer(s.cmd, &bi) != VK_SUCCESS)
        return false;
    s.recording = true;
    s.cleared = false;
    s.clipEnabled = false;
    s.drawing = false;
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
    stopDrawing();
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
    stopDrawing();
    if (s.cleared) {
        VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s.image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        p_vkCmdPipelineBarrier(s.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (!s.chain) {
            VkBufferImageCopy region = { .bufferOffset = 0,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageExtent = { s.width, s.height, 1 } };
            p_vkCmdCopyImageToBuffer(s.cmd, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     s.buffer, 1, &region);
        }
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
    s.dirty = s.cleared;
    return true;
}

bool VkGraphics_borrowPresentImage(const Device *device, VkImage *image, VkExtent2D *extent) {
    if (image) *image = VK_NULL_HANDLE;
    if (extent) *extent = (VkExtent2D) { 0, 0 };
    if (!device || !image || !extent || device != s.owner || s.recording ||
        !s.dirty || !waitPending() || !s.completed ||
        s.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        return false;
    *image = s.image;
    *extent = (VkExtent2D) { s.width, s.height };
    return true;
}

bool VkGraphics_readback(size_t capacity, uint8_t *dest) {
    if (dest == nullptr || s.chain || s.image == VK_NULL_HANDLE || s.recording ||
        capacity < s.bytes || !waitPending() || !s.completed)
        return false;
    void *mapped = nullptr;
    if (p_vkMapMemory(s.device, s.bufferMemory, 0, s.bytes, 0, &mapped) != VK_SUCCESS)
        return false;
    memcpy(dest, mapped, s.bytes);
    p_vkUnmapMemory(s.device, s.bufferMemory);
    return true;
}

static bool present(void) {
    VkImage image = VK_NULL_HANDLE;
    VkExtent2D extent = { 0, 0 };
    if (!s.chain || !VkGraphics_borrowPresentImage(s.owner, &image, &extent) ||
        !VkSwapchain_present(s.chain, image, extent))
        return false;
    s.dirty = false;
    return true;
}
static bool clip(const Rectangle *rect) {
    if (!s.recording && rect != nullptr)
        return false;
    s.clipEnabled = rect != nullptr;
    if (rect != nullptr)
        s.clipRect = *rect;
    return true;
}
static bool fillRect(const Rectangle *rect, const Brush *brush) {
    if (!s.recording || rect == nullptr || brush == nullptr || !s.cleared)
        return false;
    float x0 = floorf((*rect).x + 0.5f), y0 = floorf((*rect).y + 0.5f);
    float x1 = floorf((*rect).x + (*rect).width + 0.5f);
    float y1 = floorf((*rect).y + (*rect).height + 0.5f);
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1) ||
        (*rect).width <= 0 || (*rect).height <= 0)
        return true;
    float left = 0, top = 0, right = (float) s.width, bottom = (float) s.height;
    if (s.clipEnabled) {
        left = fmaxf(left, ceilf(s.clipRect.x));
        top = fmaxf(top, ceilf(s.clipRect.y));
        right = fminf(right, ceilf(s.clipRect.x + s.clipRect.width));
        bottom = fminf(bottom, ceilf(s.clipRect.y + s.clipRect.height));
    }
    x0 = fmaxf(left, x0); y0 = fmaxf(top, y0);
    x1 = fminf(right, x1); y1 = fminf(bottom, y1);
    if (x0 >= x1 || y0 >= y1)
        return true;
    if (!startDrawing())
        return false;
    VkRect2D scissor = { .offset = { (int32_t) x0, (int32_t) y0 },
        .extent = { (uint32_t) (x1 - x0), (uint32_t) (y1 - y0) } };
    p_vkCmdSetScissor(s.cmd, 0, 1, &scissor);
    uint32_t color = Brush_getColor(brush);
    float opacity = fmaxf(0.0f, fminf(1.0f, Brush_getOpacity(brush)));
    float push[8] = { 2.0f * x0 / (float) s.width - 1.0f,
        2.0f * y0 / (float) s.height - 1.0f,
        2.0f * (x1 - x0) / (float) s.width,
        2.0f * (y1 - y0) / (float) s.height,
        (float) ((color >> 24) & 255u) / 255.0f,
        (float) ((color >> 16) & 255u) / 255.0f,
        (float) ((color >> 8) & 255u) / 255.0f,
        (float) (color & 255u) / 255.0f * opacity };
    p_vkCmdPushConstants(s.cmd, s.pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
    p_vkCmdDraw(s.cmd, 6, 1, 0, 0);
    return true;
}
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
