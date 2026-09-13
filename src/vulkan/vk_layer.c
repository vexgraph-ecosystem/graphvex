#include <vulkan/vulkan_core.h>
#include "vulkan/vk_layer.h"
#include "vulkan/vk.h"
#include "vulkan/vk_mac.h"
#include "vulkan/vk_guard.h"
#include "annotation/overview.h"
#include "atomic/spin.h"

#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkLayer (vulkan/vk_layer.c)
 * LEVEL: L4 — Self-Management (retained offscreen GPU target lifecycle)
 * ============================================================================
 * Registry of retained offscreen render targets — the COMPOSITED scene model
 * of the Present-On-Demand law (Rule 14). A scene owns a fixed pixel-size
 * flight target (two record/submit slots, flip/flop) rendered into on the
 * present worker by VkLayer_visit; the canvas painter samples the
 * last-published flight image as a textured quad via VkLayer_composite,
 * collaged into the board pass at the scene's anchor rect. One canvas total
 * — no per-scene CAMetalLayer surfaces. DIRECT scenes use VkPane instead.
 *
 * structural invariant (Rule 14): composite != render. visit() invokes the
 * scene's render handler into its retained target; composite() only copies
 * published pixels. The canvas can never re-invoke a scene render.
 *
 * STRUCT FIELDS (local registry):
 * ----------------------------------------------------------------------------
 *   VkLayerChain chain[VK_LAYER_CHAIN_MAX];  // one per layer, fixed array
 *     images[VK_LAYER_FLIGHT]      // offscreen BGRA8 color targets
 *     views[VK_LAYER_FLIGHT]       // their image views (composite samples)
 *     fbs[VK_LAYER_FLIGHT]         // per-slot framebuffers
 *     mem[VK_LAYER_FLIGHT]         // backing device memory (molten unified)
 *     cb[VK_LAYER_FLIGHT], fence[VK_LAYER_FLIGHT], flip // flip/flop pace
 *     published                    // last-rendered slot (-1 = none yet)
 *     dirty                        // per-layer repaint demand (no new class)
 *     presentCount, skipCount      // lifetime render/skip diagnostics
 *   int count;
 *   VkRenderPass s_layerPass;      // BGRA8 CLEAR->SHADER_READ_ONLY dedicated
 *   VkCommandPool s_pool;          // per-layer CBs from one pool
 *   VkLayerRenderFn s_renderer;    // darling scene painter hook
 *   s_compLayout/s_compPipeline    // composite quad pipeline (single sampler)
 *   s_compDescLayout/Pool/Set      // one mutable sampler2D set, per-draw update
 *   s_compSampler                  // linear clamp-to-edge, shared
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - VkLayer_register(w, h, owner) : allocate flight targets + CBs + fences
 *                                    (marks layer dirty: first render)
 *   - ensureLayerPass(format)       : BGRA8 offscreen render pass
 *   - buildLayerTargets(chain)      : images + views + framebuffers + memory
 *   - ensureCompositePipeline()     : collaging quad pipeline + sampler
 *
 * Core Functions:
 *   - VkLayer_visit()               : render dirty layers into flight targets
 *                                    (Rule 39 seam guard at entry; skips clean
 *                                    layers after fence poll; clears demand on
 *                                    render; publishes the flipped slot)
 *   - VkLayer_composite(cb, ...)    : sample a published layer as a quad into
 *                                    an already-begun render pass (collage)
 *   - VkLayer_unregister(index)     : destroy targets, release CBs
 *   - VkLayer_resize(index, w, h)   : offscreen rebuild (no-op when unchanged)
 *   - VkLayer_shutdown()            : destroy all chains + pipeline + pool
 *
 * Setters:
 *   - VkLayer_setRenderer(fn)
 *   - VkLayer_markDirty(index, dirty) : per-layer repaint demand (Rule 24
 *                                       symmetric pair with isDirty)
 *
 * Getters:
 *   - VkLayer_ready() / VkLayer_count()
 *   - VkLayer_find(owner)           : slot index for the owner handle
 *   - VkLayer_isDirty(index)        : per-layer repaint demand probe
 *   - VkLayer_flightIdle()          : true when no layer submit is pending
 *   - VkLayer_presentCount(index) / VkLayer_skipCount(index)
 * ============================================================================
 */

// Device/queue/instance state owned by vulkan.c (externed like vulkan_mac.c).
extern VkDevice s_instanceDevice;
extern VkQueue s_instanceQueue;
extern PFN_vkGetDeviceProcAddr s_instanceGdpa;
extern VkInstance s_instanceInstance;
extern PFN_vkGetInstanceProcAddr s_instanceGpa;
extern VkPhysicalDevice s_instancePhys;
extern uint32_t s_instanceQueueFamily;
extern bool s_instanceDebugUtils;   // set when VK_EXT_debug_utils is live on the device

#define VK_LAYER_LOAD_DEVICE(name) \
    static PFN_vk##name name##_fn; \
    if (!name##_fn) { \
        name##_fn = (PFN_vk##name)s_instanceGdpa(s_instanceDevice, "vk" #name); \
    }

