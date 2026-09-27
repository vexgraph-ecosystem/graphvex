#include "vulkan/vk_swapchain.h"

#include <limits.h>
#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * VkSwapchain owns only a windowed Device's WSI swapchain, its image-handle
 * array, and one reusable GPU presentation slot. The Device owns the surface
 * and logical device. A successful acquire is proved by its fence, a blit
 * submits with a binary render-done semaphore, and present uses both a
 * maintenance1 fence and present ID. Both completion proofs are bounded.
 * Unproved ownership is retained; the borrowed Device must remain alive.
 */
;;OVERVIEW
/**
 * CLASS: VkSwapchain (backend-private, L3)
 * STRUCT FIELDS (mirrors vulkan/vk_swapchain.h):
 *   const Device *owner;                  // borrowed device
 *   VkDevice native;                      // borrowed logical device
 *   VkSwapchainKHR handle;                // owned swapchain
 *   VkFence acquireFence;                 // owned fence-only acquisition sync
 *   VkFence submitFence, presentFence;    // owned submit and present proofs
 *   VkSemaphore renderDone;               // owned binary submit/present edge
 *   VkCommandPool pool; VkCommandBuffer cmd; // owned reusable blit recording
 *   VkQueue queue; uint64_t presentId;    // borrowed queue, monotonic ID
 *   bool submitted, presented, presentPending; // slot retirement phases
 *   VkImage *images;                      // cold allocated borrowed image handles
 *   uint32_t count;                       // image handle count
 *   uint32_t index;                       // sole acquired image index
 *   VkFormat format;                      // negotiated surface format
 *   VkExtent2D extent;                    // native pixel extent
 *   bool acquired;                        // image loan outstanding
 *   bool acquirePending;                  // acquisition fence still outstanding
 *   bool fencePending;                    // fence may have been submitted
 *   bool poisoned;                        // unknown driver ownership; never free
 *   PFN_vkDestroySwapchainKHR destroySwapchain; // device entry point
 *   PFN_vkDestroyFence destroyFence;      // device entry point
 *   PFN_vkWaitForFences waitFences;       // device entry point
 *   PFN_vkResetFences resetFences;        // device entry point
 *   PFN_vkAcquireNextImageKHR acquireNextImage; // device entry point
 *   PFN_vkReleaseSwapchainImagesEXT releaseImages; // maintenance1 entry point
 *   PFN_vkQueueSubmit queueSubmit; PFN_vkQueuePresentKHR queuePresent;
 *   PFN_vkCreateCommandPool createPool; PFN_vkDestroyCommandPool destroyPool;
 *   PFN_vkAllocateCommandBuffers allocateCommands;
 *   PFN_vkResetCommandBuffer resetCommand; PFN_vkBeginCommandBuffer beginCommand;
 *   PFN_vkEndCommandBuffer endCommand; PFN_vkCmdPipelineBarrier barrier;
 *   PFN_vkCmdBlitImage blit; PFN_vkCreateSemaphore createSemaphore;
 *   PFN_vkDestroySemaphore destroySemaphore;
 * FUNCTION REGISTRY:
 *   Public Constructors: VkSwapchain_new
 *   Private Constructors: swapchainBuild
 *   Public Core Functions: VkSwapchain_resize, VkSwapchain_acquire,
 *       VkSwapchain_release, VkSwapchain_present, VkSwapchain_destroy
 *   Private Core Functions: swapchainDrain, swapchainRetire
 *   Public Setters: none (lifecycle verbs own state)
 *   Private Setters: none
 *   Public Getters: VkSwapchain_getExtent, VkSwapchain_getImageCount,
 *       VkSwapchain_isAcquired
 *   Private Getters: none
 */

#define SWAPCHAIN_WAIT_NS 100000000ull

