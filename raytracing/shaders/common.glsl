// MIT License - see LICENSE
//
// Shared by the shaders of the path tracer: the bindings, the push constants, the payload and the random numbers.

#ifndef COMPUTE_PASS // the compute passes work on the images only
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
#endif
// The history of each pixel: the mean radiance with the sample count in alpha, and what the pixel saw (object-space
// position, instance). The previous frame's are read, this frame's written.
layout(set = 0, binding = 1, rgba32f) uniform image2D previousAccumulation;
layout(set = 0, binding = 2, rgba32f) uniform image2D accumulation;
layout(set = 0, binding = 3, rgba32f) uniform image2D previousHit;
layout(set = 0, binding = 4, rgba32f) uniform image2D hitImage;
layout(set = 0, binding = 5, rgba8) uniform image2D output_image; // tone mapped, copied to the swapchain image
// What the denoiser is guided by and works on: the albedo of the primary hit (alpha 1 where the pixel is filtered),
// its normal with the distance from the camera in alpha, and two images the filter ping-pongs between.
layout(set = 0, binding = 10, rgba16f) uniform image2D albedoImage;
layout(set = 0, binding = 11, rgba16f) uniform image2D normalImage;
layout(set = 0, binding = 12, rgba32f) uniform image2D filterA;
layout(set = 0, binding = 13, rgba32f) uniform image2D filterB;
// What DLSS Ray Reconstruction takes instead of the history and the filter: the noisy colour, the specular albedo,
// the linear depth, the motion vectors (pixels from this frame's position to the previous one), the roughness, and
// where it writes its denoised, upscaled result.
layout(set = 0, binding = 14, rgba16f) uniform image2D colorImage;
layout(set = 0, binding = 15, rgba16f) uniform image2D specularAlbedoImage;
layout(set = 0, binding = 16, r32f) uniform image2D depthImage;
layout(set = 0, binding = 17, rg16f) uniform image2D motionImage;
layout(set = 0, binding = 18, r16f) uniform image2D roughnessImage;
layout(set = 0, binding = 19, rgba16f) uniform image2D reconstructed;

const uint INSTANCE_SKY = 0xffffffffu;
const uint INSTANCE_MIXED = 0xfffffffeu;

const uint MATERIAL_DIFFUSE = 0u;
const uint MATERIAL_METAL = 1u;
const uint MATERIAL_GLASS = 2u;
const uint MATERIAL_EMISSIVE = 3u;
const uint MATERIAL_CHECKER = 4u;

struct Material {
    vec4 albedo;  // rgb; a unused
    vec4 params;  // roughness, index of refraction, emission, type
};
layout(set = 0, binding = 6, std430) readonly buffer Materials { Material materials[]; };

// The sphere lights of the frame, for direct light sampling; the sun is a light too, defined below.
struct Light {
    vec4 positionRadius;
    vec4 emission;
};
layout(set = 0, binding = 7, std430) readonly buffer Lights { Light lights[]; };

// Where each instance was in the previous frame: three rows of its transform, for reprojecting the history.
layout(set = 0, binding = 8, std430) readonly buffer PreviousTransforms { vec4 previousTransforms[]; };

const vec3 SUN_DIRECTION = normalize(vec3(0.5, 0.8, 0.3));
const float SUN_COS_HALF_ANGLE = 0.9988;
const vec3 SUN_RADIANCE = vec3(1.0, 0.95, 0.85) * 40.0;

layout(set = 0, binding = 9, std140) uniform Frame {
    vec4 position;   // xyz, w = tan(fov / 2)
    vec4 forward;    // xyz, w = aspect
    vec4 right;
    vec4 up;
    vec4 previousPosition; // the camera of the previous frame, for reprojecting the history
    vec4 previousForward;
    vec4 previousRight;
    vec4 previousUp;
    uint frame;        // for the random numbers
    uint samples;      // paths per pixel this frame
    uint bounces;      // most bounces per path
    uint flags;        // 1: swap red and blue for a BGRA swapchain
    uint lightCount;   // sphere lights in the Lights buffer
    uint maxHistory;   // most previous samples a pixel keeps: small while things move, huge when they rest
    uint candidates;   // light candidates resampled per diffuse hit
    uint denoise;      // 1: the à-trous filter runs over the irradiance
    uint dlss;         // 1: DLSS Ray Reconstruction takes the frame: its guide images are written, one jitter per frame
    uint padding0;
    uint padding1;
    uint padding2;
    vec4 jitter;       // xy: the sub-pixel offset of this frame's samples when dlss is on
} camera;

struct Payload {
    vec3 radiance;     // emitted light at the hit
    vec3 attenuation;  // what the surface keeps of the light that comes back along the next ray
    vec3 origin;       // the next ray
    vec3 direction;
    uint seed;
    uint done;         // 1: the path ends here
    uint emissive;     // 1: a light the next ray hits counts; 0 after a diffuse bounce, whose direct light was sampled
    uint instance;     // what was hit, and where on it in its own coordinates: the history is matched by these
    vec3 objectPosition;
    vec3 normal;       // of the primary hit, for the denoiser
    vec3 albedo;       // what the radiance of a diffuse hit is proportional to; the denoiser filters without it
    uint filterable;   // 1 for diffuse surfaces, which the denoiser smooths; 0 for mirrors, glass, lights and sky
    float roughness;   // of the primary hit, for ray reconstruction
    uint type;         // the material type of the primary hit
};

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

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
