# Filter vocabulary and implementation roadmap

## What exists

`src/filter/filter_type.h` is the single operation-ID registry, using hexadecimal
`FILTERNAME_ID` constants such as `GAUSSIAN_BLUR_ID`, `DITHERING_ID`, `HSL_ID`
and `HSV_ID`. Four old names remain aliases for existing client compatibility;
new code uses the suffix form. Existing numeric
values remain identity=0, gain=1, scatter blur=2 and recipe pool=0x8000.
`src/filter/filter_functions.h` provides header-only `Filter_*` token constructors
for the requested collection, including separate HSL and HSV. `lang/filter.h`
is a compatibility include. Tokens remain numeric `ID16 | payload48`, never
pointer payloads. Endian encoding must be explicit when serializing.

**Color effects execute in Vulkan shaders, not CPU filters.**
They encode arguments only: no allocation, ownership acquisition, parameter
validation, image mutation or implicit pool lookup. The current CPU compositor
reference executes only its pre-existing identity, gain and scatter blur.
The CPU color extension has been removed; new color IDs return
`COMPOSITOR_UNSUPPORTED`, preserving submission outputs. No renderer silently
treats these declarations as identity effects.

## Constructor forms

| Form | Functions | Payload |
|---|---|---|
| No arguments | identity, grayscale, grayscaleRed, grayscaleGreen, grayscaleBlue, invert | zero |
| One float | gain, brightness, contrast, blackAndWhite | exact binary32 bits; upper payload bits zero |
| One packed color | monocolor | `0xRRGGBBAA` in low 32 bits |
| One integer radius | scatterBlur | unmodified uint32 native-pixel radius |
| Parameter reference | all remaining named operations | index32 plus generation16 |

For example, `Filter_dithering(index, generation)` and
`Filter_hsl(index, generation)` encode references to parameter entries, **not**
the settings themselves. No typed parameter pool exists yet to allocate those
entries. These constructors establish a vocabulary for that next implementation;
they must not be presented as working effect creation. The caller must eventually
supply the originating pool context: equal index/generation values in different
pools are not interchangeable. Generation zero is encoded unchanged and reserved
as invalid, not converted to identity. Copies do not retain an entry.

All float bit patterns, including NaN, infinity and negative zero, are preserved.
Ranges and color spaces for unimplemented effects remain to be defined
at their actual execution seam; a constructor does not validate those policies.
Brightness's scalar denotes an additive amount, contrast a multiplier, and
black-and-white a threshold. Complex modes (parallel/perspective extrusion,
pixelate/sheer shape, noise seed, gradient stops) belong to future typed records,
not guessed encodings squeezed into spare payload bits.

## Vulkan texture color pass

`compositor/color_pass.{c,h}` creates a real Vulkan graphics pipeline using
`resolve.vert` and `color.frag`. `ColorPass_record` samples an isolated group's
completed float texture and draws a fullscreen triangle into a separate float
attachment. No pixel processing, upload, readback or CPU fallback occurs in this
production pass. Readback exists only in the GPU owner test.

All eight color operations preserve alpha and extents and execute once on
the assembled group. Brightness accepts a finite additive amount in [-1,1];
contrast accepts a finite nonnegative multiplier around straight linear 0.5.
These and invert clamp straight RGB to [0,1], intentionally saturating HDR even
at neutral brightness/contrast. Weighted grayscale uses linear Rec.709 luminance;
channel grayscale replicates its selected channel. Both preserve HDR.
Black-and-white accepts a finite threshold in [0,1]; luminance equal to the
threshold selects white. Transparent pixels remain transparent black.

Premultiplied affine formulas avoid unpremultiplying tiny alpha. Contrast detects
saturation before multiplication, avoiding overflow without requiring float64.
GPU denormal handling is device-dependent; exact subnormal parity is not claimed.
Reserved scalar bits,
nonfinite/out-of-range parameters and nonzero no-argument payloads reject with
`false` with one cold THROW at token validation/recording, before recording any
commands. The pass borrows its device, command buffer, descriptor and textures.
The caller cold-allocates a binding-0 combined-image-sampler descriptor matching
`ColorPass_getDescriptorLayout`, supplies a compatible single-color render pass
(subpass zero, sample count one), and keeps every resource alive through completion.
Use linear-premultiplied float targets with supported sampled/color formats;
pointwise blending is disabled. Final group composition uses source-over later.