// CONSTRUCTORS (PUBLIC & PRIVATE)
static bool swapchainBuild(VkSwapchain *self, uint32_t width, uint32_t height,
                           VkSwapchainKHR *destHandle, VkImage **destImages,
                           uint32_t *destCount, VkFormat *destFormat, VkExtent2D *destExtent) {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice native = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    PFN_vkGetInstanceProcAddr gpa = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    const Device *owner = (*self).owner;
    if (!VkDevice_canSafelyPresent(owner) || !VkDevice_borrow(owner, &physical, &native,
            &queue, &family, &gpa, &instance) || native != (*self).native)
        return false;
    VkSurfaceKHR surface = VkDevice_borrowSurface(owner);
    if (surface == VK_NULL_HANDLE || !width || !height)
        return false;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getCaps =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) gpa(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getFormats =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR) gpa(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    PFN_vkGetDeviceProcAddr getProc =
        (PFN_vkGetDeviceProcAddr) gpa(instance, "vkGetDeviceProcAddr");
    if (!getCaps || !getFormats || !getProc)
        return false;
    PFN_vkCreateSwapchainKHR create = (PFN_vkCreateSwapchainKHR) getProc(native, "vkCreateSwapchainKHR");
    PFN_vkGetSwapchainImagesKHR getImages = (PFN_vkGetSwapchainImagesKHR) getProc(native, "vkGetSwapchainImagesKHR");
    if (!create || !getImages)
        return false;
    VkSurfaceCapabilitiesKHR caps;
    uint32_t formatCount = 0;
    if (getCaps(physical, surface, &caps) != VK_SUCCESS ||
        getFormats(physical, surface, &formatCount, nullptr) != VK_SUCCESS || !formatCount ||
        !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return false;
    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR*) malloc((size_t) formatCount * sizeof(*formats));
    if (!formats)
        return false;
    VkResult result = getFormats(physical, surface, &formatCount, formats);
    if (result != VK_SUCCESS || !formatCount) {
        free(formats);
        return false;
    }
    VkSurfaceFormatKHR selected = { VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    for (uint32_t i = 0; i < formatCount; i++) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_UNDEFINED) &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selected = formats[i];
            selected.format = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        }
    }
    free(formats);
    PFN_vkGetPhysicalDeviceFormatProperties getProperties =
        (PFN_vkGetPhysicalDeviceFormatProperties) gpa(instance, "vkGetPhysicalDeviceFormatProperties");
    VkFormatProperties srcProperties = {0};
    VkFormatProperties dstProperties = {0};
    if (!getProperties || selected.format == VK_FORMAT_UNDEFINED)
        return false;
    getProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &srcProperties);
    getProperties(physical, selected.format, &dstProperties);
    if (!(srcProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(dstProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        return false;
    VkExtent2D extent = { width, height };
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    if (!extent.width || !extent.height || extent.width > INT32_MAX || extent.height > INT32_MAX ||
        !caps.supportedCompositeAlpha || !caps.minImageCount)
        return false;
    uint32_t count = caps.minImageCount;
    if (count < UINT32_MAX && (!caps.maxImageCount || count < caps.maxImageCount))
        count++;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        uint32_t bit = caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1u);
        alpha = (VkCompositeAlphaFlagBitsKHR) bit;
    }
    VkSwapchainCreateInfoKHR info = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = surface;
    info.minImageCount = count;
    info.imageFormat = selected.format;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    if (create(native, &info, nullptr, &handle) != VK_SUCCESS || handle == VK_NULL_HANDLE)
        return false;
    count = 0;
    if (getImages(native, handle, &count, nullptr) != VK_SUCCESS || !count ||
        (size_t) count > SIZE_MAX / sizeof(VkImage)) {
        (*self).destroySwapchain(native, handle, nullptr);
        return false;
    }
    VkImage *images = (VkImage*) malloc((size_t) count * sizeof(*images));
    if (!images) {
        (*self).destroySwapchain(native, handle, nullptr);
        return false;
    }
    if (getImages(native, handle, &count, images) != VK_SUCCESS || !count) {
        free(images);
        (*self).destroySwapchain(native, handle, nullptr);
        return false;
    }
    *destHandle = handle;
    *destImages = images;
    *destCount = count;
    *destFormat = selected.format;
    *destExtent = extent;
    return true;
}

