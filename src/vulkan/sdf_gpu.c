#include "vulkan/sdf_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <mach-o/dyld.h>
#include "vk_guard.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Sdf_gpu (vulkan/sdf_gpu.c)
 * LEVEL: L4 — Self-Management (Vulkan GPU SDF baker setup)
 * ============================================================================
 * GPU jump-flood SDF baker for font atlases.
 *
 * STRUCT FIELDS: none — procedural (module-level GPU pipeline state only).
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - SdfGpu_initModule(instance, gpa, phys, device, queue, queueFamily)
 *   - SdfGpu_shutdown(void)
 *   - SdfGpu_available(void)
 *   - SdfGpu_bakePage(coverage, dim, outSdf)
 *     (Rule 39 net: bakePage guards the compute driver at entry)
 *   - SdfGpu_pageDim(void)
 * ============================================================================
 */


// sdf_gpu.c — jump-flood SDF baker (see sdf_gpu.h).
//
// One descriptor set for both pipelines (all storage buffers — MoltenVK's
// Metal argument-buffer path rejects the storage-image variant):
//   b0: coverage words (RO, bytes packed 4-per-word)
//   b1: sdf words (atomically OR'd by combine)
//   b2/b3: seed buffers A/B (ping-pong via push constants)
// Per page: 2 seed + 22 flood + 1 combine dispatches on the shared queue.

#define SDF_DIM 2048
#define SDF_GROUP 16
#define SDF_WORDS ((size_t)SDF_DIM * SDF_DIM / 4) // byte-packed words (cov/sdf)
#define SDF_SEED_WORDS ((size_t)SDF_DIM * SDF_DIM) // one seed word per pixel

static VkInstance s_instance = VK_NULL_HANDLE;
static PFN_vkGetInstanceProcAddr s_gpa = nullptr;
static PFN_vkGetDeviceProcAddr s_gdpa = nullptr;
static VkPhysicalDevice s_phys = VK_NULL_HANDLE;
static VkDevice s_device = VK_NULL_HANDLE;
static VkQueue s_queue = VK_NULL_HANDLE;
static uint32_t s_queueFamily = 0;
static bool s_ready = false;

static VkBuffer s_buffers[4] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
static VkDeviceMemory s_mems[4] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
static void *s_covMap = nullptr, *s_sdfMap = nullptr;
static VkDescriptorSetLayout s_layout = VK_NULL_HANDLE;
static VkDescriptorPool s_pool = VK_NULL_HANDLE;
static VkDescriptorSet s_set = VK_NULL_HANDLE;
static VkPipelineLayout s_floodLayout = VK_NULL_HANDLE, s_combineLayout = VK_NULL_HANDLE;
static VkPipeline s_floodPipe = VK_NULL_HANDLE, s_combinePipe = VK_NULL_HANDLE;
static VkCommandPool s_cmdPool = VK_NULL_HANDLE;
static VkCommandBuffer s_cmd = VK_NULL_HANDLE;
static VkFence s_fence = VK_NULL_HANDLE;

#define G(fn) s_gpa(s_instance, "vk" #fn)
#define D(fn) s_gdpa(s_device, "vk" #fn)

int SdfGpu_pageDim(void) { return SDF_DIM; }
bool SdfGpu_available(void) { return s_ready; }

// --- SPV lookup (compact copy of the vulkan.c search: ANTI_SPV_DIR first,
// then exe-relative, then CWD-relative) -------------------------------------
static unsigned char *loadSpvFile(const char *path, size_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size % 4 != 0) {
        fclose(f);
        return nullptr;
    }
    unsigned char *bytes = (unsigned char*) malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return nullptr;
    }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        return nullptr;
    }
    fclose(f);
    *outSize = (size_t)size;
    return bytes;
}

static unsigned char *loadSpvAny(const char *name, size_t *outSize) {
    char path[1024];
    unsigned char *code = nullptr;
#ifdef ANTI_SPV_DIR
    snprintf(path, sizeof(path), "%s/%s", ANTI_SPV_DIR, name);
    code = loadSpvFile(path, outSize);
    if (code)
        return code;
#endif
    uint32_t exeSize = sizeof(path);
    if (_NSGetExecutablePath(path, &exeSize) == 0) {
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = 0;
            char candidate[1024];
            // Inside .app bundle: Contents/MacOS/.. -> Contents/Resources/spv/
            snprintf(candidate, sizeof(candidate), "%s/../Resources/spv/%s", path, name);
            code = loadSpvFile(candidate, outSize);
            if (code)
                return code;
            snprintf(candidate, sizeof(candidate), "%s/../src/vulkan/spv/%s", path, name);
            code = loadSpvFile(candidate, outSize);
            if (code)
                return code;
            snprintf(candidate, sizeof(candidate), "%s/spv/%s", path, name);
            code = loadSpvFile(candidate, outSize);
            if (code)
                return code;
        }
    }
    snprintf(path, sizeof(path), "src/vulkan/spv/%s", name);
    return loadSpvFile(path, outSize);
}

static uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) {
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemProps =
        (PFN_vkGetPhysicalDeviceMemoryProperties)G(GetPhysicalDeviceMemoryProperties);
    VkPhysicalDeviceMemoryProperties mp;
    GetMemProps(s_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

static VkShaderModule loadModule(const char *name) {
    PFN_vkCreateShaderModule CreateShaderModule =
        (PFN_vkCreateShaderModule)D(CreateShaderModule);
    if (!CreateShaderModule)
        return VK_NULL_HANDLE;
    size_t size = 0;
    unsigned char *code = loadSpvAny(name, &size);
    if (!code) {
        fprintf(stderr, "sdf-gpu: spv missing: %s\n", name);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = (const uint32_t*) code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (CreateShaderModule(s_device, &ci, nullptr, &mod) != VK_SUCCESS)
        mod = VK_NULL_HANDLE;
    free(code);
    return mod;
}

bool SdfGpu_initModule(void *instance, void *gpa, void *phys, void *device,
                       void *queue, uint32_t queueFamily) {
    if (s_ready)
        return true;
    s_instance = (VkInstance)instance;
    s_gpa = (PFN_vkGetInstanceProcAddr)gpa;
    s_phys = (VkPhysicalDevice)phys;
    s_device = (VkDevice)device;
    s_queue = (VkQueue)queue;
    s_queueFamily = queueFamily;
    if (!s_instance || !s_gpa || !s_device) {
        SdfGpu_shutdown();
        return false;
    }
    s_gdpa = (PFN_vkGetDeviceProcAddr)s_gpa(s_instance, "vkGetDeviceProcAddr");
    if (!s_gdpa) {
        SdfGpu_shutdown();
        return false;
    }

    PFN_vkCreateBuffer CreateBuffer = (PFN_vkCreateBuffer)D(CreateBuffer);
    PFN_vkGetBufferMemoryRequirements GetBufferMemReq =
        (PFN_vkGetBufferMemoryRequirements)D(GetBufferMemoryRequirements);
    PFN_vkAllocateMemory AllocateMemory = (PFN_vkAllocateMemory)D(AllocateMemory);
    PFN_vkBindBufferMemory BindBufferMemory = (PFN_vkBindBufferMemory)D(BindBufferMemory);
    PFN_vkMapMemory MapMemory = (PFN_vkMapMemory)D(MapMemory);
    if (!CreateBuffer || !GetBufferMemReq || !AllocateMemory || !BindBufferMemory || !MapMemory) {
        fprintf(stderr, "sdf-gpu: loader missing buffer symbols\n");
        SdfGpu_shutdown();
        return false;
    }

    // Four staging buffers, host-visible coherent (unified memory).
    // Buffers 0/1 pack bytes 4-per-word; seed buffers hold one word/pixel.
    for (int k = 0; k < 4; k++) {
        VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = (VkDeviceSize)(k < 2 ? SDF_WORDS : SDF_SEED_WORDS) * 4;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (CreateBuffer(s_device, &bi, nullptr, &s_buffers[k]) != VK_SUCCESS) {
            fprintf(stderr, "sdf-gpu: staging buffer %d failed\n", k);
            SdfGpu_shutdown();
            return false;
        }
        VkMemoryRequirements req;
        GetBufferMemReq(s_device, s_buffers[k], &req);
        uint32_t type = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = type;
        if (type == UINT32_MAX || AllocateMemory(s_device, &ai, nullptr, &s_mems[k]) != VK_SUCCESS ||
            BindBufferMemory(s_device, s_buffers[k], s_mems[k], 0) != VK_SUCCESS) {
            fprintf(stderr, "sdf-gpu: staging memory %d failed\n", k);
            SdfGpu_shutdown();
            return false;
        }
    }
    void *map0 = nullptr, *map1 = nullptr;
    if (MapMemory(s_device, s_mems[0], 0, VK_WHOLE_SIZE, 0, &map0) != VK_SUCCESS ||
        MapMemory(s_device, s_mems[1], 0, VK_WHOLE_SIZE, 0, &map1) != VK_SUCCESS) {
        fprintf(stderr, "sdf-gpu: map failed\n");
        SdfGpu_shutdown();
        return false;
    }
    s_covMap = map0;
    s_sdfMap = map1;

    PFN_vkCreateDescriptorSetLayout CreateLayout =
        (PFN_vkCreateDescriptorSetLayout)D(CreateDescriptorSetLayout);
    PFN_vkCreateDescriptorPool CreatePool = (PFN_vkCreateDescriptorPool)D(CreateDescriptorPool);
    PFN_vkAllocateDescriptorSets AllocSets = (PFN_vkAllocateDescriptorSets)D(AllocateDescriptorSets);
    PFN_vkUpdateDescriptorSets UpdateSets = (PFN_vkUpdateDescriptorSets)D(UpdateDescriptorSets);
    PFN_vkCreatePipelineLayout CreatePipeLayout =
        (PFN_vkCreatePipelineLayout)D(CreatePipelineLayout);
    PFN_vkCreateComputePipelines CreateCompute =
        (PFN_vkCreateComputePipelines)D(CreateComputePipelines);
    PFN_vkCreateCommandPool CreateCmdPool = (PFN_vkCreateCommandPool)D(CreateCommandPool);
    PFN_vkAllocateCommandBuffers AllocCmds = (PFN_vkAllocateCommandBuffers)D(AllocateCommandBuffers);
    PFN_vkCreateFence CreateFence = (PFN_vkCreateFence)D(CreateFence);
    if (!CreateLayout || !CreatePool || !AllocSets || !UpdateSets || !CreatePipeLayout ||
        !CreateCompute || !CreateCmdPool || !AllocCmds || !CreateFence) {
        fprintf(stderr, "sdf-gpu: descriptor/pipeline symbols missing\n");
        SdfGpu_shutdown();
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[4];
    memset(bindings, 0, sizeof(bindings));
    for (int k = 0; k < 4; k++) {
        bindings[k].binding = (uint32_t)k;
        bindings[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[k].descriptorCount = 1;
        bindings[k].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    li.bindingCount = 4;
    li.pBindings = bindings;
    if (CreateLayout(s_device, &li, nullptr, &s_layout) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 };
    VkDescriptorPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &poolSize;
    if (CreatePool(s_device, &pi, nullptr, &s_pool) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkDescriptorSetAllocateInfo ai2 = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai2.descriptorPool = s_pool;
    ai2.descriptorSetCount = 1;
    ai2.pSetLayouts = &s_layout;
    if (AllocSets(s_device, &ai2, &s_set) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkDescriptorBufferInfo bufInfos[4];
    VkWriteDescriptorSet writes[4];
    memset(writes, 0, sizeof(writes));
    for (int k = 0; k < 4; k++) {
        bufInfos[k].buffer = s_buffers[k];
        bufInfos[k].offset = 0;
        bufInfos[k].range = VK_WHOLE_SIZE;
        writes[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[k].dstSet = s_set;
        writes[k].dstBinding = (uint32_t)k;
        writes[k].descriptorCount = 1;
        writes[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[k].pBufferInfo = &bufInfos[k];
    }
    UpdateSets(s_device, 4, writes, 0, nullptr);

    VkShaderModule floodMod = loadModule("sdf_jfa.spv");
    VkShaderModule combineMod = loadModule("sdf_combine.spv");
    if (floodMod == VK_NULL_HANDLE || combineMod == VK_NULL_HANDLE) {
        PFN_vkDestroyShaderModule DestroyMod = (PFN_vkDestroyShaderModule)D(DestroyShaderModule);
        if (DestroyMod) {
            if (floodMod)
                DestroyMod(s_device, floodMod, nullptr);
            if (combineMod)
                DestroyMod(s_device, combineMod, nullptr);
        }
        fprintf(stderr, "sdf-gpu: compute spv missing\n");
        SdfGpu_shutdown();
        return false;
    }
    // Push ranges: flood {mode, dst, polarity, step, dim} and
    // combine {inSel, outSel, dim, onedge, distScale} are both 20 bytes.
    VkPushConstantRange floodRange = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 20 };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &floodRange;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &s_layout;
    if (CreatePipeLayout(s_device, &pli, nullptr, &s_floodLayout) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkPushConstantRange combineRange = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 20 };
    pli.pPushConstantRanges = &combineRange;
    if (CreatePipeLayout(s_device, &pli, nullptr, &s_combineLayout) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkComputePipelineCreateInfo cpi[2];
    memset(cpi, 0, sizeof(cpi));
    for (int k = 0; k < 2; k++) {
        cpi[k].sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi[k].stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpi[k].stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpi[k].stage.module = k == 0 ? floodMod : combineMod;
        cpi[k].stage.pName = "main";
        cpi[k].layout = k == 0 ? s_floodLayout : s_combineLayout;
    }
    {
        VkPipeline pipes[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkResult pcRes = CreateCompute(s_device, VK_NULL_HANDLE, 2, cpi, nullptr, pipes);
        if (pcRes != VK_SUCCESS) {
            fprintf(stderr, "sdf-gpu: CreateComputePipelines failed: %d\n", pcRes);
            SdfGpu_shutdown();
            return false;
        }
        s_floodPipe = pipes[0];
        s_combinePipe = pipes[1];
    }
    {
        PFN_vkDestroyShaderModule DestroyMod = (PFN_vkDestroyShaderModule)D(DestroyShaderModule);
        if (DestroyMod) {
            DestroyMod(s_device, floodMod, nullptr);
            DestroyMod(s_device, combineMod, nullptr);
        }
    }

    VkCommandPoolCreateInfo cpool = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpool.queueFamilyIndex = s_queueFamily;
    if (CreateCmdPool(s_device, &cpool, nullptr, &s_cmdPool) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkCommandBufferAllocateInfo cba = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cba.commandPool = s_cmdPool;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    if (AllocCmds(s_device, &cba, &s_cmd) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (CreateFence(s_device, &fi, nullptr, &s_fence) != VK_SUCCESS) {
        SdfGpu_shutdown();
        return false;
    }

    s_ready = true;
    fprintf(stderr, "sdf-gpu: ready (jump-flood SDF baker online)\n");
    return true;
}

void SdfGpu_shutdown(void) {
    if (s_device != VK_NULL_HANDLE && s_gdpa) {
        PFN_vkDeviceWaitIdle WaitIdle = (PFN_vkDeviceWaitIdle)s_gdpa(s_device, "vkDeviceWaitIdle");
        PFN_vkUnmapMemory UnmapMemory = (PFN_vkUnmapMemory)s_gdpa(s_device, "vkUnmapMemory");
        PFN_vkDestroyFence DestroyFence = (PFN_vkDestroyFence)s_gdpa(s_device, "vkDestroyFence");
        PFN_vkDestroyCommandPool DestroyPool = (PFN_vkDestroyCommandPool)s_gdpa(s_device, "vkDestroyCommandPool");
        PFN_vkDestroyPipeline DestroyPipe = (PFN_vkDestroyPipeline)s_gdpa(s_device, "vkDestroyPipeline");
        PFN_vkDestroyPipelineLayout DestroyLayout =
            (PFN_vkDestroyPipelineLayout)s_gdpa(s_device, "vkDestroyPipelineLayout");
        PFN_vkDestroyDescriptorPool DestroyDescPool =
            (PFN_vkDestroyDescriptorPool)s_gdpa(s_device, "vkDestroyDescriptorPool");
        PFN_vkDestroyDescriptorSetLayout DestroySetLayout =
            (PFN_vkDestroyDescriptorSetLayout)s_gdpa(s_device, "vkDestroyDescriptorSetLayout");
        PFN_vkDestroyBuffer DestroyBuffer = (PFN_vkDestroyBuffer)s_gdpa(s_device, "vkDestroyBuffer");
        PFN_vkFreeMemory FreeMemory = (PFN_vkFreeMemory)s_gdpa(s_device, "vkFreeMemory");
        if (WaitIdle)
            WaitIdle(s_device);
        if (UnmapMemory) {
            if (s_covMap)
                UnmapMemory(s_device, s_mems[0]);
            if (s_sdfMap)
                UnmapMemory(s_device, s_mems[1]);
        }
        if (DestroyFence && s_fence)
            DestroyFence(s_device, s_fence, nullptr);
        if (DestroyPool && s_cmdPool)
            DestroyPool(s_device, s_cmdPool, nullptr);
        if (DestroyPipe) {
            if (s_floodPipe)
                DestroyPipe(s_device, s_floodPipe, nullptr);
            if (s_combinePipe)
                DestroyPipe(s_device, s_combinePipe, nullptr);
        }
        if (DestroyLayout) {
            if (s_floodLayout)
                DestroyLayout(s_device, s_floodLayout, nullptr);
            if (s_combineLayout)
                DestroyLayout(s_device, s_combineLayout, nullptr);
        }
        if (DestroyDescPool && s_pool)
            DestroyDescPool(s_device, s_pool, nullptr);
        if (DestroySetLayout && s_layout)
            DestroySetLayout(s_device, s_layout, nullptr);
        if (DestroyBuffer) {
            for (int k = 0; k < 4; k++) {
                if (s_buffers[k])
                    DestroyBuffer(s_device, s_buffers[k], nullptr);
            }
        }
        if (FreeMemory) {
            for (int k = 0; k < 4; k++) {
                if (s_mems[k])
                    FreeMemory(s_device, s_mems[k], nullptr);
            }
        }
    } else {
        s_instance = VK_NULL_HANDLE;
        s_gpa = nullptr;
        s_gdpa = nullptr;
    }
    for (int k = 0; k < 4; k++) {
        s_buffers[k] = VK_NULL_HANDLE;
        s_mems[k] = VK_NULL_HANDLE;
    }
    s_covMap = s_sdfMap = nullptr;
    s_layout = VK_NULL_HANDLE;
    s_pool = VK_NULL_HANDLE;
    s_set = VK_NULL_HANDLE;
    s_floodLayout = s_combineLayout = VK_NULL_HANDLE;
    s_floodPipe = s_combinePipe = VK_NULL_HANDLE;
    s_cmdPool = VK_NULL_HANDLE;
    s_cmd = VK_NULL_HANDLE;
    s_fence = VK_NULL_HANDLE;
    s_device = VK_NULL_HANDLE;
    s_instance = VK_NULL_HANDLE;
    s_gpa = nullptr;
    s_gdpa = nullptr;
    s_ready = false;
}

bool SdfGpu_bakePage(const uint8_t *coverage, int dim, uint8_t *outSdf) {
    if (!VkGuard_check("SdfGpu_bakePage", s_device, s_queue, false))
        return false;
    if (!s_ready || !coverage || !outSdf || dim != SDF_DIM)
        return false;
    PFN_vkBeginCommandBuffer BeginCmd = (PFN_vkBeginCommandBuffer)D(BeginCommandBuffer);
    PFN_vkCmdBindPipeline BindPipe = (PFN_vkCmdBindPipeline)D(CmdBindPipeline);
    PFN_vkCmdBindDescriptorSets BindSets = (PFN_vkCmdBindDescriptorSets)D(CmdBindDescriptorSets);
    PFN_vkCmdPushConstants Push = (PFN_vkCmdPushConstants)D(CmdPushConstants);
    PFN_vkCmdDispatch Dispatch = (PFN_vkCmdDispatch)D(CmdDispatch);
    PFN_vkCmdPipelineBarrier Barrier = (PFN_vkCmdPipelineBarrier)D(CmdPipelineBarrier);
    PFN_vkEndCommandBuffer EndCmd = (PFN_vkEndCommandBuffer)D(EndCommandBuffer);
    PFN_vkQueueSubmit QueueSubmit = (PFN_vkQueueSubmit)D(QueueSubmit);
    PFN_vkWaitForFences WaitFences = (PFN_vkWaitForFences)D(WaitForFences);
    PFN_vkResetFences ResetFences = (PFN_vkResetFences)D(ResetFences);
    if (!BeginCmd || !BindPipe || !BindSets || !Push || !Dispatch || !Barrier ||
        !EndCmd || !QueueSubmit || !WaitFences || !ResetFences)
        return false;

    // Pack coverage bytes into words + zero the sdf words (host side).
    uint32_t *covWords = (uint32_t*) s_covMap;
    uint32_t *sdfWords = (uint32_t*) s_sdfMap;
    for (size_t i = 0; i < SDF_WORDS; i++) {
        size_t b = i * 4;
        covWords[i] = (uint32_t)coverage[b] | ((uint32_t)coverage[b + 1] << 8) |
                      ((uint32_t)coverage[b + 2] << 16) | ((uint32_t)coverage[b + 3] << 24);
        sdfWords[i] = 0;
    }

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (BeginCmd(s_cmd, &bi) != VK_SUCCESS)
        return false;

    const uint32_t groups = SDF_DIM / SDF_GROUP;
    VkBufferMemoryBarrier bbar;
    memset(&bbar, 0, sizeof(bbar));
    bbar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bbar.size = VK_WHOLE_SIZE;
    bbar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bbar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    // Host -> shader visibility for the coverage upload (seeds untouched yet).
    bbar.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    bbar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bbar.buffer = s_buffers[0];
    Barrier(s_cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &bbar, 0, nullptr);

    BindPipe(s_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_floodPipe);
    BindSets(s_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_floodLayout, 0, 1, &s_set, 0, nullptr);

    struct { int mode, dst, polarity, step, dim; } floodPc;
    floodPc.dim = SDF_DIM;
    // Seeds: inside -> A (buffer 2), outside -> B (buffer 3).
    floodPc.mode = 0;
    floodPc.step = 0;
    floodPc.dst = 0;
    floodPc.polarity = 1;
    Push(s_cmd, s_floodLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(floodPc), &floodPc);
    Dispatch(s_cmd, groups, groups, 1);
    floodPc.dst = 1;
    floodPc.polarity = 0;
    Push(s_cmd, s_floodLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(floodPc), &floodPc);
    Dispatch(s_cmd, groups, groups, 1);

    // Seeds -> floods visibility on both seed buffers.
    VkBufferMemoryBarrier seedBars[2];
    memset(seedBars, 0, sizeof(seedBars));
    for (int k = 0; k < 2; k++) {
        seedBars[k].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        seedBars[k].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        seedBars[k].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        seedBars[k].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        seedBars[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        seedBars[k].buffer = s_buffers[2 + k];
        seedBars[k].size = VK_WHOLE_SIZE;
    }
    Barrier(s_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, seedBars, 0, nullptr);

    // Flood both fields, 1024 -> 1. A holds inside seeds, B outside seeds;
    // each field ping-pongs independently (dst selects the write buffer).
    floodPc.mode = 1;
    int inBuf = 0, outBuf = 1; // buffer indices 2+inBuf / 2+outBuf
    for (int pass = 0; pass < 2; pass++) {
        int cur = pass == 0 ? inBuf : outBuf;
        for (int step = SDF_DIM / 2; step >= 1; step /= 2) {
            int other = cur == 0 ? 1 : 0;
            floodPc.dst = other;
            floodPc.step = step;
            Push(s_cmd, s_floodLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(floodPc), &floodPc);
            Dispatch(s_cmd, groups, groups, 1);
            bbar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            bbar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            bbar.buffer = s_buffers[2 + other];
            Barrier(s_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bbar, 0, nullptr);
            cur = other;
        }
        // JFA+1: one extra step-1 pass cleans the rare occluded-seed spots.
        {
            int other = cur == 0 ? 1 : 0;
            floodPc.dst = other;
            floodPc.step = 1;
            Push(s_cmd, s_floodLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(floodPc), &floodPc);
            Dispatch(s_cmd, groups, groups, 1);
            bbar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            bbar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            bbar.buffer = s_buffers[2 + other];
            Barrier(s_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bbar, 0, nullptr);
            cur = other;
        }
        if (pass == 0)
            inBuf = cur;
        else
            outBuf = cur;
    }
    // After 11 (odd) floods each field lands on the opposite buffer.
    BindPipe(s_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_combinePipe);
    BindSets(s_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_combineLayout, 0, 1, &s_set, 0, nullptr);
    struct { int inSel, outSel, dim; float onedge, distScale; } combinePc;
    combinePc.inSel = inBuf;
    combinePc.outSel = outBuf;
    combinePc.dim = SDF_DIM;
    combinePc.onedge = 128.0f;
    combinePc.distScale = 16.0f;
    Push(s_cmd, s_combineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(combinePc), &combinePc);
    Dispatch(s_cmd, groups, groups, 1);

    // Shader -> host visibility for the sdf readback.
    bbar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bbar.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bbar.buffer = s_buffers[1];
    Barrier(s_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &bbar, 0, nullptr);

    if (EndCmd(s_cmd) != VK_SUCCESS)
        return false;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cmd;
    if (QueueSubmit(s_queue, 1, &si, s_fence) != VK_SUCCESS)
        return false;
    if (WaitFences(s_device, 1, &s_fence, VK_TRUE, 120000000000ull) != VK_SUCCESS)
        return false;
    ResetFences(s_device, 1, &s_fence);

    for (size_t i = 0; i < SDF_WORDS; i++) {
        uint32_t wv = sdfWords[i];
        size_t b = i * 4;
        outSdf[b] = (uint8_t)(wv & 0xFF);
        outSdf[b + 1] = (uint8_t)((wv >> 8) & 0xFF);
        outSdf[b + 2] = (uint8_t)((wv >> 16) & 0xFF);
        outSdf[b + 3] = (uint8_t)((wv >> 24) & 0xFF);
    }
    return true;
}
