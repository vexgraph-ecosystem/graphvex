#include "vulkan/vulkan_backend.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "image.h"
#include "quad_spv.h"
#include "vulkan/device.h"

// graphvex R3 — vulkan/vk_renderer.c
//
// THE GPU RENDERER. The display list's quads are batched (vk_batch) and drawn on
// the GPU in ONE forward pass into an offscreen target, then copied back for
// capture. No swapchain; the target is ours. Presentation via the borrowed
// CAMetalLayer is the next slice — the pixels are already GPU-produced here.

#define VK_ERR(call) do { VkResult _r = (call); if (_r != VK_SUCCESS) { snprintf(s_err, sizeof s_err, #call " -> VkResult %d", (int)_r); return false; } } while (0)

static Device *s_dev = NULL;
static VkInstance s_inst = VK_NULL_HANDLE;
static VkPhysicalDevice s_phys = VK_NULL_HANDLE;
static VkDevice s_device = VK_NULL_HANDLE;
static VkQueue s_queue = VK_NULL_HANDLE;
static uint32_t s_qfam = 0;

static VkImage s_img = VK_NULL_HANDLE;
static VkDeviceMemory s_imgMem = VK_NULL_HANDLE;
static VkImageView s_imgView = VK_NULL_HANDLE;
static VkFramebuffer s_fb = VK_NULL_HANDLE;
static VkRenderPass s_rp = VK_NULL_HANDLE;
static VkExtent2D s_ext = {0, 0};

static VkDescriptorSetLayout s_dsl = VK_NULL_HANDLE;
static VkPipelineLayout s_pl = VK_NULL_HANDLE;
static VkPipeline s_pipe = VK_NULL_HANDLE;
static VkDescriptorPool s_dpool = VK_NULL_HANDLE;
static VkDescriptorSet s_ds = VK_NULL_HANDLE;
static VkImage s_tex = VK_NULL_HANDLE;
static VkDeviceMemory s_texMem = VK_NULL_HANDLE;
static VkImageView s_texView = VK_NULL_HANDLE;
static VkSampler s_sampler = VK_NULL_HANDLE;

static VkBuffer s_vbo = VK_NULL_HANDLE;
static VkDeviceMemory s_vboMem = VK_NULL_HANDLE;
static VkDeviceSize s_vboCap = 0;
static VkBuffer s_rbo = VK_NULL_HANDLE;
static VkDeviceMemory s_rboMem = VK_NULL_HANDLE;
static VkDeviceSize s_rboCap = 0;

static VkCommandPool s_pool = VK_NULL_HANDLE;
static VkCommandBuffer s_cmd = VK_NULL_HANDLE;
static VkFence s_fence = VK_NULL_HANDLE;

static VkBatch *s_batch = NULL;
static Rect s_clip = {0, 0, 0, 0};
static int s_w = 0, s_h = 0;
static Color s_clear = COLOR_BLACK;
static bool s_rendered = false;
static char s_err[256] = "ok";

const char *VulkanBackend_lastError(void) { return s_err; }
const VkBatch *VulkanBackend_batch(void) { return s_batch; }

