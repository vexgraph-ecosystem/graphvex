# Graphvex widget compositor — first implementation slice

Graphvex owns graphical composition for all widgets, including scene images.
Darling constructs widget interfaces and handles input, focus, layout policy and
the application/native-window bridge. See the **R3 Graphics Language & Board
Compositor Law** and **Absolute Rendering & Event Bound Law** in
[graphvex-preferences.md](graphvex-preferences.md).

## Implemented contract

`compositor/compositor.h` provides owned CPU float surfaces in linear-light
premultiplied RGBA. Each surface has an integer world origin independent of its
allocation. `Compositor_compose` source-overs borrowed sources in painter order
into an isolated group, then filters the assembled result once, left-to-right.
Group outputs can become sources for parent groups. Calls require external
synchronization; outputs transfer ownership only on success and remain unchanged
on failure. Destroy surfaces when their consumers finish.

`lang/filter.h` encodes `ID16 | payload48` in a `uint64_t`. This first slice supports:

| Filter | Parameters | Behavior |
|---|---|---|
| Identity | none | unchanged output |
| Gain | binary32 scalar in low 32 bits | scales linear RGB, preserves alpha; HDR is not clamped internally |
| Scatter blur | integer radius, 0–16 native pixels | uniform square kernel, full normalized denominator, transparent outside |

Each source pixel adds its weighted premultiplied RGBA across the blur footprint.
The output expands by the radius on all sides. Transparent edges remain faded;
there is no surviving-source-weight division that cancels that fade. Radius zero
is identity. Chained blur support accumulates. This is a finite box blur, not a
Gaussian approximation or the older analytic SDF soft edge.

Pixel/source/filter budgets and allocation errors are explicitly bounded/reported
by the public API. These are prototype limits, not a promise of arbitrary scene
capacity or zero steady-state allocation. Unknown IDs reject explicitly;
malformed payloads reject as invalid.

`compositor/filter_pool.h` provides indexed immutable complex recipes: ordered
arrays of the supported inline operations. Tokens use operation ID `0x8000`,
generation16 and index32 in the payload. `FilterPool_compose` snapshots/expands
recipes before synchronous composition; bare `Compositor_compose` rejects pooled
tokens because it has no pool context. Capacity and recipe limits are configured
at cold construction. Insert owns one reference; copying a token requires retain,
and final release invalidates it. Generation exhaustion permanently retires the
slot instead of aliasing old tokens. Destruction rejects outstanding references.
Pool tokens are local to their originating live pool, not globally transferable.
Nested pooled recipes and arbitrary new parameter schemas are not implemented.

## Absolute placement

`Element_eventBound(element, parent)` resolves placement/hit geometry.
`Element_absoluteBound(element, parent)` conservatively includes current own
softness/shadow and descendant paint, with local child-content clipping. The
parent rectangle establishes world placement, not an implicit ancestor clip.
External ancestor clips constrain the result when it is composed. Positive corner
radius retains the existing rounded child clip and rounded hit semantics.

