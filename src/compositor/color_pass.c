#include "compositor/color_pass.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "exception/throw.h"

;;DEFINITION
/* ColorPass owns a real Vulkan fullscreen texture-filter pipeline, never CPU
 * pixel processing. The caller selects isolated foreground/backdrop/element
 * input, supplies barriers and a destination render pass, and retires GPU uses
 * before destroying this borrowed-device pipeline. Pipeline creation is cold;
 * recording is allocation/wait-free. No scene traversal or window ownership. */
;;OVERVIEW
/* CLASS: ColorPass. STRUCT FIELDS in order: device (borrowed VkDevice),
 * descriptorLayout (owned sampled-texture layout), layout (owned push layout),
 * pipeline (owned graphics pipeline). Public: _0/_6/chooser/zero, destroy,
 * getDescriptorLayout, validateToken, record, toString/toStringStruct.
 * Private: tokenValid (cold scalar schema), format (bounded projections).
 * Reject policy: invalid cold creation/record returns null/false and THROW once.
 * Native handle validity, source layout/extent and completion are caller-owned;
 * no submitted work may outlive pipeline/device/descriptor/source/target. */
struct ColorPass {
    VkDevice device;
    VkDescriptorSetLayout descriptorLayout;
    VkPipelineLayout layout;
    VkPipeline pipeline;
};

// Checks supported color-filter IDs and their finite, operation-specific payload ranges.
static bool tokenValid(FilterToken token) {
    uint16_t id = Filter_id(token);
    uint64_t payload = Filter_payload(token);
    if (id == GRAYSCALE_ID || id == GRAYSCALE_RED_ID || id == GRAYSCALE_GREEN_ID ||
        id == GRAYSCALE_BLUE_ID || id == INVERT_ID)
        return payload == 0;
    if (id != BRIGHTNESS_ID && id != CONTRAST_ID && id != BLACK_AND_WHITE_ID)
        return false;
    if (payload >> 32)
        return false;
    uint32_t bits = (uint32_t) payload;
    float amount;
    memcpy(&amount, &bits, sizeof amount);
    if (!isfinite(amount))
        return false;
    if (id == BRIGHTNESS_ID)
        return amount >= -1 && amount <= 1;
    if (id == BLACK_AND_WHITE_ID)
        return amount >= 0 && amount <= 1;
    return amount >= 0;
}

// Validates a token and emits a cold diagnostic when its operation or payload is unsupported.
bool ColorPass_validateToken(FilterToken token) {
    if (tokenValid(token))
        return true;
    THROW("ColorPass rejected filter token");
    return false;
}
// Returns the null identity because a Vulkan color pass requires device and shader inputs.
ColorPass *ColorPass_0(void) { return nullptr; }
// Returns the null identity through the zero-argument constructor.
ColorPass *ColorPass_zero(void) { return ColorPass_0(); }

// Destroys pipeline objects created by this pass; caller must retire submitted work first.
void ColorPass_destroy(ColorPass *self) {
    if (!self)
        return;
    if ((*self).pipeline)
        vkDestroyPipeline((*self).device, (*self).pipeline, nullptr);
    if ((*self).layout)
        vkDestroyPipelineLayout((*self).device, (*self).layout, nullptr);
    if ((*self).descriptorLayout)
        vkDestroyDescriptorSetLayout((*self).device, (*self).descriptorLayout, nullptr);
    free(self);
}

// Builds a Vulkan fullscreen filter pipeline from validated SPIR-V words and render-pass handle.
ColorPass *ColorPass_6(Device *device, void *nativeRenderPass,
                       const uint32_t *vertexWords, size_t vertexBytes,
                       const uint32_t *fragmentWords, size_t fragmentBytes) {
    if (!Device_isValid(device) || !nativeRenderPass || !vertexWords || !fragmentWords ||
        vertexBytes < 20 || fragmentBytes < 20 || vertexBytes % 4 || fragmentBytes % 4 ||
        (uintptr_t) vertexWords % 4 || (uintptr_t) fragmentWords % 4 ||
        vertexWords[0] != UINT32_C(0x07230203) || fragmentWords[0] != UINT32_C(0x07230203)) {
        THROW("ColorPass rejected pipeline inputs");
        return nullptr;
    }
    ColorPass *self = calloc(1, sizeof *self);
    if (!self) {
        THROW("ColorPass allocation failed");
        return nullptr;
    }
    (*self).device = (VkDevice) Device_native(device);
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo sm = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vertexBytes, .pCode = vertexWords};
    VkResult result = vkCreateShaderModule((*self).device, &sm, nullptr, &vertex);
    if (result != VK_SUCCESS)
        goto failed;
    sm.codeSize = fragmentBytes; sm.pCode = fragmentWords;
    result = vkCreateShaderModule((*self).device, &sm, nullptr, &fragment);
    if (result != VK_SUCCESS)
        goto failed;
    VkDescriptorSetLayoutBinding binding = {.binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    result = vkCreateDescriptorSetLayout((*self).device, &dl, nullptr, &(*self).descriptorLayout);
    if (result != VK_SUCCESS)
        goto failed;
    VkPushConstantRange push = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .size = 8};
    VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &(*self).descriptorLayout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
    result = vkCreatePipelineLayout((*self).device, &pl, nullptr, &(*self).layout);
    if (result != VK_SUCCESS)
        goto failed;
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment};
    VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic};
    VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vi,
        .pInputAssemblyState = &ia, .pViewportState = &vp, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pColorBlendState = &cb, .pDynamicState = &dy,
        .layout = (*self).layout, .renderPass = (VkRenderPass) nativeRenderPass};
    result = vkCreateGraphicsPipelines((*self).device, VK_NULL_HANDLE, 1, &gp, nullptr, &(*self).pipeline);
    if (result != VK_SUCCESS)
        goto failed;
    vkDestroyShaderModule((*self).device, fragment, nullptr);
    vkDestroyShaderModule((*self).device, vertex, nullptr);
    return self;
failed:
    if (fragment)
        vkDestroyShaderModule((*self).device, fragment, nullptr);
    if (vertex)
        vkDestroyShaderModule((*self).device, vertex, nullptr);
    ColorPass_destroy(self);
    THROW("ColorPass Vulkan creation failed: %d", (int) result);
    return nullptr;
}

// Returns the owned descriptor-set layout as an opaque handle for descriptor allocation.
void *ColorPass_getDescriptorLayout(const ColorPass *self) {
    return self ? (void*) (*self).descriptorLayout : nullptr;
}
// Records viewport, bindings, filter constants, and a fullscreen triangle into the command buffer.
bool ColorPass_record(const ColorPass *self, void *nativeCommandBuffer,
                      void *nativeDescriptorSet, uint32_t width, uint32_t height,
                      FilterToken token) {
    if (!self || !nativeCommandBuffer || !nativeDescriptorSet || !width || !height || !tokenValid(token)) {
        THROW("ColorPass rejected recording inputs");
        return false;
    }
    VkCommandBuffer cmd = (VkCommandBuffer) nativeCommandBuffer;
    VkDescriptorSet ds = (VkDescriptorSet) nativeDescriptorSet;
    uint32_t push[] = {Filter_id(token), (uint32_t) Filter_payload(token)};
    VkViewport viewport = {0, 0, (float) width, (float) height, 0, 1};
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, (*self).pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, (*self).layout, 0, 1, &ds, 0, nullptr);
    vkCmdPushConstants(cmd, (*self).layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    return true;
}
// Writes the bounded concise or field-level ColorPass projection.
static void format(const ColorPass *self, bool structure, char *dest, size_t cap, bool *outTruncated) {
    if (!dest || !cap) {
        if (outTruncated)
            *outTruncated = true;
        return;
    }
    int count;
    if (!self)
        count = snprintf(dest, cap, "nullptr");
    else if (structure)
        count = snprintf(dest, cap, "ColorPass{device=%p,descriptorLayout=%p,layout=%p,pipeline=%p}",
            (void*) (*self).device, (void*) (*self).descriptorLayout, (void*) (*self).layout, (void*) (*self).pipeline);
    else
        count = snprintf(dest, cap, "ColorPass(Vulkan texture color)");
    if (outTruncated)
        *outTruncated = count < 0 || (size_t) count >= cap;
}
// Formats a bounded value summary of the color pipeline.
void ColorPass_toString(const ColorPass *self, char *dest, size_t cap, bool *outTruncated) {
    format(self, false, dest, cap, outTruncated);
}
// Formats the ColorPass fields into caller-provided bounded storage.
void ColorPass_toStringStruct(const ColorPass *self, char *dest, size_t cap, bool *outTruncated) {
    format(self, true, dest, cap, outTruncated);
}