// ── helpers ─────────────────────────────────────────────────────────────────
static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(s_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static bool make_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkBuffer *buf, VkDeviceMemory *mem) {
    VkBufferCreateInfo bi = {0}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_ERR(vkCreateBuffer(s_device, &bi, NULL, buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(s_device, *buf, &mr);
    VkMemoryAllocateInfo ai = {0}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mem_type(mr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    VK_ERR(vkAllocateMemory(s_device, &ai, NULL, mem));
    VK_ERR(vkBindBufferMemory(s_device, *buf, *mem, 0));
    return true;
}

// ── render target ───────────────────────────────────────────────────────────
static void destroy_target(void) {
    if (s_fb) { vkDestroyFramebuffer(s_device, s_fb, NULL); s_fb = VK_NULL_HANDLE; }
    if (s_imgView) { vkDestroyImageView(s_device, s_imgView, NULL); s_imgView = VK_NULL_HANDLE; }
    if (s_img) { vkDestroyImage(s_device, s_img, NULL); s_img = VK_NULL_HANDLE; }
    if (s_imgMem) { vkFreeMemory(s_device, s_imgMem, NULL); s_imgMem = VK_NULL_HANDLE; }
}

static bool create_target(uint32_t w, uint32_t h) {
    destroy_target();
    VkImageCreateInfo ii = {0}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = (VkExtent3D){w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_ERR(vkCreateImage(s_device, &ii, NULL, &s_img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(s_device, s_img, &mr);
    VkMemoryAllocateInfo ai = {0}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, 0);
    VK_ERR(vkAllocateMemory(s_device, &ai, NULL, &s_imgMem));
    VK_ERR(vkBindImageMemory(s_device, s_img, s_imgMem, 0));

    VkImageViewCreateInfo vi = {0}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = s_img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_ERR(vkCreateImageView(s_device, &vi, NULL, &s_imgView));

    VkFramebufferCreateInfo fi = {0}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = s_rp;
    fi.attachmentCount = 1;
    fi.pAttachments = &s_imgView;
    fi.width = w;
    fi.height = h;
    fi.layers = 1;
    VK_ERR(vkCreateFramebuffer(s_device, &fi, NULL, &s_fb));
    s_ext = (VkExtent2D){w, h};
    s_rendered = false;

    // readback buffer (host-visible)
    VkDeviceSize need = (VkDeviceSize)w * h * 4u;
    if (need > s_rboCap) {
        if (s_rbo) { vkDestroyBuffer(s_device, s_rbo, NULL); s_rbo = VK_NULL_HANDLE; }
        if (s_rboMem) { vkFreeMemory(s_device, s_rboMem, NULL); s_rboMem = VK_NULL_HANDLE; }
        if (!make_buffer(need, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &s_rbo, &s_rboMem)) return false;
        s_rboCap = need;
    }
    return true;
}

// ── pipeline ────────────────────────────────────────────────────────────────
static bool create_render_pass(void) {
    VkAttachmentDescription color = {0};
    color.format = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp = {0}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    VK_ERR(vkCreateRenderPass(s_device, &rp, NULL, &s_rp));
    return true;
}

static bool create_texture(void) {
    VkImageCreateInfo ii = {0}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = (VkExtent3D){1, 1, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_ERR(vkCreateImage(s_device, &ii, NULL, &s_tex));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(s_device, s_tex, &mr);
    VkMemoryAllocateInfo ai = {0}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, 0);
    VK_ERR(vkAllocateMemory(s_device, &ai, NULL, &s_texMem));
    VK_ERR(vkBindImageMemory(s_device, s_tex, s_texMem, 0));
    VkImageViewCreateInfo vi = {0}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = s_tex;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_ERR(vkCreateImageView(s_device, &vi, NULL, &s_texView));
    VkSamplerCreateInfo si = {0}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_ERR(vkCreateSampler(s_device, &si, NULL, &s_sampler));

    VkDescriptorSetLayoutBinding b = {0};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl = {0}; dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 1;
    dl.pBindings = &b;
    VK_ERR(vkCreateDescriptorSetLayout(s_device, &dl, NULL, &s_dsl));

    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dp = {0}; dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &ps;
    VK_ERR(vkCreateDescriptorPool(s_device, &dp, NULL, &s_dpool));
    VkDescriptorSetAllocateInfo da = {0}; da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = s_dpool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &s_dsl;
    VK_ERR(vkAllocateDescriptorSets(s_device, &da, &s_ds));
    VkDescriptorImageInfo dii = {s_sampler, s_texView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w = {0}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = s_ds;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(s_device, 1, &w, 0, NULL);
    return true;
}

static bool create_pipeline(void) {
    VkShaderModuleCreateInfo vci = {0}; vci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vci.codeSize = quad_vert_spv_len;
    vci.pCode = (const uint32_t *)quad_vert_spv;
    VkShaderModule vs;
    VK_ERR(vkCreateShaderModule(s_device, &vci, NULL, &vs));
    VkShaderModuleCreateInfo fci = {0}; fci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fci.codeSize = quad_frag_spv_len;
    fci.pCode = (const uint32_t *)quad_frag_spv;
    VkShaderModule fs;
    VK_ERR(vkCreateShaderModule(s_device, &fci, NULL, &fs));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL},
    };
    VkVertexInputBindingDescription bind = {0, sizeof(VkVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[7] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VkVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VkVertex, u)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex, r)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex, br)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex, radius)},
        {5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VkVertex, qw)},
        {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VkVertex, c0)},
    };
    VkPipelineVertexInputStateCreateInfo vi = {0}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 7;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia = {0}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {0}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {0}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {0}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {0}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {0}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo pl = {0}; pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &s_dsl;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    VK_ERR(vkCreatePipelineLayout(s_device, &pl, NULL, &s_pl));

    VkGraphicsPipelineCreateInfo gp = {0}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = s_pl;
    gp.renderPass = s_rp;
    gp.subpass = 0;
    VkResult r = vkCreateGraphicsPipelines(s_device, VK_NULL_HANDLE, 1, &gp, NULL, &s_pipe);
    vkDestroyShaderModule(s_device, vs, NULL);
    vkDestroyShaderModule(s_device, fs, NULL);
    if (r != VK_SUCCESS) { snprintf(s_err, sizeof s_err, "vkCreateGraphicsPipelines -> %d", (int)r); return false; }
    return true;
}