VkSwapchain *VkSwapchain_new(const Device *device, uint32_t width, uint32_t height) {
    if (!VkDevice_canSafelyPresent(device) || !width || !height)
        return nullptr;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice native = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    PFN_vkGetInstanceProcAddr gpa = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    if (!VkDevice_borrow(device, &physical, &native, &queue, &family, &gpa, &instance))
        return nullptr;
    PFN_vkGetDeviceProcAddr proc = (PFN_vkGetDeviceProcAddr) gpa(instance, "vkGetDeviceProcAddr");
    if (!proc)
        return nullptr;
    VkSwapchain *self = (VkSwapchain*) calloc(1, sizeof(*self));
    if (!self)
        return nullptr;
    (*self).owner = device;
    (*self).native = native;
    (*self).queue = queue;
    (*self).destroySwapchain = (PFN_vkDestroySwapchainKHR) proc(native, "vkDestroySwapchainKHR");
    (*self).destroyFence = (PFN_vkDestroyFence) proc(native, "vkDestroyFence");
    (*self).waitFences = (PFN_vkWaitForFences) proc(native, "vkWaitForFences");
    (*self).resetFences = (PFN_vkResetFences) proc(native, "vkResetFences");
    (*self).acquireNextImage = (PFN_vkAcquireNextImageKHR) proc(native, "vkAcquireNextImageKHR");
    (*self).releaseImages = VkDevice_borrowReleaseSwapchainImages(device);
    (*self).queueSubmit = (PFN_vkQueueSubmit) proc(native, "vkQueueSubmit");
    (*self).queuePresent = (PFN_vkQueuePresentKHR) proc(native, "vkQueuePresentKHR");
    (*self).createPool = (PFN_vkCreateCommandPool) proc(native, "vkCreateCommandPool");
    (*self).destroyPool = (PFN_vkDestroyCommandPool) proc(native, "vkDestroyCommandPool");
    (*self).allocateCommands = (PFN_vkAllocateCommandBuffers) proc(native, "vkAllocateCommandBuffers");
    (*self).resetCommand = (PFN_vkResetCommandBuffer) proc(native, "vkResetCommandBuffer");
    (*self).beginCommand = (PFN_vkBeginCommandBuffer) proc(native, "vkBeginCommandBuffer");
    (*self).endCommand = (PFN_vkEndCommandBuffer) proc(native, "vkEndCommandBuffer");
    (*self).barrier = (PFN_vkCmdPipelineBarrier) proc(native, "vkCmdPipelineBarrier");
    (*self).blit = (PFN_vkCmdBlitImage) proc(native, "vkCmdBlitImage");
    (*self).createSemaphore = (PFN_vkCreateSemaphore) proc(native, "vkCreateSemaphore");
    (*self).destroySemaphore = (PFN_vkDestroySemaphore) proc(native, "vkDestroySemaphore");
    PFN_vkCreateFence createFence = (PFN_vkCreateFence) proc(native, "vkCreateFence");
    if (!(*self).destroySwapchain || !(*self).destroyFence || !(*self).waitFences ||
        !(*self).resetFences || !(*self).acquireNextImage || !(*self).releaseImages || !createFence ||
        !(*self).queueSubmit || !(*self).queuePresent || !(*self).createPool || !(*self).destroyPool ||
        !(*self).allocateCommands || !(*self).resetCommand || !(*self).beginCommand ||
        !(*self).endCommand || !(*self).barrier || !(*self).blit ||
        !(*self).createSemaphore || !(*self).destroySemaphore)
        goto fail;
    VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (createFence(native, &fenceInfo, nullptr, &(*self).acquireFence) != VK_SUCCESS)
        goto fail;
    if (createFence(native, &fenceInfo, nullptr, &(*self).submitFence) != VK_SUCCESS ||
        createFence(native, &fenceInfo, nullptr, &(*self).presentFence) != VK_SUCCESS)
        goto fail;
    VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if ((*self).createSemaphore(native, &semaphoreInfo, nullptr, &(*self).renderDone) != VK_SUCCESS)
        goto fail;
    VkCommandPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family };
    if ((*self).createPool(native, &poolInfo, nullptr, &(*self).pool) != VK_SUCCESS)
        goto fail;
    VkCommandBufferAllocateInfo commandInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = (*self).pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    if ((*self).allocateCommands(native, &commandInfo, &(*self).cmd) != VK_SUCCESS)
        goto fail;
    if (!swapchainBuild(self, width, height, &(*self).handle, &(*self).images,
                        &(*self).count, &(*self).format, &(*self).extent))
        goto fail;
    return self;
