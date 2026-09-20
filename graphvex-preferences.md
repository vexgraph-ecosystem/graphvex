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
| **SPIR-V Shader Deployment Law** | R3 GPU Driver | Mandatory for `graphvex` |
| **Ecosystem Vulkan Safety Nets Law (Determinism + Tree-Shaken Truth)** | R3 GPU Driver | Mandatory for `graphvex` |
| **Native Pixel Law** | R3 GPU Driver | Mandatory for `graphvex` |

## 2. Exclusive Repo-Local Laws (FULL PROSE RESTATEMENT)

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
4. CWD-relative paths (`spv/<name>`, `src/vulkan/spv/<name>`)

The top-level `vexgraph` CMake build staging copies all `.spv` blobs from `../graphvex/shader/spv/` into `${CMAKE_BINARY_DIR}/spv/` so all subsystems discover their shaders seamlessly.

---

### Ecosystem Vulkan Safety Nets Law (Determinism + Tree-Shaken Truth)

#### Definition:
Vulkan is the one subsystem whose failure surfaces at a *later* boundary than its cause: MoltenVK reports a lost device only at the next API touch, so a `DEVICE LOST` logged at acquire means the damage happened at an earlier call. Every Vulkan-touching file in the ecosystem therefore carries the same two-layer contract: **deterministic handling** (same input sequence ⇒ same seam name, same decision, same log line) and **seam guards** (every driver-facing function validates the health chain before touching the driver). The net spans ALL Vulkan touchpoints — not just the R1 present chain: hotcwap `vulkan.c` (present), `vk_pane.c` (pane presents), graphvex `texture.c`, `sdf_gpu.c`, `vk_scene.c`, `vk_view.c`, `vk_iosurface.c`, darling `compositor.c` (re-record batch). Adding a new file that calls the driver without joining the net is a defect (the repository's class registry alone is too weak — driver calls span repos).

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

All `CAMetalLayer` `drawableSize`s and Vulkan renders into pane chains must use
**native hardware pixels**, not logical points.

```c
// CORRECT — multiply point size by the monitor scale factor
int pxW = (int)(rect.z * kx + 0.5f);
int pxH = (int)(rect.w * ky + 0.5f);
VkPane_register(layer, pxW, pxH, owner);

// WRONG — logical points only, blurry on Retina
VkPane_register(layer, (int)rect.z, (int)rect.w);
```

The `CALayer` frame is always set in **logical points** (CoreAnimation convention).
`contentsScale` on a Metal pane layer must be set to `backingScaleFactor` (e.g. `2.0` on Retina)
so CoreAnimation maps the native physical pixels of the pane's swapchain to logical points at
exact 1:1 screen resolution, preventing the content from appearing doubled in size.

---

## 3. Repo-Local Extensions (managed, per the Conflict Triage Law)

;;INTENTION("R3 GPU Driver: Vulkan 1.3/MoltenVK compute & rasterization; ahead-of-time SPIR-V bytecode; native-pixel backing scale.")

---

## 4. Readiness Cross-Reference (the Living Feature Readiness Law)

- Feature readiness matrix tracked in [`../../_repositories/.ecosystem/graphvex.md`](../../_repositories/.ecosystem/graphvex.md) (rendered as `[[graphvex]]` wiki page).
