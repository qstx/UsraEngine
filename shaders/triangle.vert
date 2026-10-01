#version 450
layout(location = 0) out vec3 color;
void main() {
    vec2 points[3] = vec2[](vec2(0.0, -0.65), vec2(0.65, 0.65), vec2(-0.65, 0.65));
    vec3 colors[3] = vec3[](vec3(1.0, 0.2, 0.15), vec3(0.15, 0.9, 0.35), vec3(0.2, 0.45, 1.0));
    gl_Position = vec4(points[gl_VertexIndex], 0.0, 1.0);
    color = colors[gl_VertexIndex];
}