fail:
    if ((*self).pool && (*self).destroyPool)
        (*self).destroyPool(native, (*self).pool, nullptr);
    if ((*self).renderDone && (*self).destroySemaphore)
        (*self).destroySemaphore(native, (*self).renderDone, nullptr);
    if ((*self).presentFence && (*self).destroyFence)
        (*self).destroyFence(native, (*self).presentFence, nullptr);
    if ((*self).submitFence && (*self).destroyFence)
        (*self).destroyFence(native, (*self).submitFence, nullptr);
    if ((*self).acquireFence != VK_NULL_HANDLE && (*self).destroyFence)
        (*self).destroyFence(native, (*self).acquireFence, nullptr);
    free(self);
    return nullptr;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)
static bool swapchainDrain(VkSwapchain *self) {
    if ((*self).poisoned)
        return false;
    if (!(*self).fencePending)
        return true;
    VkResult result = (*self).waitFences((*self).native, 1, &(*self).acquireFence, VK_TRUE, SWAPCHAIN_WAIT_NS);
    if (result == VK_TIMEOUT)
        return false;
    if (result != VK_SUCCESS) {
        (*self).poisoned = true;
        return false;
    }
    (*self).fencePending = false;
    (*self).acquirePending = false;
    return true;
}

static bool swapchainRetire(VkSwapchain *self) {
    if ((*self).poisoned || !swapchainDrain(self))
        return false;
    if ((*self).submitted) {
        VkResult result = (*self).waitFences((*self).native, 1, &(*self).submitFence,
                                              VK_TRUE, SWAPCHAIN_WAIT_NS);
        if (result == VK_TIMEOUT)
            return false;
        if (result != VK_SUCCESS) {
            (*self).poisoned = true;
            return false;
        }
        (*self).submitted = false;
    }
    if ((*self).presentPending) {
        VkResult result = (*self).waitFences((*self).native, 1, &(*self).presentFence,
                                              VK_TRUE, SWAPCHAIN_WAIT_NS);
        if (result == VK_TIMEOUT)
            return false;
        if (result != VK_SUCCESS) {
            (*self).poisoned = true;
            return false;
        }
        result = VkDevice_waitForPresent((*self).owner, (*self).handle,
                                         (*self).presentId, SWAPCHAIN_WAIT_NS);
        if (result == VK_TIMEOUT)
            return false;
        if (result != VK_SUCCESS) {
            (*self).poisoned = true;
            return false;
        }
        (*self).presentPending = false;
        (*self).presented = true;
    }
    return true;
}

bool VkSwapchain_release(VkSwapchain *self) {
    if (!self || !(*self).acquired || (*self).submitted || !swapchainDrain(self))
        return false;
    VkReleaseSwapchainImagesInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT,
        .swapchain = (*self).handle, .imageIndexCount = 1, .pImageIndices = &(*self).index
    };
    if ((*self).releaseImages((*self).native, &info) != VK_SUCCESS) {
        (*self).poisoned = true;
        return false;
    }
    (*self).acquired = false;
    return true;
}

