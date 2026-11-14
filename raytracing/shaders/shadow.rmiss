// MIT License - see LICENSE
//
// Miss of a shadow ray: nothing was in the way.
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT uint visible;

void main() { visible = 1u; }
