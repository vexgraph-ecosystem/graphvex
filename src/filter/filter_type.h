#ifndef GRAPHVEX_FILTER_TYPE_H
#define GRAPHVEX_FILTER_TYPE_H

/* Canonical ID16 operation registry. IDs are numeric ABI: never renumber/reuse.
 * Declaration is vocabulary, not backend support. 0x8000 remains the existing
 * immutable recipe-pool operation, distinct from typed parameter references. */
// INTENTIONAL(vex): operation constants use FILTERNAME_ID (GAUSSIAN_BLUR_ID)
// so the effect name reads first and the suffix states its identity role.
#define IDENTITY_ID 0x0000u
#define GAIN_ID 0x0001u
#define SCATTER_BLUR_ID 0x0002u
#define BRIGHTNESS_ID 0x0003u
#define CONTRAST_ID 0x0004u
#define TONE_CURVE_ID 0x0005u
#define HSL_ID 0x0006u
#define HSV_ID 0x0007u
#define COLOR_BALANCE_ID 0x0008u
#define EDGES_ID 0x0009u
#define DROP_SHADOW_ID 0x000au
#define MONOCOLOR_ID 0x000bu
#define GRAYSCALE_ID 0x000cu
#define GRAYSCALE_RED_ID 0x000du
#define GRAYSCALE_GREEN_ID 0x000eu
#define GRAYSCALE_BLUE_ID 0x000fu
#define BLACK_AND_WHITE_ID 0x0010u
#define INVERT_ID 0x0011u
#define GRADIENT_MAP_ID 0x0012u
#define REPLACE_COLOR_ID 0x0013u
#define GAUSSIAN_BLUR_ID 0x0014u
#define BOX_BLUR_ID 0x0015u
#define ZOOMING_BLUR_ID 0x0016u
#define MOVING_BLUR_ID 0x0017u
#define SPIN_BLUR_ID 0x0018u
#define LENS_BLUR_ID 0x0019u
#define MOSAIC_ID 0x001au
#define UNSHARP_MASK_ID 0x001bu
#define FROSTED_GLASS_ID 0x001cu
#define STROKE_ID 0x001du
#define STAINED_GLASS_ID 0x001eu
#define OUTER_GLOW_ID 0x001fu
#define INNER_GLOW_ID 0x0020u
#define EMBOSS_ID 0x0021u
#define RELIEF_ID 0x0022u
#define WATERDROP_ID 0x0023u
#define EXTRUDE_ID 0x0024u
#define GOD_RAYS_ID 0x0025u
#define CHROMATIC_ABERRATION_ID 0x0026u
#define GLITCH_ID 0x0027u
#define NOISE_ID 0x0028u
#define DITHERING_ID 0x0029u
#define CHROME_ID 0x002au
#define BLOOM_ID 0x002bu
#define SHEER_ID 0x002cu
#define PIXELATE_ID 0x002du
#define POINTILLIZE_ID 0x002eu
#define EXPANSION_ID 0x002fu
#define FISHEYE_ID 0x0030u
#define SPHERE_ID 0x0031u
#define WAVE_ID 0x0032u
#define DOTS_ID 0x0033u
#define RECIPE_POOL_ID 0x8000u

/* Existing client source compatibility only; new code uses FILTERNAME_ID.
 * Aliases reference the same registry, never duplicate numeric definitions. */
#define FILTER_IDENTITY IDENTITY_ID
#define FILTER_GAIN GAIN_ID
#define FILTER_SCATTER_BLUR SCATTER_BLUR_ID
#define FILTER_POOL_ID RECIPE_POOL_ID

#endif
