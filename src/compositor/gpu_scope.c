#include "compositor/gpu_scope.h"
#include "filter/filter_functions.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "exception/throw.h"
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

;;DEFINITION
/* Explicit Vulkan scoped groups: upload borrowed straight sRGB source images,
 * isolate groups on the GPU, splat each source texel's weighted premultiplied
 * color into expanded float attachments with additive blending, then compose
 * the resulting scope into an sRGB target. Only byte transport crosses the CPU;
 * no CPU filter/color/composition algorithm or fallback exists here. This cold
 * Image bridge is synchronous and bounded; a timeout retains its entire job
 * until the fence signals, so neither retry nor destruction frees live work. */
;;OVERVIEW
/* CLASS: GpuScope. STRUCT FIELDS: device (borrowed VkDevice), physical (borrowed
 * physical GPU), queue (borrowed graphics queue), maxPixels (configured work
 * budget), maxExtent (probed image/viewport bound), scatterRender (owned 2-float
 * attachment pass), groupRender (owned float pass), finalRender (owned sRGB
 * pass), scatterDescriptor (owned single-sampler layout), scopeDescriptor
 * (owned four-sampler layout), scatterLayout (owned 24-byte push layout),
 * scopeLayout (owned 36-byte push layout), scatterPipeline/groupPipeline/
 * finalPipeline (owned pipelines), sampler (owned nearest sampler), commands
 * (owned resettable pool), command (borrowed pool buffer), fence (owned),
 * pending (submitted job flag), job (owned dumb slot record).
 * SLOT RECORD Texture: image, memory, view (owned handles), width/height/format.
 * SLOT RECORD Buffer: buffer, memory, size (owned transfer allocation).
 * SLOT RECORD Job: textures[7] (3 inputs/group/accumulation/weight/output),
 * buffers[4] (3 uploads/readback), frames[3] (group/scatter/final), descriptors
 * (owned pool), sets[3] (borrowed pool sets). Counts are the fixed shader DAG,
 * not scene entity limits. Public: _0/_3/chooser/zero, render, pending query,
 * destroy and bounded projections. Private: Vulkan allocation, render-pass/
 * pipeline setup, descriptor/barrier/record and retirement helpers.
 * Cold rejection reports once, preserves outputs. destroy false on unsignaled
 * fence is silent to preserve bounded teardown; state remains owned for retry. */
enum { PRIOR, DECORATION, FOREGROUND, GROUP, ACCUMULATION, WEIGHT, OUTPUT, TEXTURE_COUNT };
enum { UPLOAD_PRIOR, UPLOAD_DECORATION, UPLOAD_FOREGROUND, READBACK, BUFFER_COUNT };
enum { GROUP_FRAME, SCATTER_FRAME, FINAL_FRAME, FRAME_COUNT };
static const uint64_t GPU_WAIT_NS = UINT64_C(100000000);
/* Security bound for externally supplied shader blobs, not an entity ceiling. */
static const long MAX_SHADER_BYTES = 16L*1024*1024;
typedef struct Texture {
    VkImage image; VkDeviceMemory memory; VkImageView view;
    uint32_t width, height; VkFormat format;
} Texture;
typedef struct Buffer { VkBuffer buffer; VkDeviceMemory memory; VkDeviceSize size; } Buffer;
typedef struct Job {
    Texture textures[TEXTURE_COUNT]; Buffer buffers[BUFFER_COUNT];
    VkFramebuffer frames[FRAME_COUNT]; VkDescriptorPool descriptors;
    VkDescriptorSet sets[FRAME_COUNT];
} Job;
struct GpuScope {
    VkDevice device; VkPhysicalDevice physical; VkQueue queue;
    uint32_t maxPixels, maxExtent;
    VkRenderPass scatterRender, groupRender, finalRender;
    VkDescriptorSetLayout scatterDescriptor, scopeDescriptor;
    VkPipelineLayout scatterLayout, scopeLayout;
    VkPipeline scatterPipeline, groupPipeline, finalPipeline;
    VkSampler sampler; VkCommandPool commands; VkCommandBuffer command;
    VkFence fence; bool pending; Job job;
};