#define VK_LAYER_CHAIN_MAX 32
#define VK_LAYER_FLIGHT 2
#define VK_LAYER_FORMAT VK_FORMAT_B8G8R8A8_UNORM

// Per-scene flight depth: each layer owns TWO record/submit slots and
// alternates (flip/flop) every render. While slot A's submit still flies,
// the next tick records into slot B without stalling on A's fence.
typedef struct VkLayerChain {
    bool active;             // slot in use (stable index; no compaction)
    void *owner;             // darling Panel* (renderer callback arg)
    VkExtent2D extent;       // fixed pixel size (register/resize time)
    VkImage images[VK_LAYER_FLIGHT];
    VkImageView views[VK_LAYER_FLIGHT];
    VkFramebuffer fbs[VK_LAYER_FLIGHT];
    VkDeviceMemory mem[VK_LAYER_FLIGHT];
    VkCommandBuffer cb[VK_LAYER_FLIGHT]; // per-scene record buffers (flip/flop)
    VkFence fence[VK_LAYER_FLIGHT];      // per-slot submit fences (start signaled)
    uint32_t flip;           // next slot to record (alternates each render)
    int published;           // last-rendered slot composite samples (-1 = none)
    bool dirty;              // repaint demand: set on register/resize/mark,
                             // cleared after a successful render
    uint64_t presentCount;   // lifetime successful renders (diagnostic)
    uint64_t skipCount;      // lifetime fence-poll + clean skips (diagnostic)
    uint64_t fenceTimeoutNs; // 100ms bounded (Rule 27)
} VkLayerChain;

static VkLayerChain s_chains[VK_LAYER_CHAIN_MAX] = {0};
// Registry lock (two-thread live-resize contract): the present worker runs
// VkLayer_visit every frame while thread 0 may VkLayer_resize (settle) or
// VkLayer_unregister (teardown). All structural mutation and the visit
// iteration serialize here; the waits inside remain bounded (Rule 27).
static SpinLock s_layerLock = SPIN_LOCK_INIT;
static int s_count = 0;
static VkRenderPass s_layerPass = VK_NULL_HANDLE;
static VkCommandPool s_pool = VK_NULL_HANDLE;
static VkLayerRenderFn s_renderer = nullptr;

// Composite (collage) pipeline — one mutable descriptor set, updated per
// layer draw on the single present worker (no concurrent access).
static VkPipelineLayout s_compLayout = VK_NULL_HANDLE;
static VkPipeline s_compPipeline = VK_NULL_HANDLE;
static VkDescriptorSetLayout s_compDescLayout = VK_NULL_HANDLE;
static VkDescriptorPool s_compDescPool = VK_NULL_HANDLE;
static VkDescriptorSet s_compDescSet = VK_NULL_HANDLE;
static VkSampler s_compSampler = VK_NULL_HANDLE;

// // GETTERS

bool VkLayer_ready(void) {
    return s_instanceDevice != VK_NULL_HANDLE && s_count > 0;
}

int VkLayer_count(void) {
    return s_count;
}

int VkLayer_find(void *owner) {
    if (!owner)
        return -1;
    for (int i = 0; i < s_count; i++) {
        if (s_chains[i].active && s_chains[i].owner == owner)
            return i;
    }
    return -1;
}

uint64_t VkLayer_presentCount(int index) {
    if (index < 0 || index >= s_count)
        return 0;
    return s_chains[index].presentCount;
}

uint64_t VkLayer_skipCount(int index) {
    if (index < 0 || index >= s_count)
        return 0;
    return s_chains[index].skipCount;
}

bool VkLayer_isDirty(int index) {
    if (index < 0 || index >= s_count)
        return false;
    VkLayerChain *chain = &s_chains[index];
    if (!(*chain).active)
        return false;
    return (*chain).dirty;
}

// Rule 39 flight probe: true only when every layer's last submit fence is
// signaled — no offscreen CB that sampled bindless descriptors is still
// executing. Non-blocking: GetFenceStatus poll only, under a try of the
// registry lock. Fences start SIGNALED, so a never-submitted layer is idle.
bool VkLayer_flightIdle(void) {
    if (!VkLayer_ready())
        return true;
    if (!SpinLock_tryLock(&s_layerLock))
        return false;
    VK_LAYER_LOAD_DEVICE(GetFenceStatus)
    bool idle = true;
    for (int i = 0; i < s_count && idle; i++) {
        VkLayerChain *chain = &s_chains[i];
        if (!(*chain).active)
            continue;
        for (uint32_t s = 0; s < VK_LAYER_FLIGHT && idle; s++) {
            if ((*chain).fence[s] == VK_NULL_HANDLE)
                continue;
            if (GetFenceStatus_fn && GetFenceStatus_fn(s_instanceDevice, (*chain).fence[s]) != VK_SUCCESS)
                idle = false;
        }
    }
    SpinLock_unlock(&s_layerLock);
    return idle;
}