bool VkSwapchain_present(VkSwapchain *self, VkImage source, VkExtent2D sourceExtent) {
    if (!self || source == VK_NULL_HANDLE || !sourceExtent.width || !sourceExtent.height ||
        !VkDevice_canSafelyPresent((*self).owner) || !swapchainRetire(self))
        return false;
    if ((*self).presented) {
        (*self).presented = false;
        return true;
    }
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = (*self).format;
    uint32_t index = (*self).index;
    if (!(*self).acquired) {
        if (!VkSwapchain_acquire(self, &image, &format, &index))
            return false;
    } else {
        image = (*self).images[index];
    }
    if (!swapchainDrain(self))
        return false; // still owned; a later call may release after the fence signals
    if (sourceExtent.width != (*self).extent.width || sourceExtent.height != (*self).extent.height ||
        format != VK_FORMAT_B8G8R8A8_UNORM ||
        (*self).resetCommand((*self).cmd, 0) != VK_SUCCESS)
        goto release;
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    if ((*self).beginCommand((*self).cmd, &begin) != VK_SUCCESS)
        goto release;
    VkImageMemoryBarrier toTransfer = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    (*self).barrier((*self).cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    VkImageBlit region = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { { 0, 0, 0 }, { (int32_t) sourceExtent.width, (int32_t) sourceExtent.height, 1 } },
        .dstOffsets = { { 0, 0, 0 }, { (int32_t) (*self).extent.width, (int32_t) (*self).extent.height, 1 } } };
    (*self).blit((*self).cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
    VkImageMemoryBarrier toPresent = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    (*self).barrier((*self).cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                     0, 0, nullptr, 0, nullptr, 1, &toPresent);
    if ((*self).endCommand((*self).cmd) != VK_SUCCESS)
        goto release;
    if ((*self).resetFences((*self).native, 1, &(*self).submitFence) != VK_SUCCESS) {
        (*self).poisoned = true;
        return false;
    }
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &(*self).cmd,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &(*self).renderDone };
    if ((*self).queueSubmit((*self).queue, 1, &submit, (*self).submitFence) != VK_SUCCESS) {
        (*self).poisoned = true;
        return false;
    }
    (*self).submitted = true;
    if ((*self).presentId + 1 == 0 ||
        (*self).resetFences((*self).native, 1, &(*self).presentFence) != VK_SUCCESS) {
        (*self).poisoned = true;
        return false;
    }
    uint64_t id = (*self).presentId + 1;
    VkPresentIdKHR presentId = { .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .swapchainCount = 1, .pPresentIds = &id };
    VkSwapchainPresentFenceInfoEXT fenceInfo = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .pNext = &presentId, .swapchainCount = 1, .pFences = &(*self).presentFence };
    VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = &fenceInfo, .waitSemaphoreCount = 1, .pWaitSemaphores = &(*self).renderDone,
        .swapchainCount = 1, .pSwapchains = &(*self).handle, .pImageIndices = &(*self).index };
    if ((*self).queuePresent((*self).queue, &present) != VK_SUCCESS) {
        (*self).poisoned = true;
        return false;
    }
    (*self).presentId = id;
    (*self).acquired = false;
    (*self).presentPending = true;
    return swapchainRetire(self) && (*self).presented ? ((*self).presented = false, true) : false;
release:
    (void) VkSwapchain_release(self);
    return false;
}

