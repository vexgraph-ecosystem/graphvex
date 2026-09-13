#!/bin/sh
# build_shaders.sh — recompile graphvex's GLSL sources into the SPIR-V
# blobs loaded at runtime (loadSpvAny -> shader/spv/*.spv, staged to
# ${CMAKE_BINARY_DIR}/spv/ by the top-level CMake build).
#
# Layout: frag/ (*.frag), vert/ (*.vert), comp/ (*.comp) sources;
# spv/ holds the compiled blobs (<name>_<stage>.spv, bare <name>.spv
# for compute). All 10 shaders live here — base + UI alike.
#
# Requires: glslangValidator (brew install glslang).
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"

glslangValidator -V "$DIR/vert/texture_quad.vert" -o "$DIR/spv/texture_quad_vert.spv"
glslangValidator -V "$DIR/frag/texture_quad.frag" -o "$DIR/spv/texture_quad_frag.spv"
glslangValidator -V "$DIR/frag/layer_quad.frag" -o "$DIR/spv/layer_quad_frag.spv"
glslangValidator -V "$DIR/vert/text_sdf.vert" -o "$DIR/spv/text_sdf_vert.spv"
glslangValidator -V "$DIR/frag/text_sdf.frag" -o "$DIR/spv/text_sdf_frag.spv"
glslangValidator -V "$DIR/comp/sdf_jfa.comp" -o "$DIR/spv/sdf_jfa.spv"
glslangValidator -V "$DIR/comp/sdf_combine.comp" -o "$DIR/spv/sdf_combine.spv"
glslangValidator -V "$DIR/vert/hello_triangle.vert" -o "$DIR/spv/hello_triangle_vert.spv"
glslangValidator -V "$DIR/frag/hello_triangle.frag" -o "$DIR/spv/hello_triangle_frag.spv"
glslangValidator -V "$DIR/vert/solid_quad.vert" -o "$DIR/spv/solid_quad_vert.spv"
glslangValidator -V "$DIR/frag/solid_quad.frag" -o "$DIR/spv/solid_quad_frag.spv"

echo "shaders: spv refreshed"
