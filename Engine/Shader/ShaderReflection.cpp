#include <Shader/ShaderReflection.hpp>

#include <nlohmann/json.hpp>
#include <Core/StringInterop.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace Ursa::Shader
{
namespace
{
using Json = nlohmann::json;

struct ReflectedResource
{
	String name;
	UInt32 set = 0;
	UInt32 binding = 0;
	UInt32 count = 1;
	UInt32 stages = 0;
	Rhi::BindingType type = Rhi::BindingType::UniformBuffer;
	String imageFormat;
};

struct ReflectedMember
{
	String name;
	String type;
	UInt32 offset = 0;
	UInt32 size = 0;
	UInt32 matrixStride = 0;
	bool spirvRowMajor = false;
};

struct ReflectedUniformBlock
{
	UInt32 set = 0;
	UInt32 binding = 0;
	UInt32 size = 0;
	UInt32 stages = 0;
	Vector<ReflectedMember> members;
};

Result<UInt32> ReadUInt(const Json& object, const char* field, UInt32 defaultValue = 0)
{
	if (!object.contains(field))
	{
		return Result<UInt32>::Success(defaultValue);
	}
	const Json& value = object[field];
	if (!value.is_number_unsigned() && !value.is_number_integer())
	{
		return Result<UInt32>::Failure(String("Reflection field is not an unsigned integer: ") + field);
	}
	const Int64 integer = value.get<Int64>();
	if (integer < 0 || static_cast<UInt64>(integer) > std::numeric_limits<UInt32>::max())
	{
		return Result<UInt32>::Failure(String("Reflection field is out of range: ") + field);
	}
	return Result<UInt32>::Success(static_cast<UInt32>(integer));
}

UInt32 StageMask(ProgramStage stage)
{
	switch (stage)
	{
		case ProgramStage::Vertex:
			return 1;
		case ProgramStage::Fragment:
			return 2;
		case ProgramStage::Compute:
			return 4;
	}
	return 0;
}

String StageMode(ProgramStage stage)
{
	switch (stage)
	{
		case ProgramStage::Vertex:
			return "vert";
		case ProgramStage::Fragment:
			return "frag";
		case ProgramStage::Compute:
			return "comp";
	}
	return "unknown";
}

UInt32 ValueSize(PropertyType type)
{
	switch (type)
	{
		case PropertyType::Float:
		case PropertyType::Range:
		case PropertyType::Int:
			return sizeof(UInt32);
		case PropertyType::Color:
		case PropertyType::Vector:
			return sizeof(Float32) * 4;
		case PropertyType::Texture2D:
			return 0;
	}
	return 0;
}

UInt32 ReflectedValueSize(const String& type)
{
	if (type == "float" || type == "half" || type == "int" || type == "uint" || type == "bool")
	{
		return 4;
	}
	if (type == "vec2" || type == "ivec2" || type == "uvec2" || type == "float2" || type == "int2" || type == "uint2")
	{
		return 8;
	}
	if (type == "vec3" || type == "ivec3" || type == "uvec3" || type == "float3" || type == "int3" || type == "uint3")
	{
		return 12;
	}
	if (type == "vec4" || type == "ivec4" || type == "uvec4" || type == "float4" || type == "half4" || type == "int4" ||
	    type == "uint4")
	{
		return 16;
	}
	return type == "mat4" || type == "float4x4" ? 64u : 0u;
}

bool ReflectionTypeMatches(PropertyType propertyType, const String& reflectedType)
{
	switch (propertyType)
	{
		case PropertyType::Float:
		case PropertyType::Range:
			return reflectedType == "float" || reflectedType == "half";
		case PropertyType::Int:
			return reflectedType == "int" || reflectedType == "uint" || reflectedType == "bool";
		case PropertyType::Color:
		case PropertyType::Vector:
			return reflectedType == "vec4" || reflectedType == "float4" || reflectedType == "half4";
		case PropertyType::Texture2D:
			return false;
	}
	return false;
}

Result<Rhi::VertexFormat> ParseVertexFormat(const String& type)
{
	if (type == "vec2" || type == "float2")
	{
		return Result<Rhi::VertexFormat>::Success(Rhi::VertexFormat::Float2);
	}
	if (type == "vec3" || type == "float3")
	{
		return Result<Rhi::VertexFormat>::Success(Rhi::VertexFormat::Float3);
	}
	if (type == "vec4" || type == "float4")
	{
		return Result<Rhi::VertexFormat>::Success(Rhi::VertexFormat::Float4);
	}
	if (type == "uint4" || type == "uvec4")
	{
		return Result<Rhi::VertexFormat>::Success(Rhi::VertexFormat::UInt4);
	}
	return Result<Rhi::VertexFormat>::Failure("Unsupported Shader vertex input type " + type);
}

Result<UInt32> DescriptorCount(const Json& resource)
{
	if (!resource.contains("array"))
	{
		return Result<UInt32>::Success(1);
	}
	if (!resource["array"].is_array())
	{
		return Result<UInt32>::Failure("Reflection descriptor array has invalid dimensions");
	}
	UInt64 count = 1;
	for (const Json& dimension : resource["array"])
	{
		if (!dimension.is_number_unsigned() && !dimension.is_number_integer())
		{
			return Result<UInt32>::Failure("Reflection descriptor array dimension is invalid");
		}
		const Int64 length = dimension.get<Int64>();
		if (length <= 0 || count * static_cast<UInt64>(length) > std::numeric_limits<UInt32>::max())
		{
			return Result<UInt32>::Failure("Runtime or oversized descriptor arrays are unsupported");
		}
		count *= static_cast<UInt64>(length);
	}
	return Result<UInt32>::Success(static_cast<UInt32>(count));
}

String BindingKey(UInt32 set, UInt32 binding)
{
	return ToUrsaString(std::to_string(set) + ":" + std::to_string(binding));
}

Result<void> AddResources(const Json& reflection, const char* field, Rhi::BindingType type, UInt32 stageMask,
                          Map<String, ReflectedResource>& resources)
{
	if (!reflection.contains(field))
	{
		return {};
	}
	if (!reflection[field].is_array())
	{
		return Result<void>::Failure(String("SPIR-V reflection field is not an array: ") + field);
	}
	for (const Json& item : reflection[field])
	{
		if (!item.is_object() || !item.contains("name") || !item["name"].is_string())
		{
			return Result<void>::Failure(String("SPIR-V reflection has an invalid resource in ") + field);
		}
		auto set = ReadUInt(item, "set");
		auto binding = ReadUInt(item, "binding");
		auto count = DescriptorCount(item);
		if (!set || !binding || !count)
		{
			return Result<void>::Failure(!set ? set.error : !binding ? binding.error : count.error);
		}
		if (set.value != 0)
		{
			return Result<void>::Failure("Shader descriptor sets above set 0 are not supported by the current RHI");
		}
		const String name = ToUrsaString(item["name"].get<std::string>());
		String imageFormat;
		if (type == Rhi::BindingType::StorageTexture && item.contains("format"))
		{
			if (!item["format"].is_string())
			{
				return Result<void>::Failure("Storage image format reflection is invalid");
			}
			imageFormat = ToUrsaString(item["format"].get<std::string>());
		}
		const String key = BindingKey(set.value, binding.value);
		for (const auto& resource : resources)
		{
			if (resource.second.name == name && resource.first != key)
			{
				return Result<void>::Failure("Shader stages assign different bindings to resource " + name);
			}
		}
		auto found = resources.find(key);
		if (found == resources.end())
		{
			resources.emplace(
			    key, ReflectedResource{name, set.value, binding.value, count.value, stageMask, type, imageFormat});
		}
		else
		{
			if (found->second.type != type || found->second.count != count.value ||
			    found->second.imageFormat != imageFormat)
			{
				return Result<void>::Failure("Shader stages declare incompatible resources at descriptor " + key);
			}
			if (found->second.name != name)
			{
				return Result<void>::Failure("Shader stages use different resource names at descriptor " + key);
			}
			found->second.stages |= stageMask;
		}
	}
	return {};
}

Result<void> AddUniformBlocks(const Json& reflection, UInt32 stageMask, Map<String, ReflectedUniformBlock>& blocks)
{
	if (!reflection.contains("ubos"))
	{
		return {};
	}
	if (!reflection["ubos"].is_array())
	{
		return Result<void>::Failure("SPIR-V reflection ubos field is not an array");
	}
	for (const Json& item : reflection["ubos"])
	{
		auto set = ReadUInt(item, "set");
		auto binding = ReadUInt(item, "binding");
		auto size = ReadUInt(item, "block_size");
		if (!set || !binding || !size)
		{
			return Result<void>::Failure("Invalid SPIR-V uniform block reflection");
		}
		const Json* members = item.contains("members") ? &item["members"] : nullptr;
		if (!members && item.contains("type") && item["type"].is_string() && reflection.contains("types") &&
		    reflection["types"].is_object())
		{
			const std::string typeId = item["type"].get<std::string>();
			auto typeInfo = reflection["types"].find(typeId);
			if (typeInfo != reflection["types"].end() && typeInfo->is_object() && typeInfo->contains("members"))
			{
				members = &typeInfo->operator[]("members");
			}
		}
		if (!members || !members->is_array())
		{
			return Result<void>::Failure("SPIR-V uniform block has no reflected member list");
		}
		if (set.value != 0)
		{
			return Result<void>::Failure("Shader uniform buffers in descriptor sets above set 0 are unsupported");
		}
		ReflectedUniformBlock block;
		block.set = set.value;
		block.binding = binding.value;
		block.size = size.value;
		block.stages = stageMask;
		for (const Json& member : *members)
		{
			if (!member.is_object() || !member.contains("name") || !member["name"].is_string() ||
			    !member.contains("type") || !member["type"].is_string())
			{
				return Result<void>::Failure("Invalid SPIR-V uniform member reflection");
			}
			auto offset = ReadUInt(member, "offset");
			if (!offset)
			{
				return Result<void>::Failure(offset.error);
			}
			UInt32 memberSize = 0;
			if (member.contains("size"))
			{
				auto parsedSize = ReadUInt(member, "size");
				if (!parsedSize)
				{
					return Result<void>::Failure(parsedSize.error);
				}
				memberSize = parsedSize.value;
			}
			auto matrixStride = ReadUInt(member, "matrix_stride");
			if (!matrixStride || (member.contains("row_major") && !member["row_major"].is_boolean()))
			{
				return Result<void>::Failure("Invalid reflected matrix stride or major order");
			}
			block.members.push_back({ToUrsaString(member["name"].get<std::string>()),
			                         ToUrsaString(member["type"].get<std::string>()), offset.value, memberSize,
			                         matrixStride.value, member.value("row_major", false)});
		}
		const String key = BindingKey(set.value, binding.value);
		auto found = blocks.find(key);
		if (found == blocks.end())
		{
			blocks.emplace(key, std::move(block));
		}
		else
		{
			if (found->second.size != block.size || found->second.members.size() != block.members.size())
			{
				return Result<void>::Failure("Shader stages declare incompatible uniform blocks at " + key);
			}
			for (Ursa::Size index = 0; index < block.members.size(); ++index)
			{
				const auto& existing = found->second.members[index];
				const auto& candidate = block.members[index];
				if (existing.name != candidate.name || existing.type != candidate.type ||
				    existing.offset != candidate.offset || existing.matrixStride != candidate.matrixStride ||
				    existing.spirvRowMajor != candidate.spirvRowMajor ||
				    (existing.size && candidate.size && existing.size != candidate.size))
				{
					return Result<void>::Failure("Shader stages declare incompatible uniform member layouts at " + key);
				}
			}
			found->second.stages |= stageMask;
		}
	}
	return {};
}

} // namespace

Result<ShaderBindingLayout> BuildShaderBindingLayout(const ShaderAsset& shader, const ShaderVariantAsset& variant)
{
	Map<String, ReflectedResource> resources;
	Map<String, ReflectedUniformBlock> blocks;
	ShaderBindingLayout layout;
	for (const ShaderStageAsset& stage : variant.stages)
	{
		if (stage.target != ShaderTarget::VulkanSpirv)
		{
			return Result<ShaderBindingLayout>::Failure(
			    "Cannot build Vulkan resource layouts from a non-Vulkan shader");
		}
		Json reflection = Json::parse(stage.reflectionJson.begin(), stage.reflectionJson.end(), nullptr, false);
		if (reflection.is_discarded() || !reflection.is_object() || !reflection.contains("entryPoints") ||
		    !reflection["entryPoints"].is_array())
		{
			return Result<ShaderBindingLayout>::Failure("Invalid SPIR-V entry point reflection in Shader " +
			                                            shader.name);
		}
		bool entryFound = false;
		for (const Json& entry : reflection["entryPoints"])
		{
			if (entry.value("name", std::string{}) == ToStdString(stage.entryPoint) &&
			    entry.value("mode", std::string{}) == ToStdString(StageMode(stage.stage)))
			{
				entryFound = true;
				break;
			}
		}
		if (!entryFound)
		{
			return Result<ShaderBindingLayout>::Failure("SPIR-V reflection lacks entry point " + stage.entryPoint +
			                                            " in Shader " + shader.name);
		}
		if (stage.stage == ProgramStage::Vertex && reflection.contains("inputs"))
		{
			if (!reflection["inputs"].is_array())
			{
				return Result<ShaderBindingLayout>::Failure("SPIR-V vertex inputs are not an array");
			}
			for (const Json& input : reflection["inputs"])
			{
				if (!input.is_object() || !input.contains("name") || !input["name"].is_string() ||
				    !input.contains("type") || !input["type"].is_string())
				{
					return Result<ShaderBindingLayout>::Failure("Invalid SPIR-V vertex input reflection");
				}
				auto location = ReadUInt(input, "location");
				auto format = ParseVertexFormat(ToUrsaString(input["type"].get<std::string>()));
				if (!location || !format)
				{
					return Result<ShaderBindingLayout>::Failure(!location ? location.error : format.error);
				}
				String semantic = ToUrsaString(input["name"].get<std::string>());
				const auto marker = semantic.rfind(".");
				if (marker != String::npos)
				{
					semantic = semantic.substr(marker + 1);
				}
				auto existing =
				    std::find_if(layout.vertexInputs.begin(), layout.vertexInputs.end(),
				                 [&](const ShaderVertexInput& current) { return current.location == location.value; });
				if (existing != layout.vertexInputs.end())
				{
					if (existing->semantic != semantic || existing->format != format.value)
					{
						return Result<ShaderBindingLayout>::Failure("Shader stages disagree on vertex input layout");
					}
					continue;
				}
				layout.vertexInputs.push_back({location.value, std::move(semantic), format.value});
			}
		}
		const UInt32 stageMask = StageMask(stage.stage);
		for (const auto& resource : {std::pair<const char*, Rhi::BindingType>{"ubos", Rhi::BindingType::UniformBuffer},
		                             {"ssbos", Rhi::BindingType::StorageBuffer},
		                             {"textures", Rhi::BindingType::CombinedImageSampler},
		                             {"separate_images", Rhi::BindingType::SampledTexture},
		                             {"separate_samplers", Rhi::BindingType::Sampler},
		                             {"images", Rhi::BindingType::StorageTexture}})
		{
			auto added = AddResources(reflection, resource.first, resource.second, stageMask, resources);
			if (!added)
			{
				return Result<ShaderBindingLayout>::Failure(added.error);
			}
		}
		if (reflection.contains("push_constants") && reflection["push_constants"].is_array() &&
		    !reflection["push_constants"].empty())
		{
			return Result<ShaderBindingLayout>::Failure("Push constants are not supported by the current RHI layout");
		}
		auto blocksAdded = AddUniformBlocks(reflection, stageMask, blocks);
		if (!blocksAdded)
		{
			return Result<ShaderBindingLayout>::Failure(blocksAdded.error);
		}
	}
	for (const auto& resource : resources)
	{
		layout.descriptors.push_back(
		    {resource.second.binding, resource.second.type, resource.second.count, resource.second.stages});
		layout.resourceNames.emplace(resource.second.binding, resource.second.name);
		if (resource.second.type == Rhi::BindingType::StorageTexture)
		{
			layout.storageImageFormats.emplace(resource.second.binding, resource.second.imageFormat);
		}
	}
	for (const auto& block : blocks)
	{
		ShaderUniformBufferLayout uniformBuffer;
		uniformBuffer.binding = block.second.binding;
		uniformBuffer.byteSize = block.second.size;
		uniformBuffer.stages = block.second.stages;
		for (const ReflectedMember& member : block.second.members)
		{
			const UInt32 memberBytes = member.size ? member.size : ReflectedValueSize(member.type);
			if (member.offset > block.second.size || memberBytes > block.second.size - member.offset)
			{
				return Result<ShaderBindingLayout>::Failure("Uniform member lies outside its reflected block: " +
				                                            member.name);
			}
			if ((member.type == "mat4" || member.type == "float4x4") && member.matrixStride != 0 &&
			    member.matrixStride != 16)
			{
				return Result<ShaderBindingLayout>::Failure("Unsupported reflected Matrix4x4 stride: " + member.name);
			}
			uniformBuffer.members.push_back(
			    {member.name, member.type, member.offset, memberBytes, member.matrixStride, member.spirvRowMajor});
			const auto property =
			    std::find_if(shader.properties.begin(), shader.properties.end(),
			                 [&](const PropertyDeclaration& candidate) { return candidate.identifier == member.name; });
			if (property == shader.properties.end() || property->type == PropertyType::Texture2D)
			{
				continue;
			}
			if (!ReflectionTypeMatches(property->type, member.type))
			{
				return Result<ShaderBindingLayout>::Failure("Shader property " + property->identifier +
				                                            " type does not match its reflected uniform member");
			}
			const UInt32 size = member.size ? member.size : ValueSize(property->type);
			if (size < ValueSize(property->type) || member.offset > block.second.size ||
			    size > block.second.size - member.offset)
			{
				return Result<ShaderBindingLayout>::Failure("Shader property " + property->identifier +
				                                            " lies outside its reflected uniform block");
			}
			if (layout.properties.find(property->identifier) != layout.properties.end())
			{
				return Result<ShaderBindingLayout>::Failure("Shader property is bound more than once: " +
				                                            property->identifier);
			}
			layout.properties.emplace(property->identifier,
			                          ShaderPropertyBinding{Rhi::BindingType::UniformBuffer, block.second.binding,
			                                                member.offset, size, true});
		}
		layout.uniformBuffers.push_back(std::move(uniformBuffer));
	}
	for (const PropertyDeclaration& property : shader.properties)
	{
		if (property.type != PropertyType::Texture2D)
		{
			continue;
		}
		for (const auto& resource : resources)
		{
			if (resource.second.name == property.identifier &&
			    (resource.second.type == Rhi::BindingType::SampledTexture ||
			     resource.second.type == Rhi::BindingType::CombinedImageSampler))
			{
				layout.properties.emplace(
				    property.identifier,
				    ShaderPropertyBinding{resource.second.type, resource.second.binding, 0, 0, false});
				break;
			}
		}
	}
	std::sort(layout.descriptors.begin(), layout.descriptors.end(),
	          [](const Rhi::LayoutBinding& left, const Rhi::LayoutBinding& right)
	          { return left.binding < right.binding; });
	std::sort(layout.uniformBuffers.begin(), layout.uniformBuffers.end(),
	          [](const ShaderUniformBufferLayout& left, const ShaderUniformBufferLayout& right)
	          { return left.binding < right.binding; });
	return Result<ShaderBindingLayout>::Success(std::move(layout));
}

Result<Vector<UInt8>> PackMaterialUniformBuffer(const ShaderAsset& shader, const ShaderBindingLayout& layout,
                                                const MaterialInstance& material, UInt32 binding)
{
	const auto uniform =
	    std::find_if(layout.uniformBuffers.begin(), layout.uniformBuffers.end(),
	                 [&](const ShaderUniformBufferLayout& candidate) { return candidate.binding == binding; });
	if (uniform == layout.uniformBuffers.end())
	{
		return Result<Vector<UInt8>>::Failure("Shader has no uniform buffer at binding " +
		                                      ToUrsaString(std::to_string(binding)));
	}
	Vector<UInt8> bytes(uniform->byteSize, 0);
	for (const auto& propertyBinding : layout.properties)
	{
		const ShaderPropertyBinding& bindingInfo = propertyBinding.second;
		if (!bindingInfo.uniformMember || bindingInfo.binding != binding)
		{
			continue;
		}
		auto value = material.PropertyValue(propertyBinding.first, shader);
		if (!value)
		{
			return Result<Vector<UInt8>>::Failure(value.error);
		}
		const void* source = nullptr;
		UInt32 size = 0;
		switch (value.value.type)
		{
			case PropertyType::Float:
			case PropertyType::Range:
				source = &value.value.floatValue;
				size = sizeof(Float32);
				break;
			case PropertyType::Int:
				source = &value.value.intValue;
				size = sizeof(Int32);
				break;
			case PropertyType::Color:
			case PropertyType::Vector:
				source = value.value.vectorValue.data();
				size = sizeof(Float32) * 4;
				break;
			case PropertyType::Texture2D:
				return Result<Vector<UInt8>>::Failure("Texture property cannot be packed into a uniform buffer");
		}
		if (size > bindingInfo.byteSize || bindingInfo.byteOffset > bytes.size() ||
		    size > bytes.size() - bindingInfo.byteOffset)
		{
			return Result<Vector<UInt8>>::Failure("Shader property " + propertyBinding.first +
			                                      " has an invalid reflected offset");
		}
		std::memcpy(bytes.data() + bindingInfo.byteOffset, source, size);
	}
	return Result<Vector<UInt8>>::Success(std::move(bytes));
}

Result<void> WriteShaderUniformValue(Vector<UInt8>& destination, const ShaderUniformMember& member, const void* source,
                                     Size byteSize)
{
	if ((!source && byteSize) || byteSize != member.byteSize || member.byteOffset > destination.size() ||
	    byteSize > destination.size() - member.byteOffset)
	{
		return Result<void>::Failure("Uniform value size or reflected offset is invalid: " + member.name);
	}
	if (member.type == "mat4" || member.type == "float4x4")
	{
		if (byteSize != 64 || (member.matrixStride != 0 && member.matrixStride != 16))
		{
			return Result<void>::Failure("Unsupported Matrix4x4 uniform layout: " + member.name);
		}
		if (!member.spirvRowMajor)
		{
			Array<Float32, 16> columnMajor;
			Array<Float32, 16> rowMajor;
			std::memcpy(columnMajor.data(), source, sizeof(columnMajor));
			for (Size row = 0; row < 4; ++row)
			{
				for (Size column = 0; column < 4; ++column)
				{
					rowMajor[row * 4 + column] = columnMajor[column * 4 + row];
				}
			}
			std::memcpy(destination.data() + member.byteOffset, rowMajor.data(), sizeof(rowMajor));
			return {};
		}
	}
	if (byteSize)
	{
		std::memcpy(destination.data() + member.byteOffset, source, byteSize);
	}
	return {};
}

Result<Vector<MaterialTextureBinding>> BuildMaterialTextureBindings(const ShaderAsset& shader,
                                                                    const ShaderBindingLayout& layout,
                                                                    const MaterialInstance& material)
{
	if (!material.IsShaderBacked() || material.ShaderId() != shader.id)
	{
		return Result<Vector<MaterialTextureBinding>>::Failure("Material does not reference this Shader asset");
	}
	Vector<MaterialTextureBinding> result;
	for (const auto& overrideValue : material.PropertyOverrides())
	{
		const auto property =
		    std::find_if(shader.properties.begin(), shader.properties.end(),
		                 [&](const PropertyDeclaration& value) { return value.identifier == overrideValue.first; });
		if (property == shader.properties.end() || property->type != PropertyType::Texture2D)
		{
			continue;
		}
		const auto binding = layout.properties.find(overrideValue.first);
		if (binding == layout.properties.end() ||
		    (binding->second.bindingType != Rhi::BindingType::SampledTexture &&
		     binding->second.bindingType != Rhi::BindingType::CombinedImageSampler))
		{
			return Result<Vector<MaterialTextureBinding>>::Failure(
			    "Material texture property has no reflected binding: " + overrideValue.first);
		}
		if (!overrideValue.second.textureId || !overrideValue.second.texture)
		{
			return Result<Vector<MaterialTextureBinding>>::Failure(
			    "Material texture resource is missing for property " + overrideValue.first);
		}
		result.push_back({overrideValue.first, binding->second.binding, overrideValue.second.textureId,
		                  overrideValue.second.texture});
	}
	return Result<Vector<MaterialTextureBinding>>::Success(std::move(result));
}

Result<Rhi::LayoutPtr> CreateShaderBindGroupLayout(const ShaderBindingLayout& layout, Rhi::Device& device)
{
	return device.CreateBindGroupLayout(layout.descriptors);
}
} // namespace Ursa::Shader