static uint32_t memoryType(GpuScope *self,uint32_t bits,VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties((*self).physical,&properties);
    for (uint32_t i=0;i<properties.memoryTypeCount;++i) {
        VkMemoryType *type=&properties.memoryTypes[i];
        if ((bits&(1u<<i)) && ((*type).propertyFlags&want)==want)
            return i;
    }
    return UINT32_MAX;
}
static bool makeBuffer(GpuScope *self,VkDeviceSize size,VkBufferUsageFlags usage,Buffer *out) {
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    if (vkCreateBuffer((*self).device,&info,nullptr,&(*out).buffer)!=VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements((*self).device,(*out).buffer,&requirements);
    uint32_t type=memoryType(self,requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type==UINT32_MAX)
        return false;
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size,.memoryTypeIndex=type};
    (*out).size=size;
    return vkAllocateMemory((*self).device,&allocate,nullptr,&(*out).memory)==VK_SUCCESS &&
        vkBindBufferMemory((*self).device,(*out).buffer,(*out).memory,0)==VK_SUCCESS;
}
static bool makeTexture(GpuScope *self,uint32_t width,uint32_t height,VkFormat format,Texture *out) {
    (*out).width=width; (*out).height=height; (*out).format=format;
    VkImageCreateInfo info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={width,height,1},.mipLevels=1,
        .arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    if (vkCreateImage((*self).device,&info,nullptr,&(*out).image)!=VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements((*self).device,(*out).image,&requirements);
    uint32_t type=memoryType(self,requirements.memoryTypeBits,0);
    if (type==UINT32_MAX)
        return false;
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size,.memoryTypeIndex=type};
    if (vkAllocateMemory((*self).device,&allocate,nullptr,&(*out).memory)!=VK_SUCCESS ||
        vkBindImageMemory((*self).device,(*out).image,(*out).memory,0)!=VK_SUCCESS)
        return false;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=(*out).image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    return vkCreateImageView((*self).device,&view,nullptr,&(*out).view)==VK_SUCCESS;
}
static void releaseJob(GpuScope *self) {
    Job *job=&(*self).job;
    if ((*job).descriptors)
        vkDestroyDescriptorPool((*self).device,(*job).descriptors,nullptr);
    for (unsigned i=0;i<FRAME_COUNT;++i)
        if ((*job).frames[i])
            vkDestroyFramebuffer((*self).device,(*job).frames[i],nullptr);
    for (unsigned i=0;i<TEXTURE_COUNT;++i) {
        Texture *t=&(*job).textures[i];
        if ((*t).view)
            vkDestroyImageView((*self).device,(*t).view,nullptr);
        if ((*t).image)
            vkDestroyImage((*self).device,(*t).image,nullptr);
        if ((*t).memory)
            vkFreeMemory((*self).device,(*t).memory,nullptr);
    }
    for (unsigned i=0;i<BUFFER_COUNT;++i) {
        Buffer *b=&(*job).buffers[i];
        if ((*b).buffer)
            vkDestroyBuffer((*self).device,(*b).buffer,nullptr);
        if ((*b).memory)
            vkFreeMemory((*self).device,(*b).memory,nullptr);
    }
    memset(job,0,sizeof *job);
}
static bool retire(GpuScope *self) {
    if ((*self).pending) {
        if (vkWaitForFences((*self).device,1,&(*self).fence,VK_TRUE,GPU_WAIT_NS)!=VK_SUCCESS)
            return false;
        (*self).pending=false;
    }
    releaseJob(self);
    return true;
}
bool GpuScope_isPending(const GpuScope *self) { return self && (*self).pending; }
GpuScope *GpuScope_0(void) { return nullptr; }
GpuScope *GpuScope_zero(void) { return GpuScope_0(); }
bool GpuScope_destroy(GpuScope *self) {
    if (!self)
        return true;
    if (!retire(self))
        return false;
    VkDevice d=(*self).device;
    if ((*self).fence)
        vkDestroyFence(d,(*self).fence,nullptr);
    if ((*self).commands)
        vkDestroyCommandPool(d,(*self).commands,nullptr);
    if ((*self).sampler)
        vkDestroySampler(d,(*self).sampler,nullptr);
    if ((*self).scatterPipeline)
        vkDestroyPipeline(d,(*self).scatterPipeline,nullptr);
    if ((*self).groupPipeline)
        vkDestroyPipeline(d,(*self).groupPipeline,nullptr);
    if ((*self).finalPipeline)
        vkDestroyPipeline(d,(*self).finalPipeline,nullptr);
    if ((*self).scatterLayout)
        vkDestroyPipelineLayout(d,(*self).scatterLayout,nullptr);
    if ((*self).scopeLayout)
        vkDestroyPipelineLayout(d,(*self).scopeLayout,nullptr);
    if ((*self).scatterDescriptor)
        vkDestroyDescriptorSetLayout(d,(*self).scatterDescriptor,nullptr);
    if ((*self).scopeDescriptor)
        vkDestroyDescriptorSetLayout(d,(*self).scopeDescriptor,nullptr);
    if ((*self).scatterRender)
        vkDestroyRenderPass(d,(*self).scatterRender,nullptr);
    if ((*self).groupRender)
        vkDestroyRenderPass(d,(*self).groupRender,nullptr);
    if ((*self).finalRender)
        vkDestroyRenderPass(d,(*self).finalRender,nullptr);
    free(self); return true;
}
static bool renderPass(GpuScope *self,VkFormat format,unsigned count,VkRenderPass *out) {
    VkAttachmentDescription attachments[2]={0};
    VkAttachmentReference references[2]={0};
    for (unsigned i=0;i<count;++i) {
        attachments[i]=(VkAttachmentDescription){.format=format,.samples=VK_SAMPLE_COUNT_1_BIT,
            .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        references[i]=(VkAttachmentReference){i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=count,.pColorAttachments=references};
    VkSubpassDependency dependency={.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,
        .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
    VkRenderPassCreateInfo info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=count,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&subpass,
        .dependencyCount=1,.pDependencies=&dependency};
    return vkCreateRenderPass((*self).device,&info,nullptr,out)==VK_SUCCESS;
}
static VkShaderModule loadShader(GpuScope *self,const char *directory,const char *name) {
    size_t n=strlen(directory), m=strlen(name);
    if (n>SIZE_MAX-m-6)
        return VK_NULL_HANDLE;
    char *path=malloc(n+m+6);
    if (!path)
        return VK_NULL_HANDLE;
    snprintf(path,n+m+6,"%s/%s.spv",directory,name);
    FILE *file=fopen(path,"rb"); free(path);
    if (!file)
        return VK_NULL_HANDLE;
    if (fseek(file,0,SEEK_END)) { fclose(file); return VK_NULL_HANDLE; }
    long length=ftell(file);
    if (length<20 || length%4 || length>MAX_SHADER_BYTES) { fclose(file); return VK_NULL_HANDLE; }
    rewind(file);
    uint32_t *words=malloc((size_t) length);
    if (!words) { fclose(file); return VK_NULL_HANDLE; }
    bool valid=fread(words,1,(size_t) length,file)==(size_t) length && words[0]==UINT32_C(0x07230203);
    fclose(file);
    VkShaderModule result=VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=(size_t) length,.pCode=words};
    if (valid && vkCreateShaderModule((*self).device,&info,nullptr,&result)!=VK_SUCCESS)
        result=VK_NULL_HANDLE;
    free(words); return result;
}
static bool pipeline(GpuScope *self,VkShaderModule vertex,VkShaderModule fragment,
    VkRenderPass render,VkPipelineLayout layout,unsigned count,bool additive,VkPipeline *out) {
    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend[2]={0};
    for (unsigned i=0;i<count;++i)
        blend[i]=(VkPipelineColorBlendAttachmentState){.blendEnable=additive,
            .srcColorBlendFactor=VK_BLEND_FACTOR_ONE,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE,
            .colorBlendOp=VK_BLEND_OP_ADD,.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.alphaBlendOp=VK_BLEND_OP_ADD,.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=count,.pAttachments=blend};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,
        .pColorBlendState=&cb,.pDynamicState=&dynamic,.layout=layout,.renderPass=render};
    return vkCreateGraphicsPipelines((*self).device,VK_NULL_HANDLE,1,&info,nullptr,out)==VK_SUCCESS;
}
GpuScope *GpuScope_3(Device *device,const char *directory,uint32_t maxPixels) {
    if (!Device_isValid(device) || !directory || !*directory || !maxPixels || maxPixels>INT32_MAX) {
        THROW("GpuScope rejected construction inputs"); return nullptr;
    }
    GpuScope *self=calloc(1,sizeof *self);
    if (!self) { THROW("GpuScope allocation failed"); return nullptr; }
    (*self).device=(VkDevice) Device_native(device); (*self).physical=(VkPhysicalDevice) Device_physical(device);
    (*self).queue=(VkQueue) Device_queue(device); (*self).maxPixels=maxPixels;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties((*self).physical,&properties);
    VkPhysicalDeviceLimits *limits=&properties.limits;
    (*self).maxExtent=(*limits).maxImageDimension2D;
    if ((*limits).maxFramebufferWidth<(*self).maxExtent)
        (*self).maxExtent=(*limits).maxFramebufferWidth;
    if ((*limits).maxFramebufferHeight<(*self).maxExtent)
        (*self).maxExtent=(*limits).maxFramebufferHeight;
    if ((*limits).maxViewportDimensions[0]<(*self).maxExtent)
        (*self).maxExtent=(*limits).maxViewportDimensions[0];
    if ((*limits).maxViewportDimensions[1]<(*self).maxExtent)
        (*self).maxExtent=(*limits).maxViewportDimensions[1];
    if ((*self).maxExtent>(uint32_t) INT32_MAX-32)
        (*self).maxExtent=(uint32_t) INT32_MAX-32;
    for (unsigned i=0;i<2;++i) {
        VkFormat format=i ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R32G32B32A32_SFLOAT;
        VkFormatProperties support;
        vkGetPhysicalDeviceFormatProperties((*self).physical,format,&support);
        VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if (!i)
            required|=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
        if ((support.optimalTilingFeatures&required)!=required)
            goto failed;
    }
    if (!renderPass(self,VK_FORMAT_R32G32B32A32_SFLOAT,2,&(*self).scatterRender) ||
        !renderPass(self,VK_FORMAT_R32G32B32A32_SFLOAT,1,&(*self).groupRender) ||
        !renderPass(self,VK_FORMAT_R8G8B8A8_SRGB,1,&(*self).finalRender))
        goto failed;
    VkDescriptorSetLayoutBinding bindings[4]={0};
    for (unsigned i=0;i<4;++i)
        bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo ds={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
    if (vkCreateDescriptorSetLayout((*self).device,&ds,nullptr,&(*self).scopeDescriptor)!=VK_SUCCESS)
        goto failed;
    bindings[0].stageFlags=VK_SHADER_STAGE_VERTEX_BIT; ds.bindingCount=1;
    if (vkCreateDescriptorSetLayout((*self).device,&ds,nullptr,&(*self).scatterDescriptor)!=VK_SUCCESS)
        goto failed;
    VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT,.size=36};
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&(*self).scopeDescriptor,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    if (vkCreatePipelineLayout((*self).device,&pl,nullptr,&(*self).scopeLayout)!=VK_SUCCESS)
        goto failed;
    push.stageFlags=VK_SHADER_STAGE_VERTEX_BIT; push.size=24; pl.pSetLayouts=&(*self).scatterDescriptor;
    if (vkCreatePipelineLayout((*self).device,&pl,nullptr,&(*self).scatterLayout)!=VK_SUCCESS)
        goto failed;
    const char *names[]={"scatter.vert","scatter.frag","resolve.vert","scope.frag"};
    VkShaderModule modules[4]={0};
    bool ready=true;
    for (unsigned i=0;i<4;++i) {
        modules[i]=loadShader(self,directory,names[i]);
        if (!modules[i])
            ready=false;
    }
    if (ready)
        ready=pipeline(self,modules[0],modules[1],(*self).scatterRender,(*self).scatterLayout,2,true,&(*self).scatterPipeline) &&
            pipeline(self,modules[2],modules[3],(*self).groupRender,(*self).scopeLayout,1,false,&(*self).groupPipeline) &&
            pipeline(self,modules[2],modules[3],(*self).finalRender,(*self).scopeLayout,1,false,&(*self).finalPipeline);
    for (unsigned i=0;i<4;++i)
        if (modules[i])
            vkDestroyShaderModule((*self).device,modules[i],nullptr);
    if (!ready)
        goto failed;
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (vkCreateSampler((*self).device,&si,nullptr,&(*self).sampler)!=VK_SUCCESS)
        goto failed;
    VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=Device_queueFamily(device)};
    if (vkCreateCommandPool((*self).device,&cp,nullptr,&(*self).commands)!=VK_SUCCESS)
        goto failed;
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=(*self).commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    if (vkAllocateCommandBuffers((*self).device,&ca,&(*self).command)!=VK_SUCCESS)
        goto failed;
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence((*self).device,&fi,nullptr,&(*self).fence)!=VK_SUCCESS)
        goto failed;
    return self;
