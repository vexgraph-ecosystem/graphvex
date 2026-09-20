# Contributions & Engineering Manifesto (graphvex)

This project is a strictly solo development process conducted in tight pair-programming partnership with an AI coding assistant.

It serves as an architectural manifesto for **Level 2/Level 3 GPU Compute and Graphics Subsystems**: high-throughput Vulkan pipelines, 128-triangle meshlet clusters, signed distance field (SDF) rasterization, and lockless GPU synchronization in pure C23.

---

## 1. The AI-First Architecture Manifesto & Boilerplate Defense

This codebase strictly enforces the verbose, explicit boilerplate required across the `vexgraph` ecosystem:
- Strict prohibition of arrow syntax (`p->field` is banned; only explicit `(*p).field` is permitted).
- Single Class Per File (the Java Law: one public `typedef struct` per `.h`/`.c` pair).
- Arity-overloaded explicit constructor dispatch macros (`Class_0()`, `Class_1()`).
- Complete, symmetric getters and setters for all struct fields.
- Strict dest-last parameter ordering `(a, b, dest)`.
- Two-layer member access cap (`(*layer1).layer2` maximum).
- Exhaustive `;;OVERVIEW` blueprints mirrored at the top of every implementation file.

### Why the Boilerplate Exists
This boilerplate is **not** an accident, nor is it a misunderstanding of idiomatic C. It is an intentional, machine-verifiable scaffold built specifically for **AI-Human Pair Systems Programming**:
1. **Machine Comprehension**: By eliminating `->` and isolating classes to single files, an AI coding agent can track GPU descriptor bindings, push constants, and synchronization fences with flawless mechanical rigor and zero aliasing.
2. **Explicit Memory Boundaries**: `(*ptr).field` makes every memory indirection and GPU buffer offset unmistakable.
3. **AI-Maintained Rigor**: The AI agent authors and maintains the dense boilerplate, allowing human architectural focus to be spent on Vulkan command recording, mesh clustering, and compute shader dispatch.

---

## 2. Sanity Warning for External Contributors

> [!WARNING]
> **SANITY NOTICE FOR EXTERNAL CONTRIBUTORS**
> This repository is not designed for traditional C conveniences, casual hacking, or stylistic shortcuts. It is an unapologetic, machine-verifiable manifesto of AI-augmented systems architecture.
>
> **If you do not approve of this architecture or cannot find peace with this philosophy, consider leaving this repository for your own sanity.**
>
> We do not accept Pull Requests, issues, or unsolicited stylistic refactors attempting to re-introduce `->`, combine multiple pipeline classes into one file, or bypass explicit setters/getters. Upstream is maintained exclusively by the author and the AI agent.

---

## 3. Supreme Living Document: `preferences.md` & Repo-Local Preferences

All architectural rules and style invariants are governed by the central constitution:

- **[preferences.md](https://github.com/vexgraph-dev/vexspoke/blob/main/preferences.md)** (tracked in `vexspoke`, accessible locally at `../../preferences.md`)
- **[graphvex-preferences.md](graphvex-preferences.md)** (repo-local mirror binding graphvex)

Whenever preferences or conventions evolve, `preferences.md` and `graphvex-preferences.md` are updated and committed locally in the same cycle (the Living Preferences Law / Zero Drift).

---

## 4. `graphvex` Architectural Invariants

| Invariant | Specification |
| :--- | :--- |
| **Zero Steady-State Allocation** | GPU command pools, descriptor sets, and staging buffers are pre-allocated during init; zero runtime `malloc` during render loops (per the Data-Oriented Storage Law). |
| **Dest Last Parameter Order** | Vector/matrix math and raster operations always place destination buffers last: `Mat4_multiply(left, right, dest)` (per the Dest-Last Law). |
| **Bounded Queue & Fence Waits** | Vulkan queue and fence waits must specify bounded timeouts (e.g. 100ms) with graceful frame-drop fallbacks; never wait `UINT64_MAX` on worker joins (per the Bounded Wait Law). |
| **Teardown Reverse Order** | Destroy pipelines, render passes, framebuffers, and swapchain images top-down before destroying device or instances. `Memory_freeAll` runs strictly last (per the Teardown Order Law). |
