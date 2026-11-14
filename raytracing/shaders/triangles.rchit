// MIT License - see LICENSE
//
// Closest hit on a triangle mesh. The normal comes from the vertex positions that VK_KHR_ray_tracing_position_fetch
// hands the shader, so no vertex buffer is bound.
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : require
#include "common.glsl"
#include "shading.glsl"

void main() {
    const vec3 p0 = gl_HitTriangleVertexPositionsEXT[0];
    const vec3 p1 = gl_HitTriangleVertexPositionsEXT[1];
    const vec3 p2 = gl_HitTriangleVertexPositionsEXT[2];
    const vec3 objectNormal = normalize(cross(p1 - p0, p2 - p0));
    const vec3 normal = normalize(objectNormal * mat3(gl_WorldToObjectEXT)); // the inverse transpose
    const vec3 position = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    shade(position, normal);
}