// // SETTERS

void VkLayer_setRenderer(VkLayerRenderFn fn) {
    s_renderer = fn;
}

// Setters/getters for the per-layer repaint-demand bit (Rule 24 symmetric
// pair; selector first, value last per Rule 9). Lock-free single-byte stores
// matching the presentCount/skipCount diagnostic pattern: the worker reads
// and clears under the registry lock while the bridge sets from outside —
// a missed mark only delays one render to the next tick (drop-degrade).
void VkLayer_markDirty(int index, bool dirty) {
    if (index < 0 || index >= s_count)
        return;
    VkLayerChain *chain = &s_chains[index];
    if (!(*chain).active)
        return;
    (*chain).dirty = dirty;
}

// // CORE FUNCTIONS (shader loading)

// Bundle-aware spv lookup shared by the composite pipeline build: the CMake
// staging dir, adjacent spv, then cwd relative — the same resolution the
// other graphvex shader loaders honor (Rule 21).
static unsigned char *layerLoadSpv(const char *path, size_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return nullptr;
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return nullptr;
    }
    rewind(f);
    unsigned char *bytes = (unsigned char*) malloc((size_t) size);
    if (!bytes) {
        fclose(f);
        return nullptr;
    }
    size_t got = fread(bytes, 1, (size_t) size, f);
    fclose(f);
    if (got != (size_t) size) {
        free(bytes);
        return nullptr;
    }
    *outSize = (size_t) size;
    return bytes;
}

static unsigned char *layerLoadSpvAny(const char *name, size_t *outSize) {
    char path[1024];
    unsigned char *code = nullptr;
#ifdef ANTI_SPV_DIR
    snprintf(path, sizeof(path), "%s/%s", ANTI_SPV_DIR, name);
    code = layerLoadSpv(path, outSize);
    if (code)
        return code;
#endif
#ifdef VEX_SPV_DIR
    snprintf(path, sizeof(path), "%s/%s", VEX_SPV_DIR, name);
    code = layerLoadSpv(path, outSize);
    if (code)
        return code;
#endif
    uint32_t exeSize = sizeof(path);
    if (_NSGetExecutablePath(path, &exeSize) == 0) {
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = 0;
            char candidate[1024];
            snprintf(candidate, sizeof(candidate), "%s/../Resources/spv/%s", path, name);
            code = layerLoadSpv(candidate, outSize);
            if (code)
                return code;
            snprintf(candidate, sizeof(candidate), "%s/spv/%s", path, name);
            code = layerLoadSpv(candidate, outSize);
            if (code)
                return code;
        }
    }
    snprintf(path, sizeof(path), "spv/%s", name);
    code = layerLoadSpv(path, outSize);
    if (code)
        return code;
    snprintf(path, sizeof(path), "src/vulkan/spv/%s", name);
    return layerLoadSpv(path, outSize);
}

static VkShaderModule layerLoadModule(const char *name) {
    VK_LAYER_LOAD_DEVICE(CreateShaderModule)
    size_t size = 0;
    unsigned char *code = layerLoadSpvAny(name, &size);
    if (!code)
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = (const uint32_t*) code;
    VkShaderModule mod = VK_NULL_HANDLE;
    if (CreateShaderModule_fn(s_instanceDevice, &ci, nullptr, &mod) != VK_SUCCESS)
        mod = VK_NULL_HANDLE;
    free(code);
    return mod;
}

// // CONSTRUCTORS

// Offscreen layer render pass: BGRA8, cleared on BEGIN (a layer owns its
// whole extent), stored to SHADER_READ_ONLY so the composite pass can sample
// the published flight image.
static bool ensureLayerPass(void) {
    if (s_layerPass != VK_NULL_HANDLE)
        return true;
    if (s_instanceGdpa == nullptr)
        return false;

    VK_LAYER_LOAD_DEVICE(CreateRenderPass)
    VK_LAYER_LOAD_DEVICE(DestroyRenderPass)

    VkAttachmentDescription att = {0};
    att.format = VK_LAYER_FORMAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

    if (CreateRenderPass_fn(s_instanceDevice, &rpci, nullptr, &s_layerPass) != VK_SUCCESS)
        return false;
    return true;
}

static uint32_t layerMemoryType(uint32_t typeBits) {
    PFN_vkGetPhysicalDeviceMemoryProperties memPropsFn =
        (PFN_vkGetPhysicalDeviceMemoryProperties) s_instanceGpa(s_instanceInstance,
                                                                "vkGetPhysicalDeviceMemoryProperties");
    if (!memPropsFn)
        return UINT32_MAX;
    VkPhysicalDeviceMemoryProperties props;
    memPropsFn(s_instancePhys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return i;
    }
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (typeBits & (1u << i))
            return i;
    }
    return UINT32_MAX;
}

