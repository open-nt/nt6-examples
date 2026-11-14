// MIT License - see LICENSE
//
// Closest hit on a sphere: the normal is the object-space hit point of the unit sphere.
#version 460
#extension GL_EXT_ray_tracing : require
#include "common.glsl"
#include "shading.glsl"

void main() {
    const vec3 objectNormal = normalize(gl_ObjectRayOriginEXT + gl_ObjectRayDirectionEXT * gl_HitTEXT);
    const vec3 normal = normalize(objectNormal * mat3(gl_WorldToObjectEXT));
    const vec3 position = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    shade(position, normal);
}
