# graphvex — R3 graphics foundation (supervised by R1 hotcwap)
all graphics computing lies here, bespoken

## Boilerplate (shared, not vendored)
Foundation layer (the Standalone Autonomy Law): graphvex depends solely on `vexspoke`.
Shared infrastructure resolves from `vexspoke/src` via the PUBLIC link —
never copied here, so include strings stay single-sourced:

| include | resolves to |
|---|---|
| `annotation/overview.h` | `vexspoke/src/annotation/overview.h` |
| `c23/constructor.h`, `c23/overload.h` | `vexspoke/src/c23/*` |
| `oop/type.h` (`Type_*`, `ID_*`, `ARCH_*`) | `vexspoke/src/oop/type.h` |
| `nio/mem.h`, `struct/*`, `lang/*` | `vexspoke/src/...` |

graphvex-owned code lives under `src/` in subsystem folders — `buffer/`
(off-heap 2D raster: `Buffer`, `ColorBuffer`, `Depth/Stencil/Frame/Height/
Normal/Shadow`), `font/`, `io/`, `vulkan/`, plus `src/graphvex/` core;
new subsystem folders here must use filenames unique across the include
path — never shadow a `vexspoke` filename.

### UI graphics primitives

`src/ui/panel/graphics_panel.c` and `src/ui/label/label.c` contain the graphical panel and label implementations. Their stable public contracts remain `lang/graphics_panel.h` and `lang/label.h`. Input, editing, and widget composition remain in Darling; this directory adds no downstream dependency.
