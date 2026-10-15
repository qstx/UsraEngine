#pragma once

#include <Serialization/Serializable.hpp>
#include <Shader/ShaderCompileCache.hpp>
#include <Shader/ShaderSourceLibrary.hpp>

namespace Ursa
{
class SerializationContext;
}

namespace Ursa::Shader
{
constexpr UInt32 shaderReflectionFormatVersion = 2;

struct ShaderStageAsset
{
	ShaderTarget target = ShaderTarget::VulkanSpirv;
	ProgramStage stage = ProgramStage::Vertex;
	String entryPoint;
	Vector<UInt8> spirv;
	String reflectionJson;
	UInt32 reflectionVersion = shaderReflectionFormatVersion;
	String reflectionIdentity;
};

struct ShaderVariantAsset
{
	Vector<String> keywords;
	Vector<ShaderStageAsset> stages;
	bool required = true;
	String layoutIdentity;
};

struct ShaderPassAsset
{
	String name;
	Vector<TagDeclaration> tags;
	Vector<RenderStateDeclaration> renderStates;
	Vector<String> reusedPasses;
	Vector<ShaderVariantAsset> variants;
	String identity;
	bool computeKernel = false;
	Array<UInt32, 3> threadGroupSize{1, 1, 1};
};

struct ShaderSubShaderAsset
{
	Vector<TagDeclaration> tags;
	Vector<RenderStateDeclaration> renderStates;
	Vector<ShaderPassAsset> passes;
	Vector<SubShaderPassOrderEntry> passOrder;
};

struct ShaderReference
{
	String shaderName;
	ResourceId id;
	String passName;
	bool fallback = false;
};

class ShaderAsset;

struct ResolvedShaderReference
{
	ShaderReference reference;
	std::shared_ptr<const ShaderAsset> asset;
};

struct ShaderPassCompilation
{
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
	Vector<String> keywords;
	Vector<CompiledShaderStage> stages;
	bool required = true;
};

class ShaderAsset final : public Serializable
{
public:
	String SerializationType() const override;

	UInt32 SerializationVersion() const override;

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	ResourceId id;
	String name;
	String customEditor;
	String sourcePath;
	UInt32 sourceFormatVersion = 1;
	String sourceHash;
	String compilerVersion;
	Vector<PropertyDeclaration> properties;
	Vector<ShaderSubShaderAsset> subShaders;
	Vector<ShaderReference> references;
	Vector<ResolvedShaderReference> resolvedReferences;
	Vector<String> includeDependencies;
};

Result<ShaderAsset> BuildShaderAsset(const ShaderSourceAsset& source, const ShaderSourceLibrary& library,
                                     const ShaderDependencyReport& dependencies, const String& sourceHash,
                                     const String& compilerVersion, const Vector<ShaderPassCompilation>& compilations);
} // namespace Ursa::Shader