bool VkSwapchain_acquire(VkSwapchain *self, VkImage *image, VkFormat *format, uint32_t *index) {
    if (image) *image = VK_NULL_HANDLE;
    if (format) *format = VK_FORMAT_UNDEFINED;
    if (index) *index = 0;
    if (!self || !image || !format || !index || (*self).handle == VK_NULL_HANDLE ||
        (*self).acquired || (*self).poisoned || (*self).submitted || (*self).presentPending ||
        !VkDevice_canSafelyPresent((*self).owner) || !swapchainDrain(self))
        return false;
    // An already-signaled fence is reset only after its prior completion proof.
    if ((*self).acquirePending == false && (*self).index != UINT32_MAX) {
        // index starts at zero; the fence is initially unsignaled. A reset of
        // an unsignaled fence is legal, including on the very first acquire.
        if ((*self).resetFences((*self).native, 1, &(*self).acquireFence) != VK_SUCCESS) {
            (*self).poisoned = true;
            return false;
        }
    }
    uint32_t acquiredIndex = 0;
    VkResult result = (*self).acquireNextImage((*self).native, (*self).handle, SWAPCHAIN_WAIT_NS,
                                                VK_NULL_HANDLE, (*self).acquireFence, &acquiredIndex);
    if (result == VK_TIMEOUT || result == VK_NOT_READY || result == VK_ERROR_OUT_OF_DATE_KHR) {
        // No image was returned, but the driver was handed our fence. Without
        // proof that it is no longer in use, never recycle or destroy it.
        (*self).poisoned = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        (*self).poisoned = true;
        return false;
    }
    (*self).fencePending = true;
    (*self).acquirePending = true;
    (*self).acquired = true;
    (*self).index = acquiredIndex;
    if (acquiredIndex >= (*self).count) {
        (*self).poisoned = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        (void) VkSwapchain_release(self);
        return false;
    }
    *image = (*self).images[acquiredIndex];
    *format = (*self).format;
    *index = acquiredIndex;
    return true;
}

bool VkSwapchain_resize(VkSwapchain *self, uint32_t width, uint32_t height) {
    if (!self || !width || !height || (*self).acquired || !swapchainRetire(self))
        return false;
    (*self).presented = false;
    // The Mac layer may reject two simultaneous swapchains. No queued work has
    // ever used this image, so retire the old one before rebuilding. Failure
    // leaves a valid empty owner that may be retried or destroyed.
    if ((*self).handle != VK_NULL_HANDLE)
        (*self).destroySwapchain((*self).native, (*self).handle, nullptr);
    (*self).handle = VK_NULL_HANDLE;
    free((*self).images);
    (*self).images = nullptr;
    (*self).count = 0;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkImage *images = nullptr;
    uint32_t count = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = { 0, 0 };
    if (!swapchainBuild(self, width, height, &handle, &images, &count, &format, &extent))
        return false;
    (*self).handle = handle;
    (*self).images = images;
    (*self).count = count;
    (*self).format = format;
    (*self).extent = extent;
    return true;
}

bool VkSwapchain_destroy(VkSwapchain *self) {
    if (!self || (*self).acquired || !swapchainRetire(self) ||
        !VkDevice_canSafelyPresent((*self).owner))
        return false;
    (*self).destroyPool((*self).native, (*self).pool, nullptr);
    (*self).destroySemaphore((*self).native, (*self).renderDone, nullptr);
    (*self).destroyFence((*self).native, (*self).presentFence, nullptr);
    (*self).destroyFence((*self).native, (*self).submitFence, nullptr);
    (*self).destroyFence((*self).native, (*self).acquireFence, nullptr);
    if ((*self).handle != VK_NULL_HANDLE)
        (*self).destroySwapchain((*self).native, (*self).handle, nullptr);
    free((*self).images);
    free(self);
    return true;
}

// SETTERS (PUBLIC & PRIVATE): none — lifecycle operations are exclusive.

// GETTERS (PUBLIC & PRIVATE)
VkExtent2D VkSwapchain_getExtent(const VkSwapchain *self) {
    return self ? (*self).extent : (VkExtent2D) { 0, 0 };
}

uint32_t VkSwapchain_getImageCount(const VkSwapchain *self) {
    return self ? (*self).count : 0;
}

bool VkSwapchain_isAcquired(const VkSwapchain *self) {
    return self && (*self).acquired;
}