static bool init_vulkan(void) {
    if (s_device != VK_NULL_HANDLE) return true;
    s_dev = Device_create(false);
    if (!Device_isValid(s_dev)) {
        snprintf(s_err, sizeof s_err, "Device_create failed: %s", Device_lastError(s_dev));
        return false;
    }
    s_inst = (VkInstance)Device_instance(s_dev);
    s_phys = (VkPhysicalDevice)Device_physical(s_dev);
    s_device = (VkDevice)Device_native(s_dev);
    s_queue = (VkQueue)Device_queue(s_dev);
    s_qfam = Device_queueFamily(s_dev);
    if (!create_render_pass()) return false;
    if (!create_texture()) return false;
    if (!create_pipeline()) return false;
    VkCommandPoolCreateInfo cp = {0}; cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = s_qfam;
    VK_ERR(vkCreateCommandPool(s_device, &cp, NULL, &s_pool));
    VkCommandBufferAllocateInfo ca = {0}; ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ca.commandPool = s_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VK_ERR(vkAllocateCommandBuffers(s_device, &ca, &s_cmd));
    VkFenceCreateInfo fc = {0}; fc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_ERR(vkCreateFence(s_device, &fc, NULL, &s_fence));
    return true;
}

// ── the batch render pass ───────────────────────────────────────────────────
static float cf(Color c) { return (float)((c >> 24) & 0xFFu) / 255.0f; }

