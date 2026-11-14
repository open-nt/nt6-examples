#!/bin/sh
# MIT License - see LICENSE
#
# Compiles the shaders of the path tracer to SPIR-V with glslc (shaderc). The .spv files are checked in, so building the
# example needs no shader compiler.
set -e
cd "$(dirname "$0")"
glslc --target-env=vulkan1.3 -fshader-stage=rgen -O pathtracer.rgen -o pathtracer.rgen.spv
glslc --target-env=vulkan1.3 -fshader-stage=rgen -O pathtracer_ser.rgen -o pathtracer_ser.rgen.spv
glslc --target-env=vulkan1.3 -fshader-stage=rmiss -O pathtracer.rmiss -o pathtracer.rmiss.spv
glslc --target-env=vulkan1.3 -fshader-stage=rmiss -O shadow.rmiss -o shadow.rmiss.spv
glslc --target-env=vulkan1.3 -fshader-stage=rchit -O triangles.rchit -o triangles.rchit.spv
glslc --target-env=vulkan1.3 -fshader-stage=rint -O sphere.rint -o sphere.rint.spv
glslc --target-env=vulkan1.3 -fshader-stage=rchit -O sphere.rchit -o sphere.rchit.spv
