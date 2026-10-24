# graphvex — Repo-Local Living Preferences
> Repo-local preferences governed by the Living Documentation Law.
> Universal constitution: `gist.md` is the compressed law list; `PREFERENCES.md` is its rationale.

## 0. Constitution Link (supreme)
- [gist.md](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a#file-gist-md) — the compressed law list, mirrored in this workspace at `../../../metadata/gists/preferences/gist.md`. It is the only normative law text; `../../../PREFERENCES.md` carries the rationale and is never ingested whole. Numbers are slice addresses inside one file, never law identity: cite by Title.
- All universal laws bind this repository. Read one on demand from the workspace root: `./tools/prefs law "<title>"`, `./tools/prefs section "<section>"`, `./tools/prefs repo <name>`.
- This document codifies **exclusive** preferences for `graphvex` (R3 GPU Driver). R3 borrows Vexspoke CPU computation/behavior and Relational Engine memory/storage contracts. Canonical IO/NIO includes and the native Memory implementation now come from RE by default; allocation semantics remain compatible, not rewritten into Rust. GPU shaders, dispatch, capabilities and synchronization stay Graphvex-owned.

## 1. Exclusive Preferences Binding Matrix

| Law Title | Scope | Enforcement |
| :--- | :--- | :--- |
| **Unified Graphics Abstraction Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Strict 0xRRGGBBAA Color Law** | R3 GPU Driver / Universal | Mandatory for `graphvex` |
| **SPIR-V Shader Deployment Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Ecosystem Vulkan Safety Nets Law (Determinism + Tree-Shaken Truth)** | R3 GPU Driver | Mandatory for `graphvex` |
| **Seam Canvas Pixel Mapping Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **R3 Graphics Language & Board Compositor Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **UI Graphics Source Placement Law** | R3 graphics primitives | Mandatory for `graphvex` |
| **Absolute Rendering & Event Bound Law** | R3 element geometry/composition | Mandatory for `graphvex` |
| **Ordered Filter & Scatter Composition Law** | R3 widget compositor | Mandatory for `graphvex` |
| **Independent Scene Cadence Law** | R3 composition/image handoff | Mandatory for `graphvex` |

## 2. Exclusive Repo-Local Laws (FULL PROSE RESTATEMENT)

### Unified Graphics Abstraction Law

**Definition:** backend-agnostic graphics primitives (`Image`, `Fence`, `Semaphore`, `Swapchain`, `CommandBuffer`, `CommandQueue`, `Surface`, `GraphicsLayer`) decoupling app/UI rendering from Vulkan, Metal, Direct3D.
- `Image`/`Swapchain` keep tightly-packed CPU shadow blocks — zero steady-state allocation (RGBA8 at width * height * 4 Bytes) from the `vexspoke` arena (`Memory_alloc`); high-level ops touch metadata and mapped buffers, never heavy driver state transitions during recording.
- GPU-GPU ordering (`Semaphore`) advances monotonically, strictly probed, never blocking CPU threads.
- Surfaces/swapchains bridge into OS compositing layers (`CAMetalLayer` via `GraphicsLayer`, `VkIOSurface`, window hooks) by native hardware pixel scaling (the Native Pixel Law); no backend API types leak across repo boundaries. On Apple the host creates the IOSurface; `VulkanBackend_bindSurface` imports it (`VK_EXT_metal_objects`, `VkImportMetalIOSurfaceInfoEXT`) so the GPU writes the exact Bytes CoreAnimation composites — zero-copy, no readback, no `VkSwapchainKHR`.

**Why:** hand-coupled Vulkan/Metal fragments platforms, dangles command buffers across hot reloads, locks up drivers.

**Rule:**
1. Rendering operates exclusively on those primitives; never expose driver handles (`VkImage`, `VkDevice`, `MTLDevice`) in client headers.
2. Every fence join and timeline wait obeys `FENCE_WAIT_TIMEOUT_NS` (100ms max, the Bounded Wait Law); indefinite GPU waits (`UINT64_MAX`) prohibited.
3. Every image drawn into a destination rect resolves through `Image_fitRect` + `Graphics_drawImageFit` — never a bespoke widget transform. Families: `IMAGE_FIT_STRETCH` (whole image stretched into dst), `IMAGE_FIT_CONTAIN` (fit inside, centered, letterboxed), `IMAGE_FIT_COVER` (cover, centered, overflow cropped), `IMAGE_FIT_WINDOW` (source-pixel window, anchored, scaled to fill dst). WINDOW is widget-shaped: at most one of `windowW`/`windowH` drives it, the other derives from dst aspect; unset means dst pixels (1:1); the window clamps to the image. Scale = `dst ÷ window`; a 300-px window in a 600-px dst draws at 2x; fit math pure, R3-owned — widgets call, never reimplement.

### Strict 0xRRGGBBAA Color Law

**Definition:** all packed 32-bit integer colors are strictly **`0xRRGGBBAA`** (Red bits 31–24, Green 23–16, Blue 15–8, Alpha 7–0); channel order in continuous buffers, pixel shadow arrays (`Image`) and color framebuffers (`ColorBuffer`) is strictly monotonic: 0 Red, 1 Green, 2 Blue, 3 Alpha (each 0..255).

**Why:** fragmented encodings cause visual bugs, inverted transparency, fragile shift arithmetic.

**Rule:**
1. All color constants and parameter integers are `0xRRGGBBAA`; legacy `0xAARRGGBB`/`0xBBGGRRAA` prohibited. Opaque Black `0x000000FFu` (never `0xFF000000u`); Opaque White `0xFFFFFFFFu`; Fully Transparent / Clear `0x00000000u`.
2. Unpacking uses those monotonic shifts: `Red = (color >> 24) & 0xFFu` … `Alpha = color & 0xFFu`.
3. Hardware scanout needs (native BGRA display swapchains) live only inside driver-level presentation adapters (`VkIOSurface`, swapchain blits, final composite shaders); application logic, brushes, meshes, buffers and textures stay strictly RGBA.

### SPIR-V Shader Deployment Law

GLSL lives in Graphvex's `src/shaders`; umbrella build `../../../tools/b` compiles ahead-of-time SPIR-V into external build-state `shader/`; existing stage directories remain supported. New modules separate responsibilities (`ui`, `shadow`, `compositor`, `filters`, `light`) — never one unbounded shader spanning widget composition and scene lighting; shared color/alpha/coordinate helpers have one implementation. Darling and vexspoke own no shader blobs. A source module is not runtime support until a backend pipeline executes it with stated automated evidence.

**Runtime Shader Resolution Protocol** — `loadSpvAny` precedence:
1. `ANTI_SPV_DIR` / `VEX_SPV_DIR` (build-time staging `${CMAKE_BINARY_DIR}/spv/`, populated from `/shader/spv/`)
2. `<exe_dir>/spv/<name>` (adjacent deployment)
3. `<exe_dir>/../Resources/spv/<name>` (macOS `.app`)
4. CWD-relative paths (`spv/<name>`, `src/_old/vulkan/spv/<name>`)

Umbrella build = shader build truth; IDE adapters delegate to it. Compiler errors propagate, generated outputs stay outside source, shader dependencies invalidate consumers. Pointwise fusion has bounded instruction/variant budgets and preserves order; spatial filters are explicit pipeline boundaries.

### Ecosystem Vulkan Safety Nets Law (Determinism + Tree-Shaken Truth)

**Definition:** every Vulkan-touching file carries deterministic handling — same input sequence ⇒ same seam name, decision, log line. Net spans ALL Vulkan touchpoints: hotcwap `vulkan.c` present, graphvex `texture.c`, `sdf_gpu.c`, `vk_scene.c`, `vk_view.c`, `vk_iosurface.c`, seam canvas in `vk_instance.c`, darling `compositor.c`. A driver-calling file not joining the net is a defect (its one type registry law is a too-weak enforcement point — driver calls span repos).

**Why:** the report site must equal the cause site.

**Rule:**
1. `VkGuard_check(seam, device, queue, deviceLost)` lives once in `graphvex/src/vulkan/vk_guard.h` (header-only, no struct — Single Class Per File Law; allowlist-safe: graphvex→own, hotcwap→graphvex, darling→graphvex). `false` on lost/null device; null queue only on device-resource seams (create/destroy/record) — any submit/fence/present seam passes its queue (whole chain). Transient-failure degrade: return `false`, keep dirty state, retry next tick (the Bounded Wait Law + Cold-Strict, Hot-Minimal Validation Law); never crash, never UB, never a wedged wait.
2. Under `NDEBUG` the guard is a macro no-op — zero calls, zero branches, unevaluated arguments, not counted against hot-minimal branch budgets (the Conflict Triage Law). Every file's `;;OVERVIEW` lists which functions carry the net.
3. One fixed decision table for every wait/acquire/submit/present: success advances; timeout-with-signal recovers; timeout-unsignaled drops (dirty state intact), retry next tick; `VK_ERROR_DEVICE_LOST` latches once at the true site — `presentDeviceLost(where)` is hotcwap single-owned, downstream repos never re-implement, `Vk_isDeviceLost()` queryable wherever the allowlist permits — short-circuiting every later pass.
4. Report site is never the cause: on `VK_ERROR_DEVICE_LOST`, run `MVK_CONFIG_LOG_LEVEL` and read MoltenVK's Metal error (`MTLCommandBuffer` code + message) BEFORE touching code — `MTLCommandBufferErrorInternal`/`PageFault`/`Timeout` distinguishes a usage defect from a GPU power/restart event. Paste both lines; never "fix the acquire" until MoltenVK says the acquire is the cause.
5. Handles nulled in the same teardown pass (Vertical Integration Law (Teardown)) so a stale guard catches a real lifecycle defect. Cold resource creation (images/textures/framebuffers/views) keeps full result checks; hot per-frame seams (present, pane present, compositor batch, uploads, SDF dispatch, IOSurface export) carry `VkGuard_check` at entry.

### Seam Canvas Pixel Mapping Law

The universal Native Pixel Law owns the hardware-pixel rule: `CAMetalLayer` `drawableSize` and every Vulkan render into the seam canvas, never logical points. Example grammar:

```c
int pxW = (int)(w * kx + 0.5f);
int pxH = (int)(h * ky + 0.5f);
Vk_seamSetMaxExtent(pxW, pxH);    // the fixed-buffer seam canvas (built once)
Vk_seamSetMaxExtent((int)w, (int)h); // never logical points — blurry on Retina
```

Repo-local delta: seam `CALayer` frame in logical points, `contentsScale` = `backingScaleFactor` (`2.0` on Retina) so native pixels map 1:1; the seam canvas is the window's only `CAMetalLayer` (the One-Seam Canvas Window Architecture Law, owned by `hotcwap`); no per-pane surfaces or swapchains.
### R3 Graphics Language & Board Compositor Law

**Definition:** `graphvex` (R3) owns the backend-agnostic graphics language, graphical element tree and widget compositor: placement, isolated groups, images, filters, masks, render dependencies, ordered composition into the host-borrowed destination (including Darling Scene widget images). Darling (R4) is the widget interface; it submits Graphvex graphics records, never a competing filter/render compositor.

**Why:** one R3 compositor keeps filters, bounds and backend behavior consistent for all interfaces.

**Rule:**
1. `lang/` owns the graphics vocabulary: `device`, `image`, `filter`, `filter_stack`, `compositor`, `graphics_component`, `element_node`, `transform` — each a `lang/<name>.h` contract + `<dir>/<name>.c` implementation.
2. Board-image/widget-element rendering, isolation and filter execution are R3 Graphvex-owned; R4 Darling constructs widgets, supplies graphical content through R3 contracts. Host bridges may orchestrate during migration but never duplicate rendering semantics; draft R4 compositor files confer no alternate ownership.
3. Naming marks the layer: placement type `GraphicsComponent` (graphvex), never `Component` (darling's interface); container `ElementNode` (graphvex), never darling's container.
4. Allowlist: R4 darling may include graphvex (R3); graphvex borrows either R2 public contract, never R1/R4/R5 headers; no reverse edge or storage migration implied.
5. AUTO = sentinel with per-class equivalence; `lang/size.h` owns `SIZE_AUTO` (the åuto FourCC `0xE575746F`, negative on every platform); a declared `w`/`h` holds a concrete size or the sentinel; `w == SIZE_AUTO` resolves to the element's AUTO equivalence — its class default. Base `GraphicsComponent`/`GraphicsPanel` equivalence is **0**; `Label` = measured text (font advances x lines — explicit `\n` + wrap at concrete width — plus padding), written via `GraphicsComponent_setMeasuredSize` so the sentinel survives, re-resolving each render. AUTO is every element's default; never clamped by min/max.
6. `Property` = placement bound; `ui/property.h` owns the rectangle every element carries (x/y/w/h + radius/background/border/shadow/blur) — placement data, never a widget. Records live in `nio/property_pool` (vexspoke ForeignMemory blocks, stable addresses); an element borrows one, may share it — the address alias is the bind, `revalidate` reflects a shared record everywhere. `radius > 0` clips children to the rounded shape; widget semantics, input and focus remain R4; filter halos are never stored by changing this rectangle.

## 3. Repo-Local Extensions (managed, per the Conflict Triage Law)

;;INTENTION("R3 GPU Driver: Vulkan/MoltenVK compute & rasterization; device.c currently requests Vulkan API 1.1; ahead-of-time SPIR-V bytecode; native-pixel backing scale.")

## 4. Readiness Cross-Reference (the Living Documentation Law)

- Feature readiness matrix: [graphvex](https://gist.github.com/vex-graph/6943f92acb931b25dad1073c46da6ce7#file-graphvex-md).
- Open blockers and deferred decisions: [ecosystem blockers Gist](https://gist.github.com/vex-graph/e921fa188eebbd0c68c4e59646109887).

### UI Graphics Source Placement Law

Graphical panel/label implementations live under `src/ui/<kind>/`, public vocabulary under `src/lang`; Graphvex owns element-tree composition and filter execution under `src/compositor`. Editing, focus, layout policy, application behavior remain Darling's (R4); Graphvex borrows R2 public contracts only, never Darling headers.

### Absolute Rendering & Event Bound Law

**Definition:** each element has two public geometric bounds — **event bound** and **absolute bound**; neither is an offscreen allocation.

**Why:** effects need extra pixels without moving the widget, siblings, anchors or mouse target.

**Rule:**
1. **Event bound:** resolved layout/hit rectangle in the declared coordinate space; filters never alter it, layout changes still update it. Effective hit eligibility also applies ancestor event clips and shape tests.
2. **Absolute bound:** conservative world-space AABB enclosing this group's and its descendants' paint after their effects, child-content clips and ordered element filters — group output before external ancestor composition clips; need not be pixel-tight; ancestor filters belong to the ancestor's bound, not each child's.
3. **Ordered support:** each filter maps preceding output support in stack order — blur halos accumulate; offsets/crops may move or shrink it. Input ROIs, preclip support, scratch extents, pixel rounding, damage = internal data, not extra public bounds. Unknown support requires conservative full-group fallback or explicit rejection, never an invented finite margin.
4. **Absolute origins:** targets retain their world origin; expanded allocations translate coordinates only, never stretching results back into event rectangles. Native-pixel allocation rounds support outward.
5. **Clip stages:** rounded child-content clipping is default at positive radius; it clips children before the element's own filter stack, not its effect halo. Hard containment needs an explicit output clip; allocation edges are not semantic clips. Source pixels outside the visible region survive culling when they can scatter into visible output.
6. **Damage:** movement, removal, filters and descendant changes invalidate old and new absolute output support; backdrop reads add rendering dependencies.

### Ordered Filter & Scatter Composition Law

**Definition:** the widget compositor executes ordered filter stacks over isolated graphics groups.

**Rule:**
1. Backdrop filters consume the composed painter-order scene prefix, excluding self/later siblings; foreground filters consume content/children, excluding own box decoration; element filters consume the assembled element, including filtered backdrop, decoration and children — filter a group once, never per overlapping child.
2. Filter token = `ID16 | payload48`. `filter/filter_type.h` owns canonical operation IDs `FILTERNAME_ID` (e.g. `GAUSSIAN_BLUR_ID`), hex `#define` constants; existing numeric IDs stable, legacy aliases answer to the same table. Constructors live in `filter/filter_functions.h`; a constructor/ID alone is vocabulary, not effect execution. Table specifies inline parameter decoding or a typed complex-filter-pool index; payloads never encode raw pointers. Unknown IDs, unsupported parameters, exhausted resources reject explicitly preserving prior valid state; pool records outlive all stack/submitted consumers.
3. Stacks execute left-to-right; no silent reordering of contrast, HSV, noise or blur; safe pointwise fusion preserves declared results.
4. Composition and blur use linear-light premultiplied RGBA; nonlinear color operations define their working space, safely unpremultiply, operate, premultiply again; alpha-zero/alpha-preservation behavior explicit.
5. Weighted contributions add into isolated float accumulation, then resolve into one ordinary source-over group composition. Raster additive blending is valid; compute writes need supported atomics or exclusive output ownership + dependencies — graphics blend state does not protect compute stores. Device/format capabilities: queried, never assumed.
6. Source-energy conservation and constant-field preservation are distinct contracts. Fixed/variable kernels declare finite support, edge policy and weight-domain semantics. Transparent-domain samples count for destination normalization; normalization must not cancel intended transparent edge fade. Normalize premultiplied RGB and alpha together.
7. Filtered backdrop replaces its masked prefix region; never composite the original backdrop twice. Snapshot/version the prefix before dependent passes; sampling ROI and output mask differ.
8. Every operation has a CPU numeric reference + scoped backend evidence; shader compilation alone is not runtime support. Noise seeds and coordinates stay stable across allocation changes; animation is opt-in.
9. Vulkan filter paths and GPU filter galleries execute shader pipelines, never CPU filtering disguised as GPU presentation; CPU numeric math is an oracle or named legacy reference, not a GPU fallback. Image/Picture upload/readback is data transport, never zero-copy. Prove actual GPU pixels at the owner seam; appearance approval is the user's.

### Independent Scene Cadence Law

Widget composition and scene-image production run on independent logical schedules; separate OS threads are optional, not implied. The compositor consumes the newest completed scene image (the universal constitution's Present-On-Demand Law: idle composition presents nothing) without forcing a scene tick or waiting for the next image. Images and parameter records remain alive until consumers finish, resize/close included. Widget changes and completed scene versions invalidate composition independently; scene work must not monopolize widget scheduling. Current host bridges may drive composition during ownership migration; unfinished scheduler/GPU integration is a stated gap, never inferred.
