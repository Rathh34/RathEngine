#version 450

layout(location = 0) in vec3 inPosition; // Changed to vec3
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 render_matrix;
} push;

void main() {
    gl_Position = push.render_matrix * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragUV = inUV;
}
