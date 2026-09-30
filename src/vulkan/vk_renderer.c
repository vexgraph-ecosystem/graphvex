#include "vulkan/vulkan_backend.h"

#include "image.h"
#include "vulkan/device.h"

// graphvex R3 — vulkan/vk_renderer.c
//
// The Backend row. Display-list verbs land in the CPU quad batch (VkBatch);
// present() uploads the batch and issues the draw. The device session lives in
// Device. The swapchain/CAMetalLayer seam (vk_surface) is the next slice —
// until it lands, present() reports false rather than lying about a frame.

static Device *s_ctx = NULL;
static VkBatch *s_batch = NULL;
static Rect s_clip = {0, 0, 0, 0};
static uint32_t s_w = 0, s_h = 0;
static char s_error[256] = "ok";

const char *VulkanBackend_lastError(void) { return s_error; }
const VkBatch *VulkanBackend_batch(void) { return s_batch; }

bool VulkanBackend_bind(void *nativeLayer, uint32_t widthPx, uint32_t heightPx) {
    (void)nativeLayer;   // the CAMetalLayer/WSI seam arrives with vk_surface
    VulkanBackend_unbind();
    s_ctx = Device_create(false);
    if (!Device_isValid(s_ctx)) {
        if (s_ctx) {
            const char *e = Device_lastError(s_ctx);
            for (size_t i = 0; e[i] && i < sizeof s_error - 1; i++) s_error[i] = e[i];
        } else {
            const char *e = "Device_create failed";
            for (size_t i = 0; e[i] && i < sizeof s_error - 1; i++) s_error[i] = e[i];
        }
        return false;
    }
    s_batch = VkBatch_0();
    s_w = widthPx;
    s_h = heightPx;
    s_clip = (Rect){0, 0, (float)widthPx, (float)heightPx};
    return s_batch != NULL;
}

void VulkanBackend_unbind(void) {
    VkBatch_free(s_batch);
    s_batch = NULL;
    Device_destroy(s_ctx);
    s_ctx = NULL;
}

static bool vk_begin(void) {
    if (!s_batch) s_batch = VkBatch_0();
    if (!s_batch) return false;
    VkBatch_clear(s_batch);
    return true;
}

static bool vk_end(void) { return s_batch != NULL; }

static bool vk_present(void) {
    // TODO(seam): upload the batch, bind the one pipeline, issue ONE draw,
    // then present through the host surface seam (borrowed CAMetalLayer on
    // Apple; the host window elsewhere). NEVER a swapchain present.
    return false;
}

static bool vk_resize(uint32_t w, uint32_t h) {
    s_w = w;
    s_h = h;
    s_clip = (Rect){0, 0, (float)w, (float)h};
    return true;
}

static bool vk_clear(Color color) {
    if (!s_batch) return false;
    // a full-viewport quad under everything = the clear
    Rect full = {0, 0, (float)s_w, (float)s_h};
    VkBatch_rect(s_batch, full, &(Brush){color, 0.0f, 0u, 0.0f});
    return true;
}

static bool vk_clip(const Rect *rect) {
    s_clip = rect ? *rect : (Rect){0, 0, (float)s_w, (float)s_h};
    return true;
}

static bool vk_fillRect(const Rect *rect, const Brush *brush) {
    if (!s_batch || !rect || !brush) return false;
    Rect clipped = Rect_intersect(*rect, s_clip);
    VkBatch_rect(s_batch, clipped, brush);
    return true;
}

static bool vk_drawImage(const Image *image, const Rect *dst) {
    if (!s_batch || !image || !dst) return false;
    Rect src = {0, 0, (float)Image_width(image), (float)Image_height(image)};
    VkBatch_image(s_batch, image, src, *dst);
    return true;
}

static bool vk_drawText(const Rect *rect, const char *text, const Brush *brush) {
    // glyph atlas slice: emit one glyph quad per advance. Placeholder = one quad.
    if (!s_batch || !rect) return false;
    VkBatch_glyph(s_batch, *rect, 0u, brush ? brush->color : COLOR_WHITE);
    (void)text;
    return true;
}

const Backend *VulkanBackend_row(void) {
    static const Backend row = {
        BACKEND_VULKAN, vk_begin,   vk_end,     vk_present,
        vk_resize,      vk_clear,   vk_clip,    vk_fillRect,
        vk_drawImage,   vk_drawText,
    };
    return &row;
}
