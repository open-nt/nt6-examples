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

// Direct light on a diffuse surface from one light chosen among the sun and the sphere lights: the light's radiance
// times the BRDF and the cosine, over the probabilities of the choice and of the direction.
vec3 directLight(vec3 position, vec3 normal, vec3 albedo) {
    const uint count = camera.lightCount + 1u;
    const uint pick = min(uint(random(payload.seed) * float(count)), count - 1u);
    vec3 direction;
    float distance, pdf;
    vec3 radiance;
    if (pick == 0u) { // the sun
        direction = sampleCone(SUN_DIRECTION, SUN_COS_HALF_ANGLE, payload.seed);
        distance = 1e4;
        pdf = 1.0 / (6.28318530718 * (1.0 - SUN_COS_HALF_ANGLE));
        radiance = SUN_RADIANCE;
    } else {
        const Light light = lights[pick - 1u];
        const vec3 toCenter = light.positionRadius.xyz - position;
        const float d2 = dot(toCenter, toCenter);
        const float r = light.positionRadius.w;
        if (d2 <= r * r)
            return vec3(0.0); // inside the light
        const float cosHalfAngle = sqrt(1.0 - r * r / d2);
        direction = sampleCone(normalize(toCenter), cosHalfAngle, payload.seed);
        // Where the ray meets the sphere, so that the shadow ray stops short of it.
        const float b = dot(direction, -toCenter);
        const float c = d2 - r * r;
        const float disc = max(b * b - c, 0.0);
        distance = max(-b - sqrt(disc), 0.0) - 1e-3;
        pdf = 1.0 / (6.28318530718 * (1.0 - cosHalfAngle));
        radiance = light.emission.rgb;
    }
    const float cosTheta = dot(direction, normal);
    if (cosTheta <= 0.0 || distance <= 0.0)
        return vec3(0.0);
    if (!visible(position, direction, distance))
        return vec3(0.0);
    return albedo * (1.0 / 3.14159265) * radiance * cosTheta * float(count) / pdf;
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
    vec3 albedo = material.albedo.rgb;

    if (type == MATERIAL_CHECKER) {
        const bool odd = (int(floor(position.x)) + int(floor(position.z))) % 2 != 0;
        // Far from the camera the squares are smaller than a pixel: fade them to their mean instead of shimmering.
        const float fade = smoothstep(25.0, 90.0, distance(position, camera.position.xyz));
        albedo = mix(odd ? albedo : albedo * 0.35, albedo * 0.675, fade);
    }
    if (type == MATERIAL_DIFFUSE || type == MATERIAL_CHECKER) {
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
        payload.radiance = payload.emissive != 0u ? albedo * material.params.z : vec3(0.0);
        payload.attenuation = vec3(0.0);
        payload.done = 1u;
    }
}
