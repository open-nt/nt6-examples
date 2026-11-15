#!/bin/sh
# MIT License - see LICENSE
#
# Compiles the ray tracing shaders of the rtriangle example to SPIR-V with glslc (from shaderc, in the Vulkan SDK or
# the glslc package). The .spv files are checked in, so building Nt needs no shader compiler.
set -e
cd "$(dirname "$0")"
for stage in rgen rmiss rchit; do
    glslc --target-env=vulkan1.2 -fshader-stage=$stage -O rtriangle.$stage -o rtriangle.$stage.spv
done
