# graphvex — Repo-Local Living Preferences
> Exclusive repository-level preferences (the Living Preferences Law).
> Universal Supreme Constitution: preferences.md (vexspoke).

;;SYNC("mirrors ecosystem/vexspoke/preferences.md @ 2026.09-universal")

## 0. Constitution Link (supreme)
- [preferences.md](https://github.com/vexgraph-dev/vexspoke/blob/main/preferences.md) (canonical, vexspoke) — accessible locally at ../../preferences.md
- All universal laws in `../../../preferences.md` are mandatory and binding across the ecosystem.
- This document codifies **exclusive** preferences that apply uniquely to `graphvex` (R3 GPU Driver).

## 1. Exclusive Preferences Binding Matrix

| Law Title | Scope | Enforcement |
| :--- | :--- | :--- |
| **Unified Graphics Abstraction Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Strict 0xRRGGBBAA Color Law** | R3 GPU Driver / Universal | Mandatory for `graphvex` |
| **SPIR-V Shader Deployment Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Ecosystem Vulkan Safety Nets Law (Determinism + Tree-Shaken Truth)** | R3 GPU Driver | Mandatory for `graphvex` |
| **Native Pixel Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **R3 Graphics Language & Board Compositor Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **UI Graphics Source Placement Law** | R3 graphics primitives | Mandatory for `graphvex` |
| **Absolute Rendering & Event Bound Law** | R3 element geometry/composition | Mandatory for `graphvex` |
| **Ordered Filter & Scatter Composition Law** | R3 widget compositor | Mandatory for `graphvex` |
| **Independent Scene Cadence Law** | R3 composition/image handoff | Mandatory for `graphvex` |

## 2. Exclusive Repo-Local Laws (FULL PROSE RESTATEMENT)

### Unified Graphics Abstraction Law

#### Definition:
`graphvex` provides unified, backend-agnostic graphics primitives (`Image`, `Fence`, `Semaphore`, `Swapchain`, `CommandBuffer`, `CommandQueue`, `Surface`, and `GraphicsLayer`) that decouple high-level application and UI rendering pipelines from concrete hardware APIs (Vulkan, Metal, Direct3D).
- **CPU Shadow & Zero Steady-State Allocation:** Resource handles (`Image`, `Swapchain`) maintain tightly-packed CPU shadow memory blocks (e.g. RGBA8 at width * height * 4 bytes) allocated through the `vexspoke` arena (`Memory_alloc`). High-level operations manipulate metadata and memory-mapped buffers without invoking heavy GPU driver state transitions during recording.
- **Synchronization Contracts:** GPU-CPU join points (`Fence`) enforce a hard 100ms ceiling under the Bounded Wait Law (`FENCE_WAIT_TIMEOUT_NS`), preventing deadlocks on dropped frames. GPU-GPU timeline ordering (`Semaphore`) advances monotonically and is strictly probed, never blocking CPU threads.
- **Dual Presentation Seam:** Display surfaces and swapchains seamlessly bridge into OS-level compositing layers (CAMetalLayer via `GraphicsLayer`, `VkIOSurface`, and window surface hooks) using native hardware pixel scaling (the Native Pixel Law) without leaking backend API types across repository boundaries. On Apple the host creates the IOSurface; `VulkanBackend_bindSurface` imports it as the render target (`VK_EXT_metal_objects`, `VkImportMetalIOSurfaceInfoEXT`) so the GPU writes the exact bytes CoreAnimation composites — zero-copy, no readback, still no `VkSwapchainKHR`.

#### The Why:
Directly coupling UI widgets or game logic to Vulkan or Metal handles creates intractable cross-platform fragmentation, dangling command buffers across dynamic reload cycles, and driver lockups. By unifying graphics objects into pure C23 structs with predictable memory footprints, bounded waits, and optional backend binding, `graphvex` guarantees deterministic multi-platform rendering across macOS, Linux, and Windows with zero driver lock-in.

#### The Rule:
1. **Agnostic Primitives First:** High-level rendering operates exclusively on unified `Image`, `Swapchain`, `CommandBuffer`, and `GraphicsLayer` structs. Never expose driver handles (`VkImage`, `VkDevice`, `MTLDevice`) in client headers.
2. **Bounded Synchronization:** All fence joins and timeline waits must obey `FENCE_WAIT_TIMEOUT_NS` (100ms max). Indefinite GPU waits (`UINT64_MAX`) are strictly prohibited.
3. **Transparent Backing Resolution:** Backing surfaces must resolve pixel scaling via `backingScaleFactor` to ensure native 1:1 hardware pixel mapping.
4. **Fitted Image Contract:** Every image drawn into a destination rect resolves through `Image_fitRect` + `Graphics_drawImageFit` — never a bespoke widget transform. The families are `IMAGE_FIT_STRETCH` (whole image stretched into dst), `IMAGE_FIT_CONTAIN` (fit inside, centered, letterboxed), `IMAGE_FIT_COVER` (cover, centered, overflow cropped), and `IMAGE_FIT_WINDOW` (a source-pixel window, anchored, scaled to fill dst). A WINDOW is widget-shaped: at most one of `windowW` / `windowH` drives it and the other derives from the dst aspect, so the same window is expressible from either axis; unset means dst pixels (true 1:1); the window always clamps to the image (you cannot show pixels that do not exist). The scale is `dst ÷ window` — a 300-px window in a 600-px dst draws at 2x. Fit math is pure, R3-owned, and testable without a backend; widgets call it, never reimplement it.

---

### Strict 0xRRGGBBAA Color Law

#### Definition:
All packed 32-bit integer colors across the ecosystem are strictly and uniformly formatted as **`0xRRGGBBAA`** (Red in bits 31–24, Green in bits 23–16, Blue in bits 15–8, Alpha in bits 7–0). In continuous memory buffers, pixel shadow arrays (`Image`), and color framebuffers (`ColorBuffer`), byte channel ordering is strictly monotonic:
- Byte index 0: Red ($[0..255]$)
- Byte index 1: Green ($[0..255]$)
- Byte index 2: Blue ($[0..255]$)
- Byte index 3: Alpha ($[0..255]$)

#### The Why:
Fragmented color channel encodings (`0xAARRGGBB` vs `0xRRGGBBAA` vs `0xBBGGRRAA`) create subtle visual bugs, inverted transparency masks, and fragile bit-shift arithmetic across UI widgets, brushes, rasterizers, and GPU shaders. Aligning packed hex constants with sequential memory storage ensures that reading a color hex code left-to-right matches its natural channel order (R, G, B, A), eliminates channel-swizzle confusion, and maps 1:1 to standard GPU vector types (`vec4(r, g, b, a)`).

#### The Rule:
1. **Packed 32-Bit Hex Order:** All color constants and parameter integers must be formatted as `0xRRGGBBAA`. Legacy `0xAARRGGBB` is strictly prohibited.
   - Opaque Black is `0x000000FFu` (never `0xFF000000u`).
   - Opaque White is `0xFFFFFFFFu`.
   - Fully Transparent / Clear is `0x00000000u`.
2. **Monotonic Channel Extraction:** Unpacking channels from packed colors must always use monotonic shifts:
   - `Red   = (color >> 24) & 0xFFu`
   - `Green = (color >> 16) & 0xFFu`
   - `Blue  = (color >> 8)  & 0xFFu`
   - `Alpha = color & 0xFFu`
3. **Presentation Boundary Isolation:** Hardware scanout requirements on platform surfaces (such as Apple's native BGRA display swapchains) must be encapsulated entirely within driver-level presentation adapters (`VkIOSurface`, swapchain blits, or final composite shaders), keeping all application logic, brushes, meshes, buffers, and textures strictly RGBA.

---

### SPIR-V Shader Deployment Law

GLSL sources belong to Graphvex's `src/shaders/`; the umbrella build `tools/b`
compiles ahead-of-time SPIR-V into its external build-state `shader/` directory.
Existing stage directories remain supported. New modules separate responsibilities
(`ui`, `shadow`, `compositor`, `filters`, `light`) rather than creating one
unbounded shader spanning widget composition and scene lighting. Shared color,
alpha and coordinate helpers have one implementation. Darling and vexspoke own
no shader blobs. A source module is not runtime support until a backend pipeline
actually executes it and has stated automated evidence.

**Runtime Shader Resolution Protocol**:
The runtime loader (`loadSpvAny`) must search in this exact precedence order:
1. `ANTI_SPV_DIR` / `VEX_SPV_DIR` (build-time staging directory `${CMAKE_BINARY_DIR}/spv/`, populated from `/shader/spv/`)
2. `<exe_dir>/spv/<name>` (adjacent deployment)
3. `<exe_dir>/../Resources/spv/<name>` (macOS `.app` bundle)
4. CWD-relative paths (`spv/<name>`, `src/_old/vulkan/spv/<name>`)

The umbrella build is shader build truth; IDE adapters delegate to it. Compiler
errors propagate, generated outputs stay outside source, and shader dependencies
must invalidate their consumers. Pointwise fusion uses bounded instruction/variant
budgets and preserves order; spatial filters are explicit pipeline boundaries.

---

### Ecosystem Vulkan Safety Nets Law (Determinism + Tree-Shaken Truth)

#### Definition:
Vulkan is the one subsystem whose failure surfaces at a *later* boundary than its cause: MoltenVK reports a lost device only at the next API touch, so a `DEVICE LOST` logged at acquire means the damage happened at an earlier call. Every Vulkan-touching file in the ecosystem therefore carries the same two-layer contract: **deterministic handling** (same input sequence ⇒ same seam name, same decision, same log line) and **seam guards** (every driver-facing function validates the health chain before touching the driver). The net spans ALL Vulkan touchpoints — not just the R1 present chain: hotcwap `vulkan.c` (present), graphvex `texture.c`, `sdf_gpu.c`, `vk_scene.c`, `vk_view.c`, `vk_iosurface.c`, the seam canvas in `vk_instance.c`, darling `compositor.c` (re-record batch). Adding a new file that calls the driver without joining the net is a defect (the repository's class registry alone is too weak — driver calls span repos).

#### The Why:
GPU failures are only debuggable if the report site equals the cause site. Chasing acquired-device-lost logs is whack-a-mole; a seam guard converts "the driver died somewhere" into "health broke at seam X" and makes every path to failure reachable, named, and identical — deterministic output producing deterministic debugging. It is not "zero bugs"; it is the guarantee that every failure is reachable, named, and reproducible.

#### The Rule:
1. **One canonical guard, zero forks.** `VkGuard_check(seam, device, queue, deviceLost)` lives once in `graphvex/src/vulkan/vk_guard.h` (header-only, no struct, the Single Class Per File Law private-helper doctrine; allowlist-safe: graphvex→own, hotcwap→graphvex, darling→graphvex). It returns `false` when the device is lost or null; a null queue is permitted only on device-resource seams (create/destroy/record) — any seam that submits/fences/presents must pass its queue so the whole queue chain is covered. Callers degrade exactly like any transient failure: return `false`, keep dirty state, retry next tick (the Bounded Wait Law + the Cold-Strict, Hot-Minimal Validation Law). Never a crash, never UB, never a wedged wait.
2. **Tree-shaken when released.** Under `NDEBUG` the guard is a macro no-op: zero calls, zero branches, unevaluated arguments. Release binaries are the Cold-Strict, Hot-Minimal Validation Law hot-minimal skeleton of the debug build — the debug net never counts against hot-minimal branch budgets (the Conflict Triage Law triage: debug exhaustive / release minimal is the managed exception, codified here). Every file's `;;OVERVIEW` lists which of its functions carry the net.
3. **Deterministic driver-state handling.** Every wait/acquire/submit/present follows one fixed decision table, written once: success advances; timeout-with-signal recovers; timeout-unsignaled drops with dirty state intact and retries next tick; `VK_ERROR_DEVICE_LOST` latches once at the true site (`presentDeviceLost(where)` in hotcwap — the latch is single-owned; downstream repos must not re-implement it, they may query `Vk_isDeviceLost()` wherever the allowlist permits) and short-circuits every later pass.
4. **The report site is never the cause.** First action on any `VK_ERROR_DEVICE_LOST`: run with `MVK_CONFIG_LOG_LEVEL` enabled and read MoltenVK's underlying Metal error (`MTLCommandBuffer` error code + message) BEFORE touching code — `MTLCommandBufferErrorInternal`/`PageFault`/`Timeout` distinguishes a usage defect from a GPU power/restart event. Paste both lines together; never "fix the acquire" until MoltenVK says the acquire is the cause.
5. **No loopholes in the health chain.** Handles are nulled in the same teardown pass (the Teardown Order Law) so a stale guard catches a real lifecycle defect instead of passing on a zombie pointer. Cold resource-creation paths (graphvex images/textures/framebuffers/views) keep the Cold-Strict, Hot-Minimal Validation Law result checks; hot per-frame seams (present, pane present, compositor batch, uploads, SDF dispatch, IOSurface export) carry `VkGuard_check` at entry.

---

### Native Pixel Law

Every `CAMetalLayer` `drawableSize` and Vulkan render into the single seam
canvas must use **native hardware pixels**, not logical points.

```c
// CORRECT — multiply point size by the monitor scale factor
int pxW = (int)(w * kx + 0.5f);
int pxH = (int)(h * ky + 0.5f);
Vk_seamSetMaxExtent(pxW, pxH);    // the fixed-buffer seam canvas (built once)

// WRONG — logical points only, blurry on Retina
Vk_seamSetMaxExtent((int)w, (int)h);
```

The seam `CALayer` frame is always set in **logical points** (CoreAnimation
convention), while its `contentsScale` is set to `backingScaleFactor` (e.g.
`2.0` on Retina) so CoreAnimation maps the native physical pixels of the
seam's swapchain to logical points at exact 1:1 screen resolution, preventing
the content from appearing doubled in size. The seam canvas is the window's
only `CAMetalLayer` (the Single-Seam Canvas Law in `darling-framework`):
there are no per-pane surfaces or swapchains.

---

### R3 Graphics Language & Board Compositor Law

#### Definition:
`graphvex` (R3) owns the backend-agnostic graphics language, graphical element
tree and widget compositor: placement, isolated groups, images, filters, masks,
render dependencies and ordered composition into the host-borrowed destination.
This includes composing images produced for a Darling Scene widget. Darling
(R4) is the widget interface: widget semantics, tree construction, layout policy,
input/focus and application/native-window bridges. It submits Graphvex graphics
records; it does not own a competing filter or render compositor.

#### The Why:
Widget rendering is a graphics operation even when its source is a widget tree.
One R3 compositor keeps filters, bounds and backend behavior consistent for all
interfaces. The Vertical Integration Law assigns graphical tree composition to
R3 and widget behavior to R4; include and lifecycle directions are unchanged.

#### The Rule:
1. **`lang/` owns the graphics vocabulary:** `device`, `image`, `filter`,
   `filter_stack`, `compositor`, `graphics_component`, `element_node`,
   `transform`. Each is a `lang/<name>.h` contract with a `<dir>/<name>.c`
   implementation.
2. **One graphics composition owner.** Board-image and widget-element rendering,
   isolation and filter execution belong to R3 Graphvex. R4 Darling constructs
   widgets and supplies their graphical content through R3 contracts. A host
   bridge may orchestrate calls during migration, but cannot duplicate rendering
   semantics; draft R4 compositor files confer no alternate ownership.
3. **Naming marks the layer.** The placement type is `GraphicsComponent`
   (graphvex), never `Component` (darling's own interface type). The container is
   `ElementNode` (graphvex), never darling's container. Same idea, different
   layer, no collision.
4. **Allowlist unchanged.** R4 darling may include graphvex (R3); graphvex
   includes only vexspoke (R2). No reverse edge is created by this law.
5. **AUTO is a sentinel with a per-class equivalence.** `lang/size.h` owns
   `SIZE_AUTO` (the åuto FourCC, `0xE575746F`, negative on every platform).
   A declared `w`/`h` holds either a concrete size or the sentinel; `w ==
   SIZE_AUTO` is the check that swaps in the element's **AUTO equivalence** —
   the size that class defaults to. The base `GraphicsComponent`/`GraphicsPanel`
   equivalence is **0** (a dumb element has no intrinsic content); a `Label`'s
   is its **measured text** (font advances x lines — explicit `\n` plus wrap at
   a concrete width — plus padding), written through
   `GraphicsComponent_setMeasuredSize` so the declared sentinel survives and
   re-resolves every render. AUTO is the **default** for every element. The
   sentinel is never clamped by min/max.
6. **`Property` is the placement bound.** `ui/property.h` owns the rectangle
   every element carries (x/y/w/h plus radius/background/border/shadow/blur):
   placement data, never a widget. Records live in `nio/property_pool` (blocks
   from vexspoke's ForeignMemory, stable addresses), so an element borrows one
   and may share it — aliasing the address is the bind, and `revalidate`
   reflects a shared record everywhere. A `radius > 0` clips children to the
   rounded shape. Widget semantics, input, and focus remain R4. Filter halos are
   not stored by changing this placement rectangle.

---

## 3. Repo-Local Extensions (managed, per the Conflict Triage Law)

;;INTENTION("R3 GPU Driver: Vulkan 1.3/MoltenVK compute & rasterization; ahead-of-time SPIR-V bytecode; native-pixel backing scale.")

---

## 4. Readiness Cross-Reference (the Living Feature Readiness Law)

- Feature readiness matrix tracked in [`../../_repositories/.ecosystem/graphvex.md`](../../_repositories/.ecosystem/graphvex.md) (rendered as `[[graphvex]]` wiki page).

### UI Graphics Source Placement Law

Graphical panel and label implementations live under `src/ui/<kind>/`; their
public vocabulary remains under `src/lang`. Graphvex owns graphical element-tree
composition and filter execution, with implementation under `src/compositor/`.
Editing, focus, widget semantics, layout policy and application behavior remain
in Darling (R4); Graphvex still includes only vexspoke.

### Absolute Rendering & Event Bound Law

#### Definition:
Each element has two public geometric bounds with distinct purposes: its
**event bound** and its **absolute bound**. Neither is an offscreen allocation.

#### The Why:
Rendering effects need extra pixels without moving the widget, its siblings,
anchors or mouse target. Conflating placement with paint crops halos or enlarges
input regions invisibly.

#### The Rule:
1. **Event bound:** the resolved layout/hit rectangle in the declared coordinate
   space. Filters never alter it. Layout changes still update it normally.
   Effective hit eligibility also applies ancestor event clips and shape tests;
   a rectangular broad phase does not replace rounded hit testing.
2. **Absolute bound:** a conservative world-space AABB enclosing this group's
   own paint and contributing descendant paint, after descendant effects,
   child-content clips and ordered element filters. This query describes group
   output before external ancestor composition clips; those constrain its
   contribution at their composition stage. It need not be pixel-tight.
   Ancestor filters belong to the ancestor group's bound, not each child's bound.
3. **Ordered support:** each filter maps preceding output support in stack order.
   Blur halos accumulate; offsets/crops may move or shrink support. Input ROIs,
   preclip support, scratch extents, pixel rounding and damage are internal data,
   not extra public bounds. Unknown support requires conservative full-group
   fallback or explicit rejection, never an invented finite margin.
4. **Absolute origins:** targets retain their world origin. Expanded allocations
   translate coordinates only; never stretch an expanded result back into its
   event rectangle. Native-pixel allocation rounds support outward.
5. **Clip stages:** rounded child-content clipping remains the default when
   radius is positive. It clips children before the element's own filter stack,
   not its own effect halo. Hard containment needs an explicit output clip.
   Allocation edges are not semantic clips. Required source pixels outside the
   visible region survive culling when they can scatter into visible output.
6. **Damage:** movement, removal, filters and descendant changes invalidate old
   and new absolute output support; backdrop reads add rendering dependencies.

### Ordered Filter & Scatter Composition Law

#### Definition:
The widget compositor executes ordered filter stacks over isolated graphics
groups, with scatter-first spatial filtering and explicit resolve semantics.

#### The Rule:
1. **Three scopes:** Backdrop filters consume the already-composed painter-order
   scene prefix, excluding self/later siblings. Foreground filters consume content
   and children, excluding own box decoration. Element filters consume the whole
   assembled element, including filtered backdrop, decoration and children.
   Filter a group once, not each overlapping child independently.
2. **Compact tokens:** every filter token is `ID16 | payload48` in a 64-bit value.
   The canonical operation table specifies inline parameter decoding or a typed
   complex-filter-pool index. Payloads never encode raw pointers. Unknown IDs,
   unsupported parameters and exhausted resources reject explicitly and preserve
   previous valid state. Pool records outlive all stack/submitted consumers.
3. **Order:** stacks execute left-to-right. No silent reordering of contrast,
   HSV, noise or blur. Safe pointwise fusion preserves declared results.
4. **Color:** composition and blur use linear-light premultiplied RGBA. Nonlinear
   color operations define their working space, safely unpremultiply, operate,
   and premultiply again; alpha-zero and alpha-preservation behavior are explicit.
5. **Scatter:** weighted contributions add into isolated float accumulation,
   followed by resolve and one ordinary source-over group composition. Raster
   additive blending is valid; compute writes need supported atomics or exclusive
   output ownership plus dependencies. Graphics blend state does not protect
   compute stores. Device/format capabilities must be queried, never assumed.
6. **Normalization:** source-energy conservation and constant-field preservation
   are different contracts. Fixed/variable kernels declare finite support, edge
   policy and weight-domain semantics. Transparent-domain samples count when
   destination normalization is requested; normalization must not cancel intended
   transparent edge fade. Normalize premultiplied RGB and alpha together.
7. **Backdrop replacement:** filtered backdrop replaces its masked prefix region;
   do not composite that same original backdrop twice. Snapshot/version the prefix
   before scheduling dependent passes. Sampling ROI and output mask differ.
8. **Incremental proof:** each operation has a CPU numeric reference and scoped
   backend evidence. Shader compilation alone is not runtime support. Noise seeds
   and coordinates are stable across allocation changes; animation is opt-in.

### Independent Scene Cadence Law

Graphvex widget composition and scene-image production have independent logical
schedules. Separate OS threads are optional, not implied. The compositor consumes
the newest completed scene image without forcing a scene tick or waiting for the
next image. Versions do not replace GPU synchronization: images and parameter
records remain alive until consumers finish, including resize/close. Widget
changes and completed scene versions invalidate composition independently;
scene work must not monopolize widget scheduling. Current host bridges may drive
composition while ownership migrates; unfinished scheduler/GPU integration is
reported as a gap, never inferred from this law.