// Per-layer offscreen flight targets: two color images (sampled+attachment),
// their views, and framebuffers on the dedicated layer pass.
static bool buildLayerTargets(VkLayerChain *chain) {
    VK_LAYER_LOAD_DEVICE(CreateImage)
    VK_LAYER_LOAD_DEVICE(GetImageMemoryRequirements)
    VK_LAYER_LOAD_DEVICE(AllocateMemory)
    VK_LAYER_LOAD_DEVICE(BindImageMemory)
    VK_LAYER_LOAD_DEVICE(CreateImageView)
    VK_LAYER_LOAD_DEVICE(CreateFramebuffer)

    uint32_t w = (*chain).extent.width;
    uint32_t h = (*chain).extent.height;
    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = (VkExtent3D){ .width = w, .height = h, .depth = 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = VK_LAYER_FORMAT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        if (CreateImage_fn(s_instanceDevice, &ici, nullptr, &(*chain).images[s]) != VK_SUCCESS)
            return false;

        VkMemoryRequirements mr;
        GetImageMemoryRequirements_fn(s_instanceDevice, (*chain).images[s], &mr);
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = layerMemoryType(mr.memoryTypeBits);
        if (ai.memoryTypeIndex == UINT32_MAX
            || AllocateMemory_fn(s_instanceDevice, &ai, nullptr, &(*chain).mem[s]) != VK_SUCCESS)
            return false;
        BindImageMemory_fn(s_instanceDevice, (*chain).images[s], (*chain).mem[s], 0);

        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = (*chain).images[s];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_LAYER_FORMAT;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        if (CreateImageView_fn(s_instanceDevice, &vci, nullptr, &(*chain).views[s]) != VK_SUCCESS)
            return false;

        VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = s_layerPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &(*chain).views[s];
        fci.width = w;
        fci.height = h;
        fci.layers = 1;
        if (CreateFramebuffer_fn(s_instanceDevice, &fci, nullptr, &(*chain).fbs[s]) != VK_SUCCESS)
            return false;
    }
    return true;
}

// Tear down a layer's offscreen targets (images, views, framebuffers, memory).
static void destroyLayerTargets(VkLayerChain *chain) {
    if (!chain)
        return;
    if (s_instanceDevice == VK_NULL_HANDLE)
        return;

    VK_LAYER_LOAD_DEVICE(DestroyFramebuffer)
    VK_LAYER_LOAD_DEVICE(DestroyImageView)
    VK_LAYER_LOAD_DEVICE(DestroyImage)
    VK_LAYER_LOAD_DEVICE(FreeMemory)

    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        if ((*chain).fbs[s] != VK_NULL_HANDLE) {
            DestroyFramebuffer_fn(s_instanceDevice, (*chain).fbs[s], nullptr);
            (*chain).fbs[s] = VK_NULL_HANDLE;
        }
        if ((*chain).views[s] != VK_NULL_HANDLE) {
            DestroyImageView_fn(s_instanceDevice, (*chain).views[s], nullptr);
            (*chain).views[s] = VK_NULL_HANDLE;
        }
        if ((*chain).mem[s] != VK_NULL_HANDLE) {
            FreeMemory_fn(s_instanceDevice, (*chain).mem[s], nullptr);
            (*chain).mem[s] = VK_NULL_HANDLE;
        }
        if ((*chain).images[s] != VK_NULL_HANDLE) {
            DestroyImage_fn(s_instanceDevice, (*chain).images[s], nullptr);
            (*chain).images[s] = VK_NULL_HANDLE;
        }
    }
}

// Tear down a chain whose registration failed partway (count not yet
// incremented — unregister() can't be used with an out-of-range index).
static void abortLayerRegistration(VkLayerChain *chain) {
    destroyLayerTargets(chain);
    VK_LAYER_LOAD_DEVICE(DestroyFence)
    VK_LAYER_LOAD_DEVICE(FreeCommandBuffers)
    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            DestroyFence_fn(s_instanceDevice, (*chain).fence[s], nullptr);
    }
    if (s_pool != VK_NULL_HANDLE)
        FreeCommandBuffers_fn(s_instanceDevice, s_pool, VK_LAYER_FLIGHT, (*chain).cb);
    memset(chain, 0, sizeof(*chain));
}

