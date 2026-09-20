#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

// Unified PictureMode constants
#define MODE_FIT               0u
#define MODE_ZOOM_FILL         1u
#define MODE_ZOOM_FIT          2u
#define MODE_FILL_CENTER       3u
#define MODE_FILL_TOP_LEFT     4u
#define MODE_FILL_TOP_RIGHT    5u
#define MODE_FILL_BOTTOM_LEFT  6u
#define MODE_FILL_BOTTOM_RIGHT 7u

layout(push_constant) uniform Push {
    layout(offset = 16) vec4  u_color;      // tint (rgba)
    layout(offset = 32) uint  u_textureId;  // Bindless texture ID
    // 4 bytes padding at offset 36
    layout(offset = 40) vec2  u_imgSize;    // image width, height
    layout(offset = 48) vec2  u_quadSize;   // quad width, height
    layout(offset = 56) uint  u_mode;       // PictureMode
} push;

layout(set = 0, binding = 0) uniform sampler2D u_textures[];

// Returns the x-axis offset for a specific mode.
float horzOffset(uint mode, float slack) {
    if (mode == MODE_FILL_TOP_RIGHT || mode == MODE_FILL_BOTTOM_RIGHT) return slack;
    if (mode == MODE_FILL_CENTER || mode == MODE_ZOOM_FILL || mode == MODE_ZOOM_FIT) return slack * 0.5;
    return 0.0; // TOP_LEFT, BOTTOM_LEFT
}

// Returns the y-axis offset for a specific mode.
float vertOffset(uint mode, float slack) {
    if (mode == MODE_FILL_BOTTOM_LEFT || mode == MODE_FILL_BOTTOM_RIGHT) return slack;
    if (mode == MODE_FILL_CENTER || mode == MODE_ZOOM_FILL || mode == MODE_ZOOM_FIT) return slack * 0.5;
    return 0.0; // TOP_LEFT, TOP_RIGHT
}

void main() {
    vec2 uv = v_uv; // base 0→1 across the quad

    float imgAspect = push.u_imgSize.x / push.u_imgSize.y;
    float quadAspect = push.u_quadSize.x / push.u_quadSize.y;
    float r = quadAspect / imgAspect;

    if (push.u_mode == MODE_FIT) {
        // Stretch and warp — UV unchanged, image fills the entire quad
    }
    else if (push.u_mode >= MODE_FILL_CENTER && push.u_mode <= MODE_FILL_BOTTOM_RIGHT) {
        // 1:1 pixel mapping (no scaling).
        float fracX = push.u_quadSize.x / push.u_imgSize.x;
        float fracY = push.u_quadSize.y / push.u_imgSize.y;
        float offsetX = horzOffset(push.u_mode, 1.0 - fracX);
        float offsetY = vertOffset(push.u_mode, 1.0 - fracY);
        uv.x = uv.x * fracX + offsetX;
        uv.y = uv.y * fracY + offsetY;
    }
    else if (push.u_mode == MODE_ZOOM_FILL) {
        // Cover: scale so the quad is completely filled. Crop the overflow.
        if (r >= 1.0) {
            // Quad wider than image → fill width exactly, crop top/bottom
            float visibleFrac = 1.0 / r;             // how much of image height is visible
            float offset = vertOffset(push.u_mode, 1.0 - visibleFrac);
            uv.y = uv.y * visibleFrac + offset;
        } else {
            // Quad taller than image → fill height exactly, crop left/right
            float visibleFrac = r;                    // how much of image width is visible
            float offset = horzOffset(push.u_mode, 1.0 - visibleFrac);
            uv.x = uv.x * visibleFrac + offset;
        }
    }
    else if (push.u_mode == MODE_ZOOM_FIT) {
        // Contain: entire image is visible. Letterbox/pillarbox the dead space.
        if (r >= 1.0) {
            // Quad wider → pillarboxes on left/right
            float imgFrac = 1.0 / r;                 // fraction of quad width the image occupies
            float offset = horzOffset(push.u_mode, 1.0 - imgFrac);
            uv.x = (uv.x - offset) / imgFrac;
        } else {
            // Quad taller → letterboxes top/bottom
            float imgFrac = r;                        // fraction of quad height the image occupies
            float offset = vertOffset(push.u_mode, 1.0 - imgFrac);
            uv.y = (uv.y - offset) / imgFrac;
        }
    }

    if (push.u_mode == MODE_ZOOM_FIT || (push.u_mode >= MODE_FILL_CENTER && push.u_mode <= MODE_FILL_BOTTOM_RIGHT)) {
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 0.0);
            return;
        }
    }

    vec4 texColor = texture(u_textures[nonuniformEXT(push.u_textureId)], uv);
    fragColor = texColor * push.u_color;
}
