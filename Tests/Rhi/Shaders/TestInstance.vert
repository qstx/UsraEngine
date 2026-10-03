#version 450
layout(location = 0) out vec3 color;
void main() {
    const vec2 positions[3] = vec2[3](vec2(-0.32, -0.55), vec2(0.32, -0.55), vec2(0.0, 0.55));
    vec2 position = positions[gl_VertexIndex] + vec2(gl_InstanceIndex == 0 ? -0.5 : 0.5, 0.0);
    gl_Position = vec4(position, 0.0, 1.0);
    color = gl_InstanceIndex == 0 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
}
