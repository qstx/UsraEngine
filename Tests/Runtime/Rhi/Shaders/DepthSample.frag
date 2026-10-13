#version 450
layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main()
{
	float depth = texture(depthTexture, uv).r;
	color = vec4(depth, depth, depth, 1);
}
