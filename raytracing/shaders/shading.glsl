// MIT License - see LICENSE
//
// The materials, shared by the closest-hit shaders of triangles and spheres: each decides the next ray of the path.

layout(location = 0) rayPayloadInEXT Payload payload;
layout(location = 1) rayPayloadEXT uint shadowVisible;

// Whether nothing stands between `origin` and `distance` along `direction`: a shadow ray that stops at the first hit
// and runs no closest-hit shader; the shadow miss shader sets the payload.
bool visible(vec3 origin, vec3 direction, float distance) {
    shadowVisible = 0u;
    traceRayEXT(scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT, 0xff, 0, 0, 1,
                origin, 1e-4, direction, distance, 1);
    return shadowVisible != 0u;
}

// A direction towards a uniformly chosen point of the cone around `axis` with the given cosine of its half angle.
vec3 sampleCone(vec3 axis, float cosHalfAngle, inout uint seed) {
    const float cosTheta = 1.0 - random(seed) * (1.0 - cosHalfAngle);
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 6.28318530718 * random(seed);
    const vec3 tangent = normalize(abs(axis.x) > 0.5 ? cross(axis, vec3(0.0, 1.0, 0.0)) : cross(axis, vec3(1.0, 0.0, 0.0)));
    const vec3 bitangent = cross(axis, tangent);
    return normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + axis * cosTheta);
}

// One candidate for the direct light: a direction towards the sun or a sphere light, with the unshadowed
// contribution it would make and the probability it was generated with.
struct LightSample {
    vec3 direction;
    float distance;
    vec3 contribution; // BRDF times radiance times cosine, unshadowed
    float pdf;
};

LightSample sampleLight(vec3 position, vec3 normal, vec3 albedo, inout uint seed) {
    LightSample s;
    s.contribution = vec3(0.0);
    s.pdf = 1.0;
    s.distance = 0.0;
    s.direction = normal;
    const uint count = camera.lightCount + 1u;
    const uint pick = min(uint(random(seed) * float(count)), count - 1u);
    vec3 radiance;
    if (pick == 0u) { // the sun
        s.direction = sampleCone(SUN_DIRECTION, SUN_COS_HALF_ANGLE, seed);
        s.distance = 1e4;
        s.pdf = 1.0 / (6.28318530718 * (1.0 - SUN_COS_HALF_ANGLE)) / float(count);
        radiance = SUN_RADIANCE;
    } else {
        const Light light = lights[pick - 1u];
        const vec3 toCenter = light.positionRadius.xyz - position;
        const float d2 = dot(toCenter, toCenter);
        const float r = light.positionRadius.w;
        if (d2 <= r * r)
            return s; // inside the light
        const float cosHalfAngle = sqrt(1.0 - r * r / d2);
        s.direction = sampleCone(normalize(toCenter), cosHalfAngle, seed);
        // Where the ray meets the sphere, so that the shadow ray stops short of it.
        const float b = dot(s.direction, -toCenter);
        const float c = d2 - r * r;
        const float disc = max(b * b - c, 0.0);
        s.distance = max(-b - sqrt(disc), 0.0) - 1e-3;
        s.pdf = 1.0 / (6.28318530718 * (1.0 - cosHalfAngle)) / float(count);
        radiance = light.emission.rgb;
    }
    const float cosTheta = dot(s.direction, normal);
    if (cosTheta > 0.0 && s.distance > 0.0)
        s.contribution = albedo * (1.0 / 3.14159265) * radiance * cosTheta;
    return s;
}

// Direct light on a diffuse surface by resampled importance sampling, the first stage of ReSTIR: several candidate
// light samples are drawn, one is kept with probability proportional to its unshadowed contribution, and only that
// one gets a shadow ray. With many small lights this finds the ones that matter far more often than one blind pick.
vec3 directLight(vec3 position, vec3 normal, vec3 albedo) {
    LightSample kept;
    kept.contribution = vec3(0.0);
    float keptTarget = 0.0;
    float weightSum = 0.0;
    const uint candidates = max(camera.candidates, 1u);
    for (uint i = 0u; i < candidates; ++i) {
        const LightSample s = sampleLight(position, normal, albedo, payload.seed);
        const float target = luminance(s.contribution);
        const float weight = target / s.pdf;
        weightSum += weight;
        if (weight > 0.0 && random(payload.seed) * weightSum <= weight) {
            kept = s;
            keptTarget = target;
        }
    }
    if (keptTarget <= 0.0)
        return vec3(0.0);
    if (!visible(position, kept.direction, kept.distance))
        return vec3(0.0);
    return kept.contribution * (weightSum / (keptTarget * float(candidates)));
}

