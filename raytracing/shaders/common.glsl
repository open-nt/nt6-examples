// MIT License - see LICENSE
//
// Shared by the shaders of the path tracer: the bindings, the push constants, the payload and the random numbers.

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1, rgba32f) uniform image2D accumulation; // the running mean of the samples, linear
layout(set = 0, binding = 2, rgba8) uniform image2D output_image;   // tone mapped, copied to the swapchain image

const uint MATERIAL_DIFFUSE = 0u;
const uint MATERIAL_METAL = 1u;
const uint MATERIAL_GLASS = 2u;
const uint MATERIAL_EMISSIVE = 3u;
const uint MATERIAL_CHECKER = 4u;

struct Material {
    vec4 albedo;  // rgb; a unused
    vec4 params;  // roughness, index of refraction, emission, type
};
layout(set = 0, binding = 3, std430) readonly buffer Materials { Material materials[]; };

// The sphere lights of the frame, for direct light sampling; the sun is a light too, defined below.
struct Light {
    vec4 positionRadius;
    vec4 emission;
};
layout(set = 0, binding = 4, std430) readonly buffer Lights { Light lights[]; };

const vec3 SUN_DIRECTION = normalize(vec3(0.5, 0.8, 0.3));
const float SUN_COS_HALF_ANGLE = 0.9988;
const vec3 SUN_RADIANCE = vec3(1.0, 0.95, 0.85) * 40.0;

layout(push_constant) uniform Camera {
    vec4 position;   // xyz, w = tan(fov / 2)
    vec4 forward;    // xyz, w = aspect
    vec4 right;
    vec4 up;
    uint frame;        // for the random numbers
    uint accumulated;  // samples already in the accumulation image
    uint bounces;      // most bounces per path
    uint flags;        // 1: swap red and blue for a BGRA swapchain
    uint samples;      // paths per pixel this frame
    uint lightCount;   // sphere lights in the Lights buffer
} camera;

struct Payload {
    vec3 radiance;     // emitted light at the hit
    vec3 attenuation;  // what the surface keeps of the light that comes back along the next ray
    vec3 origin;       // the next ray
    vec3 direction;
    uint seed;
    uint done;         // 1: the path ends here
    uint emissive;     // 1: a light the next ray hits counts; 0 after a diffuse bounce, whose direct light was sampled
};

// PCG, one state per path.
uint pcg(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(inout uint seed) { return float(pcg(seed)) / 4294967296.0; }

// Cosine-weighted direction about the normal.
vec3 sampleHemisphere(vec3 normal, inout uint seed) {
    const float u = random(seed), v = random(seed);
    const float r = sqrt(u), phi = 6.28318530718 * v;
    const vec3 tangent = normalize(abs(normal.x) > 0.5 ? cross(normal, vec3(0.0, 1.0, 0.0)) : cross(normal, vec3(1.0, 0.0, 0.0)));
    const vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(max(0.0, 1.0 - u)));
}

vec3 sampleSphere(inout uint seed) {
    const float z = 1.0 - 2.0 * random(seed);
    const float r = sqrt(max(0.0, 1.0 - z * z));
    const float phi = 6.28318530718 * random(seed);
    return vec3(r * cos(phi), r * sin(phi), z);
}
