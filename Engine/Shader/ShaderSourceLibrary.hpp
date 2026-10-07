#pragma once

#include <Shader/ShaderSource.hpp>
#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa::Shader
{
struct ShaderSourceAsset
{
	VirtualPath path;
	ShaderSourceDocument source;
};

struct ShaderDependencyReport
{
	Vector<VirtualPath> includes;
	Vector<String> shaderReferences;
};

class ShaderSourceLibrary final
{
public:
	static Result<ShaderSourceLibrary> Load(const VirtualFileSystem& filesystem, const String& scheme);

	const ShaderSourceAsset* Find(const String& shaderName) const;

	Vector<const ShaderSourceAsset*> Sources() const;

	Result<ShaderDependencyReport> ResolveDependencies(const VirtualFileSystem& filesystem,
	                                                   const String& shaderName) const;

private:
	Map<String, ShaderSourceAsset> shaders;
};
} // namespace Ursa::Shader
