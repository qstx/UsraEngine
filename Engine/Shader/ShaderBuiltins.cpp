#include <Shader/ShaderBuiltins.hpp>

namespace Ursa::Shader
{
ResourceId BuiltinUnlitShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000001").value;
	return id;
}

ResourceId BuiltinPbrShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000002").value;
	return id;
}

ResourceId BuiltinBlitShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000006").value;
	return id;
}

ResourceId BuiltinFinalPostShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000004").value;
	return id;
}

ResourceId BuiltinBloomShaderId()
{
	static const ResourceId id = ResourceId::Parse("10000000-0000-4000-8000-000000000005").value;
	return id;
}

const char* BuiltinShaderAssetPath(ResourceId id)
{
	if (id == BuiltinUnlitShaderId())
	{
		return "ursa:Shaders/Universal/Unlit.ursashaderasset";
	}
	if (id == BuiltinPbrShaderId())
	{
		return "ursa:Shaders/Universal/Lit.ursashaderasset";
	}
	if (id == BuiltinBlitShaderId())
	{
		return "ursa:Shaders/Internal/Blit.ursashaderasset";
	}
	if (id == BuiltinFinalPostShaderId())
	{
		return "ursa:Shaders/Internal/FinalPost.ursashaderasset";
	}
	if (id == BuiltinBloomShaderId())
	{
		return "ursa:Shaders/Internal/Bloom.ursashaderasset";
	}
	return nullptr;
}
} // namespace Ursa::Shader