void shade(vec3 position, vec3 normal) {
    const Material material = materials[gl_InstanceCustomIndexEXT];
    const uint type = uint(material.params.w + 0.5);
    const vec3 incoming = gl_WorldRayDirectionEXT;
    const bool inside = dot(incoming, normal) > 0.0;
    const vec3 facing = inside ? -normal : normal; // towards the ray
    const float eps = 1e-3;

    payload.radiance = vec3(0.0);
    payload.done = 0u;
    payload.emissive = 1u;
    payload.instance = gl_InstanceCustomIndexEXT;
    payload.objectPosition = gl_ObjectRayOriginEXT + gl_ObjectRayDirectionEXT * gl_HitTEXT;
    payload.normal = facing;
    payload.filterable = 0u;
    payload.origin = position;
    payload.type = type;
    payload.roughness = type == MATERIAL_METAL ? material.params.x : type == MATERIAL_GLASS ? 0.0 : 1.0;
    vec3 albedo = material.albedo.rgb;

    if (type == MATERIAL_CHECKER) {
        const bool odd = (int(floor(position.x)) + int(floor(position.z))) % 2 != 0;
        // Far from the camera the squares are smaller than a pixel: fade them to their mean instead of shimmering.
        const float fade = smoothstep(25.0, 90.0, distance(position, camera.position.xyz));
        albedo = mix(odd ? albedo : albedo * 0.35, albedo * 0.675, fade);
    }
    payload.albedo = albedo;
    if (type == MATERIAL_DIFFUSE || type == MATERIAL_CHECKER) {
        payload.filterable = 1u;
        payload.origin = position + facing * eps;
        payload.radiance = directLight(payload.origin, facing, albedo);
        payload.emissive = 0u; // the lights were sampled here; the next bounce must not count them again
        payload.attenuation = albedo;
        payload.direction = sampleHemisphere(facing, payload.seed);
    } else if (type == MATERIAL_METAL) {
        const float roughness = material.params.x;
        vec3 reflected = reflect(incoming, facing) + roughness * sampleSphere(payload.seed);
        if (dot(reflected, facing) <= 0.0) {
            payload.attenuation = vec3(0.0); // absorbed below the surface
            payload.done = 1u;
            return;
        }
        payload.attenuation = albedo;
        payload.origin = position + facing * eps;
        payload.direction = normalize(reflected);
    } else if (type == MATERIAL_GLASS) {
        const float ior = material.params.y;
        const float eta = inside ? ior : 1.0 / ior;
        const float cosTheta = min(dot(-incoming, facing), 1.0);
        const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        // Schlick's approximation of the Fresnel reflectance.
        float r0 = (1.0 - ior) / (1.0 + ior);
        r0 *= r0;
        const float reflectance = r0 + (1.0 - r0) * pow(1.0 - cosTheta, 5.0);
        payload.attenuation = inside ? pow(albedo, vec3(0.5)) : vec3(1.0); // tinted on the way through
        if (eta * sinTheta > 1.0 || random(payload.seed) < reflectance) {
            payload.direction = reflect(incoming, facing);
            payload.origin = position + facing * eps;
        } else {
            payload.direction = normalize(refract(incoming, facing, eta));
            payload.origin = position - facing * eps;
        }
    } else { // MATERIAL_EMISSIVE
        payload.albedo = vec3(1.0);
        payload.radiance = payload.emissive != 0u ? albedo * material.params.z : vec3(0.0);
        payload.attenuation = vec3(0.0);
        payload.done = 1u;
    }
}
