#pragma once

#include <Material/Material.hpp>
#include <Rhi/RHI.hpp>

namespace Ursa::Shader
{
struct ShaderPropertyBinding
{
	Rhi::BindingType bindingType = Rhi::BindingType::UniformBuffer;
	UInt32 binding = 0;
	UInt32 byteOffset = 0;
	UInt32 byteSize = 0;
	bool uniformMember = false;
};

struct ShaderUniformMember
{
	String name;
	String type;
	UInt32 byteOffset = 0;
	UInt32 byteSize = 0;
	UInt32 matrixStride = 0;
	bool spirvRowMajor = false;
};

struct ShaderUniformBufferLayout
{
	UInt32 binding = 0;
	UInt32 byteSize = 0;
	UInt32 stages = 0;
	Vector<ShaderUniformMember> members;
};

struct ShaderVertexInput
{
	UInt32 location = 0;
	String semantic;
	Rhi::VertexFormat format = Rhi::VertexFormat::Float3;
};

struct ShaderBindingLayout
{
	Vector<Rhi::LayoutBinding> descriptors;
	Vector<ShaderUniformBufferLayout> uniformBuffers;
	Vector<ShaderVertexInput> vertexInputs;
	Map<UInt32, String> resourceNames;
	Map<UInt32, String> storageImageFormats;
	Map<String, ShaderPropertyBinding> properties;
};

struct MaterialTextureBinding
{
	String property;
	UInt32 binding = 0;
	ResourceId textureId;
	MaterialTexturePtr texture;
};

Result<ShaderBindingLayout> BuildShaderBindingLayout(const ShaderAsset& shader, const ShaderVariantAsset& variant);

Result<Vector<UInt8>> PackMaterialUniformBuffer(const ShaderAsset& shader, const ShaderBindingLayout& layout,
                                                const MaterialInstance& material, UInt32 binding);

// Matrix inputs use the engine's column-major convention; DXC transposes HLSL matrix types in SPIR-V.
Result<void> WriteShaderUniformValue(Vector<UInt8>& destination, const ShaderUniformMember& member, const void* source,
                                     Size byteSize);

Result<Vector<MaterialTextureBinding>> BuildMaterialTextureBindings(const ShaderAsset& shader,
                                                                    const ShaderBindingLayout& layout,
                                                                    const MaterialInstance& material);

Result<Rhi::LayoutPtr> CreateShaderBindGroupLayout(const ShaderBindingLayout& layout, Rhi::Device& device);
} // namespace Ursa::Shader
