#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;
layout(location = 0) out vec2 uv;
layout(location = 4) out vec4 instanceColor;

struct DirectionalLight { vec4 directionIntensity; vec4 color; };
struct PointLight { vec4 positionRange; vec4 colorIntensity; };
struct Instance { mat4 model; vec4 color; };
layout(std430, set = 0, binding = 0) readonly buffer Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    ivec4 counts;
    DirectionalLight directional[4];
    PointLight point[8];
    Instance instances[];
} scene;

void main() {
    Instance instance = scene.instances[gl_InstanceIndex];
    uv = inUv;
    instanceColor = instance.color;
    gl_Position = scene.viewProjection * instance.model * vec4(inPosition, 1.0);
}