// Composite (collage) pipeline: samples one published layer image, in a
// render pass, at a canvas rect. Vertex reuses texture_quad_vert.spv (its
// push is the same NDC rect); the fragment (layer_quad_frag.spv) samples a
// single bounded sampler2D and applies the tint. Built against the BGRA8
// layer pass; Vulkan render-pass compatibility (same format + single color
// attachment) makes it bind in every board/pane BGRA8 pass too.
static bool ensureCompositePipeline(void) {
    if (s_instanceGdpa == nullptr || s_layerPass == VK_NULL_HANDLE)
        return false;

    // Build is idempotent under the registry lock: the warm-up presents (thread
    // 0) and the present worker can race the first composite at startup, and
    // two concurrent builds would orphan one whole pipeline + descriptor set.
    SpinLock_lock(&s_layerLock);
    if (s_compPipeline != VK_NULL_HANDLE) {
        SpinLock_unlock(&s_layerLock);
        return true;
    }

    VK_LAYER_LOAD_DEVICE(CreateDescriptorSetLayout)
    VK_LAYER_LOAD_DEVICE(CreateDescriptorPool)
    VK_LAYER_LOAD_DEVICE(AllocateDescriptorSets)
    VK_LAYER_LOAD_DEVICE(CreateSampler)
    VK_LAYER_LOAD_DEVICE(CreatePipelineLayout)
    VK_LAYER_LOAD_DEVICE(CreateGraphicsPipelines)

    VkDescriptorSetLayoutBinding binding = {0};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dlci.bindingCount = 1;
    dlci.pBindings = &binding;
    if (CreateDescriptorSetLayout_fn(s_instanceDevice, &dlci, nullptr, &s_compDescLayout) != VK_SUCCESS) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkDescriptorPoolSize psize = {0};
    psize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    psize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psize;
    if (CreateDescriptorPool_fn(s_instanceDevice, &dpci, nullptr, &s_compDescPool) != VK_SUCCESS) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = s_compDescPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &s_compDescLayout;
    if (AllocateDescriptorSets_fn(s_instanceDevice, &dsai, &s_compDescSet) != VK_SUCCESS) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.0f;
    if (CreateSampler_fn(s_instanceDevice, &sci, nullptr, &s_compSampler) != VK_SUCCESS) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    // Vertex: offset=0 size=16 (rectNdc). Fragment: offset=16 size=16 (tint).
    VkPushConstantRange compPush[2] = {{0}, {0}};
    compPush[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    compPush[0].offset = 0;
    compPush[0].size = 16;
    compPush[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compPush[1].offset = 16;
    compPush[1].size = 16;

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &s_compDescLayout;
    plci.pushConstantRangeCount = 2;
    plci.pPushConstantRanges = compPush;
    if (CreatePipelineLayout_fn(s_instanceDevice, &plci, nullptr, &s_compLayout) != VK_SUCCESS) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkShaderModule compVert = layerLoadModule("texture_quad_vert.spv");
    VkShaderModule compFrag = layerLoadModule("layer_quad_frag.spv");
    if (compVert == VK_NULL_HANDLE || compFrag == VK_NULL_HANDLE) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAtt = {0};
    blendAtt.colorWriteMask = 0xF;
    blendAtt.blendEnable = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb2 = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb2.attachmentCount = 1;
    cb2.pAttachments = &blendAtt;
    VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    VkPipelineShaderStageCreateInfo compStages[2] = {{0}, {0}};
    compStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    compStages[0].module = compVert;
    compStages[0].pName = "main";
    compStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    compStages[1].module = compFrag;
    compStages[1].pName = "main";

    VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount = 2;
    gpci.pStages = compStages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb2;
    gpci.pDynamicState = &ds;
    gpci.layout = s_compLayout;
    gpci.renderPass = s_layerPass;  // BGRA8 — compatible with every BGRA8 board/pane pass
    gpci.subpass = 0;

    if (CreateGraphicsPipelines_fn(s_instanceDevice, VK_NULL_HANDLE, 1, &gpci, nullptr, &s_compPipeline) != VK_SUCCESS) {
        s_compPipeline = VK_NULL_HANDLE;
        SpinLock_unlock(&s_layerLock);
        return false;
    }
    SpinLock_unlock(&s_layerLock);
    printf("vk: layer composite pipeline built\n");
    return true;
}

int VkLayer_register(int width, int height, void *owner) {
    if (width <= 0 || height <= 0 || !owner)
        return -1;
    if (!s_instanceDevice || !s_instanceInstance)
        return -1;

    SpinLock_lock(&s_layerLock);

    if (s_count >= VK_LAYER_CHAIN_MAX) {
        SpinLock_unlock(&s_layerLock);
        return -1;
    }

    VK_LAYER_LOAD_DEVICE(CreateCommandPool)
    VK_LAYER_LOAD_DEVICE(AllocateCommandBuffers)
    VK_LAYER_LOAD_DEVICE(CreateFence)

    if (s_pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo cpci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = s_instanceQueueFamily,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        };
        if (CreateCommandPool_fn(s_instanceDevice, &cpci, nullptr, &s_pool) != VK_SUCCESS) {
            SpinLock_unlock(&s_layerLock);
            return -1;
        }
    }

    if (!ensureLayerPass()) {
        SpinLock_unlock(&s_layerLock);
        return -1;
    }

    // Reuse the first free slot (stable indices — the compositor maps
    // layerIndex -> Panel and must never see a slot move).
    int idx = -1;
    for (int i = 0; i < s_count; i++) {
        if (!s_chains[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_count >= VK_LAYER_CHAIN_MAX) {
            SpinLock_unlock(&s_layerLock);
            return -1;
        }
        idx = s_count;
        s_count++;
    }

    VkLayerChain *chain = &s_chains[idx];
    memset(chain, 0, sizeof(*chain));
    (*chain).active = true;
    (*chain).owner = owner;
    // Layer pixel size is fixed NOW (register args) — the registered ground
    // truth the offscreen targets are built at (Rule 11.5).
    (*chain).extent = (VkExtent2D){ .width = (uint32_t) width, .height = (uint32_t) height };
    (*chain).fenceTimeoutNs = 100000000ULL;
    (*chain).dirty = true;      // registration demands the first render
    (*chain).published = -1;    // no frame to composite yet

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VK_LAYER_FLIGHT,
    };
    if (AllocateCommandBuffers_fn(s_instanceDevice, &cbai, (*chain).cb) != VK_SUCCESS) {
        abortLayerRegistration(chain);
        SpinLock_unlock(&s_layerLock);
        return -1;
    }

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        if (CreateFence_fn(s_instanceDevice, &fci, nullptr, &(*chain).fence[s]) != VK_SUCCESS) {
            abortLayerRegistration(chain);
            SpinLock_unlock(&s_layerLock);
            return -1;
        }
    }

    if (!buildLayerTargets(chain)) {
        (*chain).active = false;
        abortLayerRegistration(chain);
        SpinLock_unlock(&s_layerLock);
        return -1;
    }

    fprintf(stderr, "vk: layer %d live %dx%d (target %d)\n", idx, width, height, idx);
    SpinLock_unlock(&s_layerLock);
    return idx;
}