static bool render(void) {
    if (!s_device) return false;
    if (s_w <= 0 || s_h <= 0) return false;
    if (!s_fb || s_ext.width != (uint32_t)s_w || s_ext.height != (uint32_t)s_h) {
        if (!create_target((uint32_t)s_w, (uint32_t)s_h)) return false;
    }
    size_t vcount = VkBatch_vertices(s_batch, NULL, 0);
    if (vcount == 0) vcount = 6;   // need at least something; draw 0 anyway
    VkDeviceSize vbytes = (VkDeviceSize)vcount * sizeof(VkVertex);
    if (vbytes > s_vboCap) {
        if (s_vbo) { vkDestroyBuffer(s_device, s_vbo, NULL); s_vbo = VK_NULL_HANDLE; }
        if (s_vboMem) { vkFreeMemory(s_device, s_vboMem, NULL); s_vboMem = VK_NULL_HANDLE; }
        if (!make_buffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s_vbo, &s_vboMem)) return false;
        s_vboCap = vbytes;
    }
    size_t written = 0;
    if (vcount > 0) {
        void *map = NULL;
        VK_ERR(vkMapMemory(s_device, s_vboMem, 0, vbytes, 0, &map));
        written = VkBatch_vertices(s_batch, (VkVertex *)map, vcount);
        vkUnmapMemory(s_device, s_vboMem);
    }

    vkResetFences(s_device, 1, &s_fence);
    VK_ERR(vkResetCommandBuffer(s_cmd, 0));
    VkCommandBufferBeginInfo bi = {0}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_ERR(vkBeginCommandBuffer(s_cmd, &bi));

    VkClearValue clear;
    memset(&clear, 0, sizeof clear);
    clear.color.float32[0] = cf(s_clear);
    clear.color.float32[1] = cf(s_clear << 8);
    clear.color.float32[2] = cf(s_clear << 16);
    clear.color.float32[3] = (float)(s_clear & 0xFFu) / 255.0f;
    VkRenderPassBeginInfo rp = {0}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = s_rp;
    rp.framebuffer = s_fb;
    rp.renderArea = (VkRect2D){{0, 0}, {(uint32_t)s_w, (uint32_t)s_h}};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(s_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0, 0, (float)s_w, (float)s_h, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)s_w, (uint32_t)s_h}};
    vkCmdSetViewport(s_cmd, 0, 1, &viewport);
    vkCmdSetScissor(s_cmd, 0, 1, &scissor);
    if (written > 0) {
        vkCmdBindPipeline(s_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_pipe);
        vkCmdBindDescriptorSets(s_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_pl, 0, 1, &s_ds, 0, NULL);
        float push[2] = {(float)s_w, (float)s_h};
        vkCmdPushConstants(s_cmd, s_pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof push, push);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(s_cmd, 0, 1, &s_vbo, &off);
        vkCmdDraw(s_cmd, (uint32_t)written, 1, 0, 0);
    }
    vkCmdEndRenderPass(s_cmd);
    VkBufferImageCopy region = {0};
    region.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = (VkExtent3D){(uint32_t)s_w, (uint32_t)s_h, 1};
    vkCmdCopyImageToBuffer(s_cmd, s_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s_rbo, 1, &region);
    VK_ERR(vkEndCommandBuffer(s_cmd));
    VkSubmitInfo si = {0}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cmd;
    VK_ERR(vkQueueSubmit(s_queue, 1, &si, s_fence));
    VK_ERR(vkWaitForFences(s_device, 1, &s_fence, VK_TRUE, 1000000000ull));
    s_rendered = true;
    return true;
}

// ── Backend row ─────────────────────────────────────────────────────────────
static bool vk_begin(void) {
    if (!init_vulkan()) return false;
    if (!s_batch) s_batch = VkBatch_0();
    if (!s_batch) return false;
    VkBatch_clear(s_batch);
    VkBatch_setClip(s_batch, s_clip);   // the window clip (or an explicit one)
    s_rendered = false;
    return true;
}
static bool vk_end(void) { return s_batch != NULL; }
static bool vk_present(void) { return render(); }

static bool vk_resize(uint32_t w, uint32_t h) {
    s_w = (int)w;
    s_h = (int)h;
    s_clip = (Rect){0, 0, (float)w, (float)h};
    return true;
}
static bool vk_clear(Color color) { s_clear = color; return true; }
static bool vk_clip(const Rect *rect) {
    s_clip = rect ? *rect : (Rect){0, 0, (float)s_w, (float)s_h};
    if (s_batch) VkBatch_setClip(s_batch, s_clip);
    return true;
}
static bool vk_fillRect(const Rect *rect, const Brush *brush) {
    if (!s_batch || !rect || !brush) return false;
    // NEVER intersect the geometry: that would resize the box and recompute the
    // corner. The quad keeps its size; the clip cuts fragments (in the shader).
    VkBatch_rect(s_batch, *rect, brush);
    return true;
}
static bool vk_drawImage(const Image *image, const Rect *dst) {
    if (!s_batch || !image || !dst) return false;
    Rect src = {0, 0, (float)Image_width(image), (float)Image_height(image)};
    VkBatch_image(s_batch, image, src, *dst);
    return true;
}
static bool vk_drawText(const Rect *rect, const char *text, const Brush *brush) {
    if (!s_batch || !rect) return false;
    VkBatch_glyph(s_batch, *rect, 0u, brush ? brush->color : COLOR_WHITE);
    (void)text;
    return true;
}

static bool vk_capture(Image *dest) {
    if (!dest) return false;
    if (!s_rendered && !render()) return false;
    if (!Image_ensureShadow(dest, (uint32_t)s_w, (uint32_t)s_h)) return false;
    void *map = NULL;
    VkDeviceSize size = (VkDeviceSize)s_w * s_h * 4u;
    VK_ERR(vkMapMemory(s_device, s_rboMem, 0, size, 0, &map));
    uint8_t *out = Image_pixels(dest);
    size_t stride = Image_stride(dest);
    for (int y = 0; y < s_h; y++)
        memcpy(out + (size_t)y * stride, (uint8_t *)map + (size_t)y * (size_t)s_w * 4u, (size_t)s_w * 4u);
    vkUnmapMemory(s_device, s_rboMem);
    return true;
}

const Backend *VulkanBackend_row(void) {
    static const Backend row = {
        BACKEND_VULKAN, vk_begin,    vk_end,      vk_present,
        vk_resize,      vk_clear,    vk_clip,     vk_fillRect,
        vk_drawImage,   vk_drawText, vk_capture,
    };
    return &row;
}

bool VulkanBackend_bind(void *nativeLayer, uint32_t widthPx, uint32_t heightPx) {
    (void)nativeLayer;
    if (!init_vulkan()) return false;
    return vk_resize(widthPx, heightPx);
}

void VulkanBackend_unbind(void) {
    if (s_device) {
        vkDeviceWaitIdle(s_device);
        destroy_target();
        if (s_vbo) vkDestroyBuffer(s_device, s_vbo, NULL);
        if (s_vboMem) vkFreeMemory(s_device, s_vboMem, NULL);
        if (s_rbo) vkDestroyBuffer(s_device, s_rbo, NULL);
        if (s_rboMem) vkFreeMemory(s_device, s_rboMem, NULL);
        if (s_pipe) vkDestroyPipeline(s_device, s_pipe, NULL);
        if (s_pl) vkDestroyPipelineLayout(s_device, s_pl, NULL);
        if (s_dpool) vkDestroyDescriptorPool(s_device, s_dpool, NULL);
        if (s_dsl) vkDestroyDescriptorSetLayout(s_device, s_dsl, NULL);
        if (s_rp) vkDestroyRenderPass(s_device, s_rp, NULL);
    }
    VkBatch_free(s_batch);
    s_batch = NULL;
    Device_destroy(s_dev);
    s_dev = NULL;
    s_device = VK_NULL_HANDLE;
    s_vbo = s_rbo = VK_NULL_HANDLE;
    s_vboMem = s_rboMem = VK_NULL_HANDLE;
    s_pipe = VK_NULL_HANDLE;
    s_pl = VK_NULL_HANDLE;
    s_dpool = VK_NULL_HANDLE;
    s_dsl = VK_NULL_HANDLE;
    s_rp = VK_NULL_HANDLE;
    s_vboCap = s_rboCap = 0;
}