failed:
    GpuScope_destroy(self); THROW("GpuScope Vulkan setup unsupported or failed"); return nullptr;
}
static void barrier(GpuScope *self,Texture *texture,VkImageLayout old,VkImageLayout next,
    VkAccessFlags from,VkAccessFlags to,VkPipelineStageFlags fromStage,VkPipelineStageFlags toStage) {
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=from,.dstAccessMask=to,
        .oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=(*texture).image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier((*self).command,fromStage,toStage,0,0,nullptr,0,nullptr,1,&b);
}
static bool upload(GpuScope *self,const Image *source,unsigned index) {
    Job *job=&(*self).job; Texture *t=&(*job).textures[index]; Buffer *b=&(*job).buffers[index];
    uint32_t w=Image_width(source),h=Image_height(source);
    if (!makeTexture(self,w,h,VK_FORMAT_R8G8B8A8_SRGB,t) ||
        !makeBuffer(self,(VkDeviceSize) w*h*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,b))
        return false;
    void *mappedPixelBytes;
    if (vkMapMemory((*self).device,(*b).memory,0,(*b).size,0,&mappedPixelBytes)!=VK_SUCCESS)
        return false;
    for (uint32_t y=0;y<h;++y)
        memcpy((uint8_t*) mappedPixelBytes+(size_t) y*w*4,Image_pixels(source)+(size_t) y*Image_stride(source),(size_t) w*4);
    vkUnmapMemory((*self).device,(*b).memory);
    barrier(self,t,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
    vkCmdCopyBufferToImage((*self).command,(*b).buffer,(*t).image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier(self,t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    return true;
}
static bool framebuffer(GpuScope *self,unsigned index,VkRenderPass render,unsigned first,unsigned count) {
    Job *job=&(*self).job; Texture *t=&(*job).textures[first]; VkImageView views[2];
    for (unsigned i=0;i<count;++i) { Texture *v=&(*job).textures[first+i]; views[i]=(*v).view; }
    VkFramebufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=render,
        .attachmentCount=count,.pAttachments=views,.width=(*t).width,.height=(*t).height,.layers=1};
    return vkCreateFramebuffer((*self).device,&info,nullptr,&(*job).frames[index])==VK_SUCCESS;
}
static void descriptor(GpuScope *self,unsigned set,unsigned binding,unsigned texture) {
    Job *job=&(*self).job; Texture *t=&(*job).textures[texture];
    VkDescriptorImageInfo image={(*self).sampler,(*t).view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=(*job).sets[set],
        .dstBinding=binding,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image};
    vkUpdateDescriptorSets((*self).device,1,&write,0,nullptr);
}
static void beginPass(GpuScope *self,unsigned frame,VkRenderPass render,VkPipeline pipeline,
    VkPipelineLayout layout,unsigned width,unsigned height,unsigned attachments) {
    Job *job=&(*self).job;
    VkClearValue clear[2]={0};
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=render,
        .framebuffer=(*job).frames[frame],.renderArea={{0,0},{width,height}},.clearValueCount=attachments,.pClearValues=clear};
    VkViewport viewport={0,0,(float) width,(float) height,0,1}; VkRect2D scissor={{0,0},{width,height}};
    vkCmdBeginRenderPass((*self).command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport((*self).command,0,1,&viewport); vkCmdSetScissor((*self).command,0,1,&scissor);
    vkCmdBindPipeline((*self).command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdBindDescriptorSets((*self).command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&(*job).sets[frame],0,nullptr);
}
static bool inputValid(const Image *image,uint32_t maxPixels,uint32_t maxExtent) {
    uint32_t w=Image_width(image),h=Image_height(image);
    return image && w && h && w<=maxExtent && h<=maxExtent && (uint64_t) w*h<=maxPixels &&
        (uint64_t) w*h<=SIZE_MAX/4 && Image_format(image)==IMAGE_FORMAT_RGBA8 && Image_pixels(image) && Image_stride(image)>=(uint64_t) w*4;
}
bool GpuScope_render(GpuScope *self,unsigned scope,const Image *prior,const Image *decoration,
    int32_t panelX,int32_t panelY,const Image *foreground,int32_t foregroundX,int32_t foregroundY,uint32_t radius,Image **out) {
    if (!self || !out || scope>GPU_SCOPE_ELEMENT || radius>FILTER_SCATTER_MAX_RADIUS)
        goto rejected;
    uint32_t max=(*self).maxPixels,extent=(*self).maxExtent;
    if (!inputValid(prior,max,extent) || !inputValid(decoration,max,extent) || !inputValid(foreground,max,extent))
        goto rejected;
    uint32_t w=Image_width(prior),h=Image_height(prior),pw=Image_width(decoration),ph=Image_height(decoration);
    if (panelX<0 || panelY<0 || (uint64_t) panelX+pw>w || (uint64_t) panelY+ph>h ||
        (int64_t) foregroundX+Image_width(foreground)>INT32_MAX ||
        (int64_t) foregroundY+Image_height(foreground)>INT32_MAX ||
        foregroundX<INT32_MIN+(int32_t) w+32 || foregroundY<INT32_MIN+(int32_t) h+32)
        goto rejected;
    uint32_t sw=scope==GPU_SCOPE_FOREGROUND ? Image_width(foreground) : w;
    uint32_t sh=scope==GPU_SCOPE_FOREGROUND ? Image_height(foreground) : h;
    uint64_t aw=(uint64_t) sw+2*radius,ah=(uint64_t) sh+2*radius;
    if (aw>extent || ah>extent || aw*ah>max || aw*ah>SIZE_MAX/16)
        goto rejected;
    if (!retire(self))
        goto rejected;
    Job *job=&(*self).job;
    if (vkResetCommandBuffer((*self).command,0)!=VK_SUCCESS)
        goto failed;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer((*self).command,&begin)!=VK_SUCCESS)
        goto failed;
    if (!upload(self,prior,PRIOR) || !upload(self,decoration,DECORATION) || !upload(self,foreground,FOREGROUND) ||
        !makeTexture(self,w,h,VK_FORMAT_R32G32B32A32_SFLOAT,&(*job).textures[GROUP]) ||
        !makeTexture(self,(uint32_t) aw,(uint32_t) ah,VK_FORMAT_R32G32B32A32_SFLOAT,&(*job).textures[ACCUMULATION]) ||
        !makeTexture(self,(uint32_t) aw,(uint32_t) ah,VK_FORMAT_R32G32B32A32_SFLOAT,&(*job).textures[WEIGHT]) ||
        !makeTexture(self,w,h,VK_FORMAT_R8G8B8A8_SRGB,&(*job).textures[OUTPUT]) ||
        !makeBuffer(self,(VkDeviceSize) w*h*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&(*job).buffers[READBACK]) ||
        !framebuffer(self,GROUP_FRAME,(*self).groupRender,GROUP,1) ||
        !framebuffer(self,SCATTER_FRAME,(*self).scatterRender,ACCUMULATION,2) ||
        !framebuffer(self,FINAL_FRAME,(*self).finalRender,OUTPUT,1))
        goto failed;
    VkDescriptorPoolSize poolSize={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,9};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=3,.poolSizeCount=1,.pPoolSizes=&poolSize};
    if (vkCreateDescriptorPool((*self).device,&dp,nullptr,&(*job).descriptors)!=VK_SUCCESS)
        goto failed;
    VkDescriptorSetLayout layouts[]={(*self).scopeDescriptor,(*self).scatterDescriptor,(*self).scopeDescriptor};
    VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=(*job).descriptors,.descriptorSetCount=3,.pSetLayouts=layouts};
    if (vkAllocateDescriptorSets((*self).device,&da,(*job).sets)!=VK_SUCCESS)
        goto failed;
    for (unsigned i=0;i<4;++i) {
        descriptor(self,GROUP_FRAME,i,i<3 ? i : PRIOR); // phase0 never reads filtered
        descriptor(self,FINAL_FRAME,i,i<3 ? i : ACCUMULATION);
    }
    unsigned source=scope==GPU_SCOPE_ELEMENT ? GROUP : scope==GPU_SCOPE_FOREGROUND ? FOREGROUND : PRIOR;
    descriptor(self,SCATTER_FRAME,0,source);
    int32_t push[]={(int32_t) scope,0,panelX,panelY,(int32_t) pw,(int32_t) ph,foregroundX,foregroundY,(int32_t) radius};
    if (scope==GPU_SCOPE_ELEMENT) {
        beginPass(self,GROUP_FRAME,(*self).groupRender,(*self).groupPipeline,(*self).scopeLayout,w,h,1);
        vkCmdPushConstants((*self).command,(*self).scopeLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof push,push);
        vkCmdDraw((*self).command,3,1,0,0); vkCmdEndRenderPass((*self).command);
    }
    beginPass(self,SCATTER_FRAME,(*self).scatterRender,(*self).scatterPipeline,(*self).scatterLayout,(uint32_t) aw,(uint32_t) ah,2);
    int32_t scatter[]={(int32_t) sw,(int32_t) sh,(int32_t) aw,(int32_t) ah,(int32_t) radius,scope==GPU_SCOPE_ELEMENT ? 0 : 1};
    vkCmdPushConstants((*self).command,(*self).scatterLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof scatter,scatter);
    vkCmdDraw((*self).command,6,sw*sh,0,0); vkCmdEndRenderPass((*self).command);
    push[1]=1;
    beginPass(self,FINAL_FRAME,(*self).finalRender,(*self).finalPipeline,(*self).scopeLayout,w,h,1);
    vkCmdPushConstants((*self).command,(*self).scopeLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof push,push);
    vkCmdDraw((*self).command,3,1,0,0); vkCmdEndRenderPass((*self).command);
    Texture *target=&(*job).textures[OUTPUT]; Buffer *read=&(*job).buffers[READBACK];
    barrier(self,target,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={w,h,1}};
    vkCmdCopyImageToBuffer((*self).command,(*target).image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,(*read).buffer,1,&copy);
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=(*read).buffer,.size=(*read).size};
    vkCmdPipelineBarrier((*self).command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    if (vkEndCommandBuffer((*self).command)!=VK_SUCCESS || vkResetFences((*self).device,1,&(*self).fence)!=VK_SUCCESS)
        goto failed;
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&(*self).command};
    VkResult submitted=vkQueueSubmit((*self).queue,1,&submit,(*self).fence);
    if (submitted==VK_ERROR_DEVICE_LOST) {
        (*self).pending=true; // completion uncertain: retain, never free under GPU
        goto rejected;
    }
    if (submitted!=VK_SUCCESS)
        goto failed;
    (*self).pending=true;
    if (vkWaitForFences((*self).device,1,&(*self).fence,VK_TRUE,GPU_WAIT_NS)!=VK_SUCCESS)
        goto rejected;
    (*self).pending=false;
    Image *result=Image_2(w,h);
    if (!result || !Image_ensureShadow(result,w,h)) { Image_destroy(result); goto failed; }
    void *mappedPixelBytes;
    if (vkMapMemory((*self).device,(*read).memory,0,(*read).size,0,&mappedPixelBytes)!=VK_SUCCESS) { Image_destroy(result); goto failed; }
    for (uint32_t y=0;y<h;++y)
        memcpy(Image_pixels(result)+(size_t) y*Image_stride(result),(uint8_t*) mappedPixelBytes+(size_t) y*w*4,(size_t) w*4);
    vkUnmapMemory((*self).device,(*read).memory); releaseJob(self); *out=result; return true;