The compositor's integer support is internal allocation geometry, not a third
public element bound. For a source at `(100,100)` sized `200×80`, blur radius 20
would require origin `(80,80)` and extent `240×120` (that radius exceeds this first
prototype's limit). Submit the expanded image at its expanded origin; never
stretch it back into the event rectangle. Layout padding/margin retain their
layout meanings and are not repurposed as filter halos.

Image adapters convert straight sRGB RGBA8 to/from linear-premultiplied float.
Conversion back to RGBA8 clamps at that boundary, not during internal gain.
The display-list bridge borrows a caller-owned exported image; keep it alive and
unchanged until submission finishes. The existing presentation path is not yet
fully linear-light; composing more groups before export avoids intermediate
RGBA8 quantization but does not claim end-to-end display parity.

## Deliberate gaps

- No automatic Element/Property filter-stack attachment or Darling setter migration.
- No foreground/backdrop scene-prefix planner, rounded group masks or reverse ROI
  culling yet. Callers explicitly choose group sources; allocation is not clipping.
- No arbitrary complex-filter parameter schemas, nested pooled recipes, contrast,
  HSV, progressive blur, noise or frosted-glass recipe yet. These follow
  individually with numeric evidence.
- CPU reference composition is not a GPU pipeline or a performance claim.
  Modular `src/shaders/compositor/` splat/resolve entrypoints are compiled by the
  umbrella build and independently SPIR-V-validated, but are not yet bound by a
  Vulkan compositor pipeline. Float attachment capability checks and GPU lifetime
  barriers remain mandatory before runtime integration.
- No independent scene scheduler, completed-image GPU handoff or fence retirement
  yet. Existing Frame/Surface orchestration remains the migration bridge.
- No target reuse/damage graph, cold preparation, hostile OOM injection, or
  cross-platform GPU/visual approval is implied.

## CPU reference usage

Run these allocating reference operations during cold preparation, not in a
production steady-state frame loop. The caller owns `outImage` on success and
must retain it through display-list submission.

```c
#include "compositor/compositor_image.h"
#include "compositor/compositor_submit.h"

CompositorStatus prepareGroup(const Image *content, DisplayList *list,
                               Image **outImage) {
    CompositorSurface *source = nullptr;
    CompositorStatus status = CompositorSurface_fromImage(content, 100, 100, &source);
    if (status != COMPOSITOR_OK)
        return status;
    const CompositorSurface *sources[] = {source};
    FilterToken filters[] = {Filter_gain(0.8f), Filter_scatterBlur(4)};
    CompositorSurface *group = nullptr;
    status = Compositor_compose(sources, 1, filters, 2, &group);
    if (status == COMPOSITOR_OK)
        status = Compositor_record(group, list, outImage);
    CompositorSurface_destroy(group);
    CompositorSurface_destroy(source);
    return status;
}
```

## Automated checks

Tests live in the workspace `tests/graphvex/`, mirroring their owning units.
Use `./tools/b test compositor`, `./tools/b test filter_pool_test`,
`./tools/b test filter_test` and `./tools/b test element_bounds_test` from the
workspace. Documentation contracts use
`python3 tests/tools/compositor_contract_test.py`; compile/validation-only shader checks use
`python3 tests/tools/compositor_shader_test.py`. Actual timestamps and scopes
belong in the generated test checklist, not this document. Numeric tests do not
constitute user appearance approval.
# Three-scope gallery integration

`tools/b run filter_gallery` opens one Application-attached Darling Frame with
Backdrop, Foreground and Element examples. The fixed-size cards use native tree
anchors: backdrop left, foreground centered, element right; their positions are
resolved from live parent bounds rather than copied from the initial width.
A generated landscape supplies clear
edges: backdrop frost blurs only prior scene under a panel, foreground/child blur
spills inside its panel but is clipped at the panel edge, and whole-element blur
softens the assembled panel and spills beyond it. These are explicit CPU scope
submissions displayed by real Picture/Element image painting, not automatic
Element filter attachments or GPU spatial filtering.

Resize updates use the ordinary Frame resize cascade, but changed geometry
bypasses the content/focus FPS cap so publication does not wait for an
Application poll inside AppKit's tracking loop. Normal content remains paced.
The current host publishes IOSurface contents through CALayer (not a
CAMetalLayer drawable); live resize flushes after committing the transaction.
`frame_live_resize_test` asserts immediate native publication under a frozen
clock; `filter_gallery --smoke` checks four extents and all three anchors using
published IOSurface pixels without a capture-induced repaint. Neither proves
human drag smoothness or eliminates reference-renderer
cost (color-run generation and target recreation remain).

`compositor/compositor_scope.h` exposes `Compositor_scopedScene` and an
origin-aware `Compositor_crop`. This initial scope builder requires an opaque
prior scene, rectangular masks, inline tokens and sufficient backdrop sample
halo inside the viewport. It rejects unsupported input without replacing output.
Scope, fixture and Picture owner tests pass; native `filter_gallery --smoke`
asserts captured image pixels. This is not appearance approval. Vulkan's current
CPU-shadow image adapter emits pixel color-run quads; optimized texture uploads
remain future work.
