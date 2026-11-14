// MIT License - see LICENSE
//
// Miss: the sky, a gradient with a sun.
#version 460
#extension GL_EXT_ray_tracing : require
#include "common.glsl"

layout(location = 0) rayPayloadInEXT Payload payload;

void main() {
    const vec3 direction = normalize(gl_WorldRayDirectionEXT);
    const float t = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(vec3(0.6, 0.7, 0.8), vec3(0.15, 0.3, 0.7), t);
    const float cosSun = dot(direction, SUN_DIRECTION);
    sky += vec3(1.0, 0.95, 0.85) * pow(max(cosSun, 0.0), 32.0) * 0.3; // the glow around the sun
    if (cosSun > SUN_COS_HALF_ANGLE && payload.emissive != 0u) // the disc, unless its light was sampled already
        sky += SUN_RADIANCE;
    payload.radiance = sky;
    payload.attenuation = vec3(0.0);
    payload.done = 1u;
    payload.instance = INSTANCE_SKY;
    payload.objectPosition = direction;
    payload.normal = -direction;
    payload.albedo = vec3(1.0);
    payload.filterable = 0u;
    payload.origin = gl_WorldRayOriginEXT + direction * 1e4;
    payload.type = 0xffffffffu;
    payload.roughness = 0.0;
}
