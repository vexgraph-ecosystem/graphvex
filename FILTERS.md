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

**Constructors are implemented; the newly named effects are not implemented.**
They encode arguments only: no allocation, ownership acquisition, parameter
validation, image mutation or implicit pool lookup. The current CPU compositor
executes only identity, gain and scatter blur. Every new ID returns
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
Ranges, color spaces and defaults for newly named effects remain to be defined
at their actual execution seam; a constructor cannot assert those policies.
Brightness's scalar denotes an additive amount, contrast a multiplier, and
black-and-white a threshold. Complex modes (parallel/perspective extrusion,
pixelate/sheer shape, noise seed, gradient stops) belong to future typed records,
not guessed encodings squeezed into spare payload bits.

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
`tools/b test filter_pool_test`. These prove encoding and current rejection,
not new effect pixels, Pool migration or GPU runtime support.
