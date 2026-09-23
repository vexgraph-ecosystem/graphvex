# graphvex — Repo-Local Living Preferences
> Exclusive repository-level preferences (the Living Preferences Law).
> Universal Supreme Constitution: preferences.md (vexspoke).

;;SYNC("mirrors ecosystem/vexspoke/preferences.md @ 2026.09-universal")

## 0. Constitution Link (supreme)
- [preferences.md](https://github.com/vexgraph-dev/vexspoke/blob/main/preferences.md) (canonical, vexspoke) — accessible locally at ../../preferences.md
- All universal laws in `preferences.md` are mandatory and binding across the ecosystem.
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

## 2. Exclusive Repo-Local Laws (FULL PROSE RESTATEMENT)

### Unified Graphics Abstraction Law

#### Definition:
`graphvex` provides unified, backend-agnostic graphics primitives (`Image`, `Fence`, `Semaphore`, `Swapchain`, `CommandBuffer`, `CommandQueue`, `Surface`, and `GraphicsLayer`) that decouple high-level application and UI rendering pipelines from concrete hardware APIs (Vulkan, Metal, Direct3D).
- **CPU Shadow & Zero Steady-State Allocation:** Resource handles (`Image`, `Swapchain`) maintain tightly-packed CPU shadow memory blocks (e.g. RGBA8 at width * height * 4 bytes) allocated through the `vexspoke` arena (`Memory_alloc`). High-level operations manipulate metadata and memory-mapped buffers without invoking heavy GPU driver state transitions during recording.
- **Synchronization Contracts:** GPU-CPU join points (`Fence`) enforce a hard 100ms ceiling under the Bounded Wait Law (`FENCE_WAIT_TIMEOUT_NS`), preventing deadlocks on dropped frames. GPU-GPU timeline ordering (`Semaphore`) advances monotonically and is strictly probed, never blocking CPU threads.
- **Dual Presentation Seam:** Display surfaces and swapchains seamlessly bridge into OS-level compositing layers (CAMetalLayer via `GraphicsLayer`, `VkIOSurface`, and window surface hooks) using native hardware pixel scaling (the Native Pixel Law) without leaking backend API types across repository boundaries.

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

SPIR-V shaders (`.spv`) are centralized under `../graphvex/shader/` — the single source of truth, laid out by stage:
- `shader/frag/`, `shader/vert/`, `shader/comp/` — GLSL sources (base: `hello_triangle`, `solid_quad`; UI: `texture_quad`, `text_sdf`; compute: `sdf_jfa`, `sdf_combine`).
- `shader/spv/` — compiled blobs (`<name>_<stage>.spv`, bare `<name>.spv` for compute), rebuilt via `shader/build_shaders.sh` (requires `glslangValidator`).
- (Legacy note: sources lived in `hotcwap/vulkan/shaders/` + `darling/vulkan/shaders/`, blobs in per-subsystem `spv/` mirrors — all stale, pending deletion.)
- **`vexspoke` / `darling`**: Own zero blobs; they load via the resolution protocol below. Per-subsystem `spv/` directories, if still present, are stale mirrors pending deletion.

**Runtime Shader Resolution Protocol**:
The runtime loader (`loadSpvAny`) must search in this exact precedence order:
1. `ANTI_SPV_DIR` / `VEX_SPV_DIR` (build-time staging directory `${CMAKE_BINARY_DIR}/spv/`, populated from `../graphvex/shader/spv/`)
2. `<exe_dir>/spv/<name>` (adjacent deployment)
3. `<exe_dir>/../Resources/spv/<name>` (macOS `.app` bundle)
4. CWD-relative paths (`spv/<name>`, `src/_old/vulkan/spv/<name>`)

The top-level `vexgraph` CMake build staging copies all `.spv` blobs from `../graphvex/shader/spv/` into `${CMAKE_BINARY_DIR}/spv/` so all subsystems discover their shaders seamlessly.

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
`graphvex` (R3) hosts the **graphics language** (`lang/`): the backend-agnostic
vocabulary every layer speaks — `Device` (the dialect registry), `Image`,
`Filter` + `FilterStack` (the ordered filter chain), the **board compositor**,
and the **element placement vocabulary** (`GraphicsComponent` + `ElementNode` +
`Transform`, the origin/anchor/pivot dials). These are generic graphics
primitives, not UI widgets: the board compositor folds the window's board images
into one seam image and runs the filter chain; the placement vocabulary places
any rect — a board, a scene object, or a UI element — inside a parent rect.

The **UI toolkit proper** — panels, widgets, input, the widget tree, and the
*UI* compositor that paints it — remains R4 `darling-framework` (the Vertical
Integration Law).

#### The Why:
The Vertical Integration Law names "compositor" under R4, but there are two
compositors, and conflating them inverts the dependency. The **board compositor**
is a pure image operation (over-composite + filter chain) the seam needs; the
origin/anchor/pivot math is ONE vocabulary that the compositor, scenes, and UI
all place with. Duplicating either per layer drifts. So the placement vocabulary
and the board compositor live at R3 (the language), and darling becomes the
interface that consumes them. This is a managed exception per the Conflict
Triage Law: the Tier-1/Tier-2 invariants (Vertical Integration allowlist) are
preserved — R4 may `#include` graphvex; graphvex never includes R4.

#### The Rule:
1. **`lang/` owns the graphics vocabulary:** `device`, `image`, `filter`,
   `filter_stack`, `compositor`, `graphics_component`, `element_node`,
   `transform`. Each is a `lang/<name>.h` contract with a `<dir>/<name>.c`
   implementation.
2. **Two compositors, two layers.** The **board compositor** (R3, image collage
   + filter chain) is graphvex; the **UI compositor** (widget-tree paint) stays
   R4 darling.
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

---

## 3. Repo-Local Extensions (managed, per the Conflict Triage Law)

;;INTENTION("R3 GPU Driver: Vulkan 1.3/MoltenVK compute & rasterization; ahead-of-time SPIR-V bytecode; native-pixel backing scale.")

---

## 4. Readiness Cross-Reference (the Living Feature Readiness Law)

- Feature readiness matrix tracked in [`../../_repositories/.ecosystem/graphvex.md`](../../_repositories/.ecosystem/graphvex.md) (rendered as `[[graphvex]]` wiki page).