bool VkLayer_unregister(int index) {
    SpinLock_lock(&s_layerLock);

    if (index < 0 || index >= s_count) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkLayerChain *chain = &s_chains[index];
    if (!(*chain).active) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    // Bounded wait for in-flight renders before teardown (Rule 27): both
    // flight slots drain, so neither flight image can be retired under.
    VK_LAYER_LOAD_DEVICE(WaitForFences)
    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            WaitForFences_fn(s_instanceDevice, 1, &(*chain).fence[s], VK_TRUE, (*chain).fenceTimeoutNs);
    }
    destroyLayerTargets(chain);

    VK_LAYER_LOAD_DEVICE(DestroyFence)
    VK_LAYER_LOAD_DEVICE(FreeCommandBuffers)
    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            DestroyFence_fn(s_instanceDevice, (*chain).fence[s], nullptr);
    }
    if (s_pool != VK_NULL_HANDLE)
        FreeCommandBuffers_fn(s_instanceDevice, s_pool, VK_LAYER_FLIGHT, (*chain).cb);

    // Slot stays reserved (stable indices for the compositor), just inert.
    (*chain).active = false;
    (*chain).published = -1;
    SpinLock_unlock(&s_layerLock);
    return true;
}

bool VkLayer_resize(int index, int width, int height) {
    SpinLock_lock(&s_layerLock);

    if (index < 0 || index >= s_count || width <= 0 || height <= 0) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }

    VkLayerChain *chain = &s_chains[index];
    if (!(*chain).active) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }
    if ((*chain).extent.width == (uint32_t) width && (*chain).extent.height == (uint32_t) height) {
        SpinLock_unlock(&s_layerLock);
        return true; // fixed layer: no rebuild on window resize (Rule 11.5)
    }

    // Bound the wait on in-flight renders (both flight slots) before
    // tearing down the targets.
    VK_LAYER_LOAD_DEVICE(WaitForFences)
    for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
        if ((*chain).fence[s] != VK_NULL_HANDLE)
            WaitForFences_fn(s_instanceDevice, 1, &(*chain).fence[s], VK_TRUE, (*chain).fenceTimeoutNs);
    }
    destroyLayerTargets(chain);
    (*chain).extent = (VkExtent2D){ .width = (uint32_t) width, .height = (uint32_t) height };
    bool ok = buildLayerTargets(chain);
    if (ok) {
        (*chain).dirty = true;    // rebuilt targets demand a re-render
        (*chain).published = -1;  // old published image is gone
    }
    SpinLock_unlock(&s_layerLock);
    return ok;
}