failed:
    releaseJob(self);
rejected:
    THROW("GpuScope rejected or failed GPU scope render"); return false;
}
static void format(const GpuScope *self,bool structure,char *dest,size_t cap,bool *outTruncated) {
    if (!dest || !cap) {
        if (outTruncated)
            *outTruncated=true;
        return;
    }
    int n;
    if (!self)
        n=snprintf(dest,cap,"nullptr");
    else if (!structure)
        n=snprintf(dest,cap,"GpuScope(Vulkan scatter, budget=%u,pending=%d)",(*self).maxPixels,(*self).pending);
    else n=snprintf(dest,cap,"GpuScope{device=%p,physical=%p,queue=%p,maxPixels=%u,maxExtent=%u,scatterRender=%p,groupRender=%p,finalRender=%p,scatterDescriptor=%p,scopeDescriptor=%p,scatterLayout=%p,scopeLayout=%p,scatterPipeline=%p,groupPipeline=%p,finalPipeline=%p,sampler=%p,commands=%p,command=%p,fence=%p,pending=%d,job=GPU transport records}",
        (void*) (*self).device,(void*) (*self).physical,(void*) (*self).queue,(*self).maxPixels,(*self).maxExtent,
        (void*) (*self).scatterRender,(void*) (*self).groupRender,(void*) (*self).finalRender,
        (void*) (*self).scatterDescriptor,(void*) (*self).scopeDescriptor,(void*) (*self).scatterLayout,(void*) (*self).scopeLayout,
        (void*) (*self).scatterPipeline,(void*) (*self).groupPipeline,(void*) (*self).finalPipeline,(void*) (*self).sampler,
        (void*) (*self).commands,(void*) (*self).command,(void*) (*self).fence,(*self).pending);
    if (outTruncated)
        *outTruncated=n<0 || (size_t) n>=cap;
}
void GpuScope_toString(const GpuScope *self,char *dest,size_t cap,bool *outTruncated) { format(self,false,dest,cap,outTruncated); }
void GpuScope_toStringStruct(const GpuScope *self,char *dest,size_t cap,bool *outTruncated) { format(self,true,dest,cap,outTruncated); }