Stacks execute via ping-pong targets, with a color-write-to-fragment-read barrier
between passes. Never sample the current destination attachment. Both images have
the same native-pixel extent and retained world origin; no stretching. The caller
selects foreground/backdrop/element input; there is one shader algorithm. Automatic
tree/scope scheduling and attachment APIs remain unfinished, as do scatter runtime
pipeline binding, allocation fault injection and other-platform GPU proof.
Current automated GPU evidence is on Apple A18 Pro through MoltenVK. The local
Vulkan loader dylib was built for macOS 26; execution at the macOS 14 support
floor is unproved. Device validation-layer support is also not enabled by the
current Device implementation. Neither gap is shader-compilation evidence.

## One implementation, three scopes

Element, foreground and backdrop select sources, not separate effect algorithms:

- **Foreground:** content and children, excluding own box decoration.
- **Backdrop:** the painter-order prefix behind the element, excluding self and
  later siblings. Filtered backdrop replaces its masked prefix region.
- **Element:** the complete assembled group, including decoration, filtered
  backdrop and children.

`CompositorScopeDesc` already carries these three ordered token arrays in the CPU
reference seam. Automatic Element attachment APIs and `element_filter`,
`foreground_filter`, `backdrop_filter` modules are not implemented. They should
retain stack entries and use one shared effect library, not duplicate effects.

## Scatter is not vertex-to-compute

Current CPU scatter sends each source pixel's weighted premultiplied RGBA to
multiple destination pixels. Shader prototypes use **vertex → fragment**:
`scatter.vert` creates a footprint per source pixel; `scatter.frag` emits weighted
color and weight into float attachments requiring additive ONE/ONE blending;
`resolve.frag` resolves before ordinary source-over composition. They are not
bound to a working GPU compositor pipeline yet. Shader compilation is not GPU
execution proof.

A future compute implementation would be a separate dispatch with supported
atomics or another race-free accumulation strategy, followed by resolve and
explicit synchronization. Graphics blend state does not protect compute writes.
Preserve scatter-first spatial filtering, expanded world-origin bounds,
linear-light premultiplied accumulation, declared normalization and stack order.
Pointwise color filters need not scatter to neighboring pixels.

## Next implementation and proof

1. Implement typed parameter entries over the general Pool, with ownership,
   copy-on-write, stable IDs and explicit migration semantics.
2. Implement effects individually with parameter contracts and CPU numeric
   references. Channel grayscale replicates the selected channel across RGB;
   HSL and HSV remain distinct. Define alpha and HDR handling explicitly.
3. Integrate retained stacks and the three scopes without changing hit bounds.
4. Add GPU pipelines, capability checks and retirement/synchronization proof.

Reference behavior comes from the public
[ibisPaint filter tutorials](https://ibispaint.com/lecture/index.jsp?lang=en),
not claims about its private algorithms. Its
[Sheer](https://ibispaint.com/lecture/index.jsp?no=116&lang=en) is patterned noise;
[Retro Game](https://ibispaint.com/lecture/index.jsp?no=181&lang=en) includes
eight-color conversion; [Relief](https://ibispaint.com/lecture/index.jsp?no=134&lang=en)
describes raised shading, not documented hardware ray tracing.

Registered token/registry proof: `tools/b test filter_functions_test`,
`tools/b test filter_type_test`, plus existing `tools/b test filter_test` and
`tools/b test filter_pool_test`. These prove encoding and current rejection.
`tools/b test color_pass_test` executes the Vulkan texture pipeline and asserts
readback pixels, alpha/HDR, ordered ping-pong passes, cold rejection diagnostics
and borrowed-device lifetime. CPU arithmetic in this test is only an oracle.
Pool migration, automatic scope/tree wiring and the remaining GPU effects are
not implemented.