bool VkLayer_visit(void) {
    // Rule 39 seam guard: layers submit to the shared queue every frame; a
    // dead/nulled device must never be handed layer work. Debug net only.
    if (!VkGuard_check("VkLayer_visit", Vk_getDevice(), Vk_getQueue(), Vk_isDeviceLost()))
        return false;
    if (!s_renderer || s_count == 0 || !s_instanceDevice)
        return false;

    SpinLock_lock(&s_layerLock);
    if (!s_renderer || s_count == 0 || !s_instanceDevice) {
        SpinLock_unlock(&s_layerLock);
        return false;
    }
    VK_LAYER_LOAD_DEVICE(ResetCommandBuffer)
    VK_LAYER_LOAD_DEVICE(BeginCommandBuffer)
    VK_LAYER_LOAD_DEVICE(EndCommandBuffer)
    VK_LAYER_LOAD_DEVICE(CmdBeginRenderPass)
    VK_LAYER_LOAD_DEVICE(CmdEndRenderPass)
    VK_LAYER_LOAD_DEVICE(QueueSubmit)
    VK_LAYER_LOAD_DEVICE(GetFenceStatus)
    VK_LAYER_LOAD_DEVICE(ResetFences)
    VK_LAYER_LOAD_DEVICE(CreateFence)
    VK_LAYER_LOAD_DEVICE(DestroyFence)

    bool rendered = false;
    for (int i = 0; i < s_count; i++) {
        VkLayerChain *chain = &s_chains[i];
        if (!(*chain).active)
            continue;

        // Flip/flop: record into the free slot while the other still flies.
        // A scene stalled on its own late submit no longer parks its next
        // record — each scene renders on its own buffering.
        uint32_t slot = (*chain).flip & 1u;
        VkCommandBuffer cb = (*chain).cb[slot];
        VkFence fence = (*chain).fence[slot];
        if (cb == VK_NULL_HANDLE || fence == VK_NULL_HANDLE)
            continue;

        // Stale-frame reuse: when this slot's last submit still flies, skip
        // the layer and keep its last published image on the canvas — never
        // block the walk. Non-blocking poll only: drop-degrade per Rule 27,
        // zero logging per Rule 35 hot-minimal.
        if (GetFenceStatus_fn(s_instanceDevice, fence) != VK_SUCCESS) {
            (*chain).skipCount++;
            continue;
        }

        // Clean-layer skip: no repaint demand since the last successful
        // render — keep the stale published image, never re-record. Scenes
        // regain demand every tick through the bridge (tree-dirty
        // propagation); static layers rest here until their tree dirties.
        if (!(*chain).dirty) {
            (*chain).skipCount++;
            continue;
        }

        ResetCommandBuffer_fn(cb, 0);
        VkCommandBufferBeginInfo bbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        if (BeginCommandBuffer_fn(cb, &bbi) != VK_SUCCESS)
            continue;

        VkClearValue clear = {0};
        VkRenderPassBeginInfo rpbi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = s_layerPass,
            .framebuffer = (*chain).fbs[slot],
            .renderArea.extent = (*chain).extent,
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        CmdBeginRenderPass_fn(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        s_renderer(cb, (int)(*chain).extent.width, (int)(*chain).extent.height, (*chain).owner);
        CmdEndRenderPass_fn(cb);
        if (EndCommandBuffer_fn(cb) != VK_SUCCESS)
            continue;

        ResetFences_fn(s_instanceDevice, 1, &fence);
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        if (QueueSubmit_fn(s_instanceQueue, 1, &si, fence) != VK_SUCCESS) {
            // Failed submits queue nothing: the just-reset fence would never
            // signal again, wedging this layer on its stale frame forever.
            // Recreate it signaled so the next tick retries (same refence
            // contract the pane walk honors).
            if (CreateFence_fn && DestroyFence_fn) {
                VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                VkFence fresh = VK_NULL_HANDLE;
                if (CreateFence_fn(s_instanceDevice, &fci, nullptr, &fresh) == VK_SUCCESS) {
                    DestroyFence_fn(s_instanceDevice, fence, nullptr);
                    (*chain).fence[slot] = fresh;
                }
            }
            continue;
        }

        // Published: the composite walk later in this same queue pass samples
        // this image — same-queue FIFO guarantees the render finished first.
        (*chain).published = (int) slot;
        (*chain).flip ^= 1u;
        (*chain).presentCount++;
        (*chain).dirty = false; // demand satisfied; bridge re-arms next tick
        rendered = true;
    }
    SpinLock_unlock(&s_layerLock);
    return rendered;
}

bool VkLayer_composite(void *cmdBuffer, float surfaceW, float surfaceH,
                       int index, float x, float y, float w, float h,
                       float r, float g, float b, float a) {
    if (!cmdBuffer || surfaceW <= 0.0f || surfaceH <= 0.0f)
        return false;
    if (index < 0 || index >= s_count)
        return false;
    VkLayerChain *chain = &s_chains[index];
    if (!(*chain).active || (*chain).published < 0)
        return false;
    if (w <= 0.0f || h <= 0.0f)
        return false;

    VkImageView view = (*chain).views[(*chain).published];
    if (view == VK_NULL_HANDLE)
        return false;

    if (s_instanceDevice == VK_NULL_HANDLE)
        return false;
    if (s_compPipeline == VK_NULL_HANDLE && !ensureCompositePipeline())
        return false;

    VK_LAYER_LOAD_DEVICE(CmdBindPipeline)
    VK_LAYER_LOAD_DEVICE(CmdBindDescriptorSets)
    VK_LAYER_LOAD_DEVICE(CmdPushConstants)
    VK_LAYER_LOAD_DEVICE(CmdDraw)
    VK_LAYER_LOAD_DEVICE(CmdSetViewport)
    VK_LAYER_LOAD_DEVICE(CmdSetScissor)
    VK_LAYER_LOAD_DEVICE(UpdateDescriptorSets)

    VkCommandBuffer cb = (VkCommandBuffer) cmdBuffer;

    // Point the single composite descriptor set at this layer's published
    // image. Single present worker – no concurrent access, no per-layer sets.
    VkDescriptorImageInfo dii = {
        .sampler = s_compSampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet wds = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wds.dstSet = s_compDescSet;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    UpdateDescriptorSets_fn(s_instanceDevice, 1, &wds, 0, nullptr);

    CmdBindPipeline_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_compPipeline);
    CmdBindDescriptorSets_fn(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_compLayout,
                             0, 1, &s_compDescSet, 0, nullptr);

    float scLeft = x < 0.0f ? 0.0f : x;
    float scRight = (x + w > surfaceW) ? surfaceW : (x + w);
    float scBottom = y < 0.0f ? 0.0f : y;
    float scTop = (y + h > surfaceH) ? surfaceH : (y + h);
    if (scRight <= scLeft || scTop <= scBottom)
        return false;

    VkViewport viewport = { .x = 0.0f, .y = surfaceH, .width = surfaceW, .height = -surfaceH, .maxDepth = 1.0f };
    VkRect2D scissor = {
        .offset.x = (int32_t) scLeft,
        .offset.y = (int32_t) (surfaceH - scTop),
        .extent.width = (uint32_t) (scRight - scLeft),
        .extent.height = (uint32_t) (scTop - scBottom),
    };
    CmdSetViewport_fn(cb, 0, 1, &viewport);
    CmdSetScissor_fn(cb, 0, 1, &scissor);

    struct {
        float rect[4];       // offset 0  (vertex NDC rect)
        float color[4];      // offset 16 (fragment tint)
    } push;
    push.rect[0] = (x / surfaceW) * 2.0f - 1.0f;
    push.rect[1] = (y / surfaceH) * 2.0f - 1.0f;
    push.rect[2] = (w / surfaceW) * 2.0f;
    push.rect[3] = (h / surfaceH) * 2.0f;
    push.color[0] = r;
    push.color[1] = g;
    push.color[2] = b;
    push.color[3] = a;

    CmdPushConstants_fn(cb, s_compLayout, VK_SHADER_STAGE_VERTEX_BIT,   0,  16, push.rect);
    CmdPushConstants_fn(cb, s_compLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 16, 16, push.color);
    CmdDraw_fn(cb, 6, 1, 0, 0);
    return true;
}

void VkLayer_shutdown(void) {
    if (!s_instanceDevice)
        return;

    PFN_vkDeviceWaitIdle devWait = (PFN_vkDeviceWaitIdle) s_instanceGdpa(s_instanceDevice, "vkDeviceWaitIdle");
    if (devWait)
        devWait(s_instanceDevice);

    VK_LAYER_LOAD_DEVICE(DestroyFence)
    VK_LAYER_LOAD_DEVICE(FreeCommandBuffers)

    for (int i = 0; i < s_count; i++) {
        VkLayerChain *chain = &s_chains[i];
        destroyLayerTargets(chain);
        for (uint32_t s = 0; s < VK_LAYER_FLIGHT; s++) {
            if ((*chain).fence[s] != VK_NULL_HANDLE)
                DestroyFence_fn(s_instanceDevice, (*chain).fence[s], nullptr);
        }
        if (s_pool != VK_NULL_HANDLE)
            FreeCommandBuffers_fn(s_instanceDevice, s_pool, VK_LAYER_FLIGHT, (*chain).cb);
        memset(chain, 0, sizeof(VkLayerChain));
    }
    s_count = 0;

    if (s_pool != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyCommandPool)
        DestroyCommandPool_fn(s_instanceDevice, s_pool, nullptr);
        s_pool = VK_NULL_HANDLE;
    }
    if (s_layerPass != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyRenderPass)
        DestroyRenderPass_fn(s_instanceDevice, s_layerPass, nullptr);
        s_layerPass = VK_NULL_HANDLE;
    }
    if (s_compPipeline != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyPipeline)
        DestroyPipeline_fn(s_instanceDevice, s_compPipeline, nullptr);
        s_compPipeline = VK_NULL_HANDLE;
    }
    if (s_compLayout != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyPipelineLayout)
        DestroyPipelineLayout_fn(s_instanceDevice, s_compLayout, nullptr);
        s_compLayout = VK_NULL_HANDLE;
    }
    if (s_compDescPool != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyDescriptorPool)
        DestroyDescriptorPool_fn(s_instanceDevice, s_compDescPool, nullptr);
        s_compDescPool = VK_NULL_HANDLE;
    }
    if (s_compDescLayout != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroyDescriptorSetLayout)
        DestroyDescriptorSetLayout_fn(s_instanceDevice, s_compDescLayout, nullptr);
        s_compDescLayout = VK_NULL_HANDLE;
    }
    if (s_compSampler != VK_NULL_HANDLE) {
        VK_LAYER_LOAD_DEVICE(DestroySampler)
        DestroySampler_fn(s_instanceDevice, s_compSampler, nullptr);
        s_compSampler = VK_NULL_HANDLE;
    }
    s_renderer = nullptr;
}