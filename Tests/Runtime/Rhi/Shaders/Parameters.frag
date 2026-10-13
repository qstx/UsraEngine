#version 450
layout(set = 0, binding = 0, std140) uniform FrameData { vec4 tint; } frame;
layout(set = 1, binding = 0, std140) uniform ObjectData { vec4 color; } object;
layout(location = 0) out vec4 outputColor;
void main()
{
	outputColor = frame.tint * object.color;
}
