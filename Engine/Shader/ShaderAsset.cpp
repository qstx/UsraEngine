#include <Shader/ShaderAsset.hpp>
#include <Shader/ShaderReflection.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/JsonArchiveInterop.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace Ursa::Shader
{
namespace
{
using Json = nlohmann::json;

bool ValidSpirv(const Vector<UInt8>& bytes);

String PropertyTypeName(PropertyType type)
{
	switch (type)
	{
		case PropertyType::Float:
			return "Float";
		case PropertyType::Range:
			return "Range";
		case PropertyType::Int:
			return "Int";
		case PropertyType::Color:
			return "Color";
		case PropertyType::Vector:
			return "Vector";
		case PropertyType::Texture2D:
			return "Texture2D";
	}
	return "Unknown";
}

String StableDigest(const String& value)
{
	UInt64 first = 14695981039346656037ull;
	UInt64 second = 0x6a09e667f3bcc909ull;
	for (const unsigned char byte : value)
	{
		first = (first ^ byte) * 1099511628211ull;
		second ^= static_cast<UInt64>(byte) + 0x9e3779b97f4a7c15ull + (second << 6) + (second >> 2);
		second *= 0x100000001b3ull;
	}
	static constexpr char digits[] = "0123456789abcdef";
	String result = "v1-";
	for (UInt64 part : {first, second})
	{
		for (Int32 shift = 60; shift >= 0; shift -= 4)
		{
			result.push_back(digits[(part >> shift) & 0xf]);
		}
	}
	return result;
}

String PassIdentity(ResourceId shader, UInt32 subShaderIndex, UInt32 passIndex, const String& name)
{
	String key = "UrsaShaderPassV1|";
	key += shader.ToString();
	key += "|";
	key += ToUrsaString(std::to_string(subShaderIndex));
	key += "|";
	key += ToUrsaString(std::to_string(passIndex));
	key += "|";
	key += name;
	return StableDigest(key);
}

Result<String> ReflectionIdentity(const ShaderStageAsset& stage)
{
	const Json reflection = Json::parse(stage.reflectionJson.begin(), stage.reflectionJson.end(), nullptr, false);
	if (reflection.is_discarded() || !reflection.is_object())
	{
		return Result<String>::Failure("Invalid Shader reflection JSON");
	}
	String key = "UrsaShaderReflectionV";
	key += ToUrsaString(std::to_string(stage.reflectionVersion));
	key += "|";
	key += ToUrsaString(std::to_string(static_cast<UInt32>(stage.target)));
	key += "|";
	key += ToUrsaString(std::to_string(static_cast<UInt32>(stage.stage)));
	key += "|";
	key += stage.entryPoint;
	key += "|";
	key += ToUrsaString(reflection.dump());
	return Result<String>::Success(StableDigest(key));
}

Result<String> LayoutIdentity(const ShaderAsset& shader, const ShaderVariantAsset& variant)
{
	auto built = BuildShaderBindingLayout(shader, variant);
	if (!built)
	{
		return Result<String>::Failure(built.error);
	}
	Json layout;
	layout["descriptors"] = Json::array();
	for (const Rhi::LayoutBinding& descriptor : built.value.descriptors)
	{
		layout["descriptors"].push_back({{"binding", descriptor.binding},
		                                 {"type", static_cast<UInt32>(descriptor.type)},
		                                 {"count", descriptor.count},
		                                 {"stages", descriptor.stages}});
	}
	layout["uniformBuffers"] = Json::array();
	for (const ShaderUniformBufferLayout& buffer : built.value.uniformBuffers)
	{
		Json members = Json::array();
		for (const ShaderUniformMember& member : buffer.members)
		{
			members.push_back({{"name", ToStdString(member.name)},
			                   {"type", ToStdString(member.type)},
			                   {"offset", member.byteOffset},
			                   {"size", member.byteSize},
			                   {"arrayCount", member.arrayCount},
			                   {"arrayStride", member.arrayStride}});
		}
		layout["uniformBuffers"].push_back({{"binding", buffer.binding},
		                                    {"size", buffer.byteSize},
		                                    {"stages", buffer.stages},
		                                    {"members", std::move(members)}});
	}
	layout["vertexInputs"] = Json::array();
	for (const ShaderVertexInput& input : built.value.vertexInputs)
	{
		layout["vertexInputs"].push_back({{"location", input.location},
		                                  {"semantic", ToStdString(input.semantic)},
		                                  {"format", static_cast<UInt32>(input.format)}});
	}
	return Result<String>::Success(StableDigest(ToUrsaString(layout.dump())));
}

Result<PropertyType> ParsePropertyType(const String& value)
{
	if (value == "Float")
	{
		return Result<PropertyType>::Success(PropertyType::Float);
	}
	if (value == "Range")
	{
		return Result<PropertyType>::Success(PropertyType::Range);
	}
	if (value == "Int")
	{
		return Result<PropertyType>::Success(PropertyType::Int);
	}
	if (value == "Color")
	{
		return Result<PropertyType>::Success(PropertyType::Color);
	}
	if (value == "Vector")
	{
		return Result<PropertyType>::Success(PropertyType::Vector);
	}
	if (value == "Texture2D")
	{
		return Result<PropertyType>::Success(PropertyType::Texture2D);
	}
	return Result<PropertyType>::Failure("Unknown Shader property type " + value);
}

String StageName(ProgramStage stage)
{
	switch (stage)
	{
		case ProgramStage::Vertex:
			return "vertex";
		case ProgramStage::Fragment:
			return "fragment";
		case ProgramStage::Compute:
			return "compute";
	}
	return "unknown";
}

Result<ProgramStage> ParseStage(const String& value)
{
	if (value == "vertex")
	{
		return Result<ProgramStage>::Success(ProgramStage::Vertex);
	}
	if (value == "fragment")
	{
		return Result<ProgramStage>::Success(ProgramStage::Fragment);
	}
	if (value == "compute")
	{
		return Result<ProgramStage>::Success(ProgramStage::Compute);
	}
	return Result<ProgramStage>::Failure("Unknown Shader stage " + value);
}

String TargetName(ShaderTarget target)
{
	switch (target)
	{
		case ShaderTarget::VulkanSpirv:
			return "VulkanSpirv";
		case ShaderTarget::OpenGlGlsl:
			return "OpenGlGlsl";
		case ShaderTarget::D3D12Dxil:
			return "D3D12Dxil";
	}
	return "Unknown";
}

Result<ShaderTarget> ParseTarget(const String& value)
{
	if (value == "VulkanSpirv")
	{
		return Result<ShaderTarget>::Success(ShaderTarget::VulkanSpirv);
	}
	if (value == "OpenGlGlsl")
	{
		return Result<ShaderTarget>::Success(ShaderTarget::OpenGlGlsl);
	}
	if (value == "D3D12Dxil")
	{
		return Result<ShaderTarget>::Success(ShaderTarget::D3D12Dxil);
	}
	return Result<ShaderTarget>::Failure("Unknown Shader target " + value);
}

Json TagsToJson(const Vector<TagDeclaration>& tags)
{
	Json values = Json::array();
	for (const TagDeclaration& tag : tags)
	{
		values.push_back({{"name", ToStdString(tag.name)}, {"value", ToStdString(tag.value)}});
	}
	return values;
}

Json StatesToJson(const Vector<RenderStateDeclaration>& states)
{
	Json values = Json::array();
	for (const RenderStateDeclaration& state : states)
	{
		values.push_back({{"name", ToStdString(state.name)}, {"value", ToStdString(state.value)}});
	}
	return values;
}

Json BytesToJson(const Vector<UInt8>& bytes)
{
	return Json::binary(std::vector<UInt8>(bytes.begin(), bytes.end()));
}

Json ToJson(const ShaderAsset& shader)
{
	Json properties = Json::array();
	for (const PropertyDeclaration& property : shader.properties)
	{
		Json attributes = Json::array();
		for (const String& attribute : property.attributes)
		{
			attributes.push_back(ToStdString(attribute));
		}
		Json values = Json::array();
		for (Float32 component : property.defaultVector)
		{
			values.push_back(component);
		}
		properties.push_back({{"identifier", ToStdString(property.identifier)},
		                      {"displayName", ToStdString(property.displayName)},
		                      {"type", ToStdString(PropertyTypeName(property.type))},
		                      {"rangeMinimum", property.rangeMinimum},
		                      {"rangeMaximum", property.rangeMaximum},
		                      {"defaultFloat", property.defaultFloat},
		                      {"defaultInt", property.defaultInt},
		                      {"defaultVector", std::move(values)},
		                      {"defaultTexture", ToStdString(property.defaultTexture)},
		                      {"textureOptions", ToStdString(property.textureOptions)},
		                      {"attributes", std::move(attributes)},
		                      {"line", property.location.line},
		                      {"column", property.location.column}});
	}
	Json subShaders = Json::array();
	for (const ShaderSubShaderAsset& subShader : shader.subShaders)
	{
		Json passes = Json::array();
		Json passOrder = Json::array();
		for (const SubShaderPassOrderEntry& entry : subShader.passOrder)
		{
			passOrder.push_back({{"localPassIndex", entry.localPassIndex},
			                     {"reusedPass", ToStdString(entry.reusedPass)},
			                     {"isReused", entry.isReused}});
		}
		for (const ShaderPassAsset& pass : subShader.passes)
		{
			Json threadGroupSize{pass.threadGroupSize[0], pass.threadGroupSize[1], pass.threadGroupSize[2]};
			Json reusedPasses = Json::array();
			for (const String& reusedPass : pass.reusedPasses)
			{
				reusedPasses.push_back(ToStdString(reusedPass));
			}
			Json variants = Json::array();
			for (const ShaderVariantAsset& variant : pass.variants)
			{
				Json keywords = Json::array();
				for (const String& keyword : variant.keywords)
				{
					keywords.push_back(ToStdString(keyword));
				}
				Json stages = Json::array();
				for (const ShaderStageAsset& stage : variant.stages)
				{
					stages.push_back({{"target", ToStdString(TargetName(stage.target))},
					                  {"stage", ToStdString(StageName(stage.stage))},
					                  {"entryPoint", ToStdString(stage.entryPoint)},
					                  {"reflectionVersion", stage.reflectionVersion},
					                  {"reflectionIdentity", ToStdString(stage.reflectionIdentity)},
					                  {"spirv", BytesToJson(stage.spirv)},
					                  {"reflectionJson", ToStdString(stage.reflectionJson)}});
				}
				variants.push_back({{"keywords", std::move(keywords)},
				                    {"stages", std::move(stages)},
				                    {"required", variant.required},
				                    {"layoutIdentity", ToStdString(variant.layoutIdentity)}});
			}
			passes.push_back({{"name", ToStdString(pass.name)},
			                  {"identity", ToStdString(pass.identity)},
			                  {"computeKernel", pass.computeKernel},
			                  {"threadGroupSize", std::move(threadGroupSize)},
			                  {"tags", TagsToJson(pass.tags)},
			                  {"renderStates", StatesToJson(pass.renderStates)},
			                  {"reusedPasses", std::move(reusedPasses)},
			                  {"variants", std::move(variants)}});
		}
		subShaders.push_back({{"tags", TagsToJson(subShader.tags)},
		                      {"renderStates", StatesToJson(subShader.renderStates)},
		                      {"passes", std::move(passes)},
		                      {"passOrder", std::move(passOrder)}});
	}
	Json references = Json::array();
	for (const ShaderReference& reference : shader.references)
	{
		references.push_back({{"shaderName", ToStdString(reference.shaderName)},
		                      {"id", ToStdString(reference.id.ToString())},
		                      {"passName", ToStdString(reference.passName)},
		                      {"fallback", reference.fallback}});
	}
	Json includes = Json::array();
	for (const String& include : shader.includeDependencies)
	{
		includes.push_back(ToStdString(include));
	}
	return {{"serializationVersion", 4},
	        {"id", ToStdString(shader.id.ToString())},
	        {"name", ToStdString(shader.name)},
	        {"customEditor", ToStdString(shader.customEditor)},
	        {"sourcePath", ToStdString(shader.sourcePath)},
	        {"sourceFormatVersion", shader.sourceFormatVersion},
	        {"sourceHash", ToStdString(shader.sourceHash)},
	        {"compilerVersion", ToStdString(shader.compilerVersion)},
	        {"properties", std::move(properties)},
	        {"subShaders", std::move(subShaders)},
	        {"references", std::move(references)},
	        {"includeDependencies", std::move(includes)}};
}

Result<String> Text(const Json& object, const char* field)
{
	if (!object.is_object() || !object.contains(field) || !object[field].is_string())
	{
		return Result<String>::Failure(String("Invalid Shader field ") + field);
	}
	return Result<String>::Success(ToUrsaString(object[field].get<std::string>()));
}

Result<UInt32> Unsigned(const Json& object, const char* field)
{
	if (!object.is_object() || !object.contains(field) || !object[field].is_number_unsigned() ||
	    object[field].get<UInt64>() > std::numeric_limits<UInt32>::max())
	{
		return Result<UInt32>::Failure(String("Invalid Shader field ") + field);
	}
	return Result<UInt32>::Success(object[field].get<UInt32>());
}

Result<Float32> Float(const Json& object, const char* field)
{
	if (!object.is_object() || !object.contains(field) || !object[field].is_number())
	{
		return Result<Float32>::Failure(String("Invalid Shader field ") + field);
	}
	const Float32 value = object[field].get<Float32>();
	return std::isfinite(value) ? Result<Float32>::Success(value)
	                            : Result<Float32>::Failure(String("Non-finite Shader field ") + field);
}

Result<ResourceId> Id(const Json& object, const char* field)
{
	auto text = Text(object, field);
	return text ? ResourceId::Parse(text.value) : Result<ResourceId>::Failure(text.error);
}

Result<Vector<String>> StringArray(const Json& object, const char* field)
{
	if (!object.is_object() || !object.contains(field) || !object[field].is_array())
	{
		return Result<Vector<String>>::Failure(String("Invalid Shader array ") + field);
	}
	Vector<String> values;
	for (const Json& value : object[field])
	{
		if (!value.is_string())
		{
			return Result<Vector<String>>::Failure(String("Invalid Shader string in ") + field);
		}
		values.push_back(ToUrsaString(value.get<std::string>()));
	}
	return Result<Vector<String>>::Success(std::move(values));
}

Result<Vector<UInt8>> Bytes(const Json& value)
{
	if (!value.is_object() || !value.contains("$ursaBytes") || !value["$ursaBytes"].is_array())
	{
		return Result<Vector<UInt8>>::Failure("Invalid Shader stage bytecode block");
	}
	Vector<UInt8> bytes;
	bytes.reserve(value["$ursaBytes"].size());
	for (const Json& entry : value["$ursaBytes"])
	{
		if (!entry.is_number_integer())
		{
			return Result<Vector<UInt8>>::Failure("Invalid Shader stage bytecode value");
		}
		const Int64 byte = entry.get<Int64>();
		if (byte < 0 || byte > 255)
		{
			return Result<Vector<UInt8>>::Failure("Shader stage bytecode value is outside [0, 255]");
		}
		bytes.push_back(static_cast<UInt8>(byte));
	}
	return Result<Vector<UInt8>>::Success(std::move(bytes));
}

Result<Vector<TagDeclaration>> DecodeTags(const Json& values)
{
	if (!values.is_array())
	{
		return Result<Vector<TagDeclaration>>::Failure("Invalid Shader Tags");
	}
	Vector<TagDeclaration> tags;
	for (const Json& value : values)
	{
		auto name = Text(value, "name");
		auto text = Text(value, "value");
		if (!name || !text)
		{
			return Result<Vector<TagDeclaration>>::Failure(!name ? name.error : text.error);
		}
		tags.push_back({std::move(name.value), std::move(text.value)});
	}
	return Result<Vector<TagDeclaration>>::Success(std::move(tags));
}

Result<Vector<RenderStateDeclaration>> DecodeStates(const Json& values)
{
	if (!values.is_array())
	{
		return Result<Vector<RenderStateDeclaration>>::Failure("Invalid Shader render states");
	}
	Vector<RenderStateDeclaration> states;
	for (const Json& value : values)
	{
		auto name = Text(value, "name");
		auto text = Text(value, "value");
		if (!name || !text)
		{
			return Result<Vector<RenderStateDeclaration>>::Failure(!name ? name.error : text.error);
		}
		states.push_back({std::move(name.value), std::move(text.value), {}});
	}
	return Result<Vector<RenderStateDeclaration>>::Success(std::move(states));
}

Result<ShaderAsset> DecodeShader(const Json& document)
{
	if (!document.is_object())
	{
		return Result<ShaderAsset>::Failure("Shader archive root must be an object");
	}
	ShaderAsset shader;
	auto version = Unsigned(document, "serializationVersion");
	auto id = Id(document, "id");
	auto name = Text(document, "name");
	auto sourcePath = Text(document, "sourcePath");
	auto sourceFormatVersion = Unsigned(document, "sourceFormatVersion");
	auto sourceHash = Text(document, "sourceHash");
	auto compilerVersion = Text(document, "compilerVersion");
	if (!version || version.value < 1 || version.value > shader.SerializationVersion() || !id || !name || !sourcePath ||
	    !sourceFormatVersion || !sourceHash || !compilerVersion)
	{
		return Result<ShaderAsset>::Failure(!version               ? version.error
		                                    : !id                  ? id.error
		                                    : !name                ? name.error
		                                    : !sourcePath          ? sourcePath.error
		                                    : !sourceFormatVersion ? sourceFormatVersion.error
		                                    : !sourceHash          ? sourceHash.error
		                                    : !compilerVersion     ? compilerVersion.error
		                                                           : "Unsupported Shader serialization version");
	}
	if (!VirtualPath::Parse(sourcePath.value))
	{
		return Result<ShaderAsset>::Failure("Invalid Shader source virtual path " + sourcePath.value);
	}
	shader.id = id.value;
	shader.name = std::move(name.value);
	if (version.value >= 3)
	{
		auto customEditor = Text(document, "customEditor");
		if (!customEditor)
		{
			return Result<ShaderAsset>::Failure(customEditor.error);
		}
		shader.customEditor = std::move(customEditor.value);
	}
	shader.sourcePath = std::move(sourcePath.value);
	shader.sourceFormatVersion = sourceFormatVersion.value;
	shader.sourceHash = std::move(sourceHash.value);
	shader.compilerVersion = std::move(compilerVersion.value);
	if (!document.contains("properties") || !document["properties"].is_array() || !document.contains("subShaders") ||
	    !document["subShaders"].is_array() || !document.contains("references") || !document["references"].is_array() ||
	    !document.contains("includeDependencies") || !document["includeDependencies"].is_array())
	{
		return Result<ShaderAsset>::Failure("Shader archive is missing a required collection");
	}
	for (const Json& value : document["properties"])
	{
		PropertyDeclaration property;
		auto identifier = Text(value, "identifier");
		auto displayName = Text(value, "displayName");
		auto typeName = Text(value, "type");
		auto minimum = Float(value, "rangeMinimum");
		auto maximum = Float(value, "rangeMaximum");
		auto defaultFloat = Float(value, "defaultFloat");
		auto defaultInt = value.contains("defaultInt") && value["defaultInt"].is_number_integer()
		                      ? Result<Int32>::Success(value["defaultInt"].get<Int32>())
		                      : Result<Int32>::Failure("Invalid Shader property defaultInt");
		auto defaultTexture = Text(value, "defaultTexture");
		auto textureOptions = Text(value, "textureOptions");
		auto attributes = StringArray(value, "attributes");
		auto line = Unsigned(value, "line");
		auto column = Unsigned(value, "column");
		if (!identifier || !displayName || !typeName || !minimum || !maximum || !defaultFloat || !defaultInt ||
		    !defaultTexture || !textureOptions || !attributes || !line || !column)
		{
			return Result<ShaderAsset>::Failure(!identifier       ? identifier.error
			                                    : !displayName    ? displayName.error
			                                    : !typeName       ? typeName.error
			                                    : !minimum        ? minimum.error
			                                    : !maximum        ? maximum.error
			                                    : !defaultFloat   ? defaultFloat.error
			                                    : !defaultInt     ? defaultInt.error
			                                    : !defaultTexture ? defaultTexture.error
			                                    : !textureOptions ? textureOptions.error
			                                    : !attributes     ? attributes.error
			                                    : !line           ? line.error
			                                                      : column.error);
		}
		auto type = ParsePropertyType(typeName.value);
		if (!type)
		{
			return Result<ShaderAsset>::Failure(type.error);
		}
		if (!value.contains("defaultVector") || !value["defaultVector"].is_array() ||
		    value["defaultVector"].size() != property.defaultVector.size())
		{
			return Result<ShaderAsset>::Failure("Invalid Shader property defaultVector");
		}
		for (Ursa::Size index = 0; index < property.defaultVector.size(); ++index)
		{
			if (!value["defaultVector"][index].is_number())
			{
				return Result<ShaderAsset>::Failure("Invalid Shader property defaultVector component");
			}
			property.defaultVector[index] = value["defaultVector"][index].get<Float32>();
			if (!std::isfinite(property.defaultVector[index]))
			{
				return Result<ShaderAsset>::Failure("Non-finite Shader property defaultVector component");
			}
		}
		property.identifier = std::move(identifier.value);
		property.displayName = std::move(displayName.value);
		property.type = type.value;
		property.rangeMinimum = minimum.value;
		property.rangeMaximum = maximum.value;
		property.defaultFloat = defaultFloat.value;
		property.defaultInt = defaultInt.value;
		property.defaultTexture = std::move(defaultTexture.value);
		property.textureOptions = std::move(textureOptions.value);
		property.attributes = std::move(attributes.value);
		property.location = {line.value, column.value};
		shader.properties.push_back(std::move(property));
	}
	for (UInt32 subShaderIndex = 0; subShaderIndex < document["subShaders"].size(); ++subShaderIndex)
	{
		const Json& subValue = document["subShaders"][subShaderIndex];
		ShaderSubShaderAsset subShader;
		if (!subValue.contains("tags") || !subValue.contains("renderStates") || !subValue.contains("passes") ||
		    !subValue.contains("passOrder") || !subValue["passOrder"].is_array() || !subValue["passes"].is_array())
		{
			return Result<ShaderAsset>::Failure("Invalid Shader SubShader metadata");
		}
		auto tags = DecodeTags(subValue["tags"]);
		auto states = DecodeStates(subValue["renderStates"]);
		if (!tags || !states)
		{
			return Result<ShaderAsset>::Failure(!tags ? tags.error : states.error);
		}
		subShader.tags = std::move(tags.value);
		subShader.renderStates = std::move(states.value);
		for (const Json& orderValue : subValue["passOrder"])
		{
			auto localIndex = Unsigned(orderValue, "localPassIndex");
			auto reusedPass = Text(orderValue, "reusedPass");
			if (!localIndex || !reusedPass || !orderValue.contains("isReused") || !orderValue["isReused"].is_boolean())
			{
				return Result<ShaderAsset>::Failure("Invalid Shader SubShader pass order entry");
			}
			subShader.passOrder.push_back(
			    {localIndex.value, std::move(reusedPass.value), orderValue["isReused"].get<bool>()});
		}
		for (UInt32 passIndex = 0; passIndex < subValue["passes"].size(); ++passIndex)
		{
			const Json& passValue = subValue["passes"][passIndex];
			ShaderPassAsset pass;
			auto passName = Text(passValue, "name");
			if (!passName)
			{
				return Result<ShaderAsset>::Failure(passName.error);
			}
			pass.name = std::move(passName.value);
			if (version.value >= 4)
			{
				auto identity = Text(passValue, "identity");
				if (!identity || identity.value != PassIdentity(shader.id, subShaderIndex, passIndex, pass.name))
				{
					return Result<ShaderAsset>::Failure("Shader Pass identity is invalid");
				}
				pass.identity = std::move(identity.value);
				auto computeKernel = passValue.value("computeKernel", false);
				if (!passValue.contains("computeKernel") || !passValue["computeKernel"].is_boolean() ||
				    !passValue.contains("threadGroupSize") || !passValue["threadGroupSize"].is_array() ||
				    passValue["threadGroupSize"].size() != 3)
				{
					return Result<ShaderAsset>::Failure("Invalid Shader kernel metadata");
				}
				pass.computeKernel = computeKernel;
				for (Ursa::Size dimension = 0; dimension < pass.threadGroupSize.size(); ++dimension)
				{
					if (!passValue["threadGroupSize"][dimension].is_number_unsigned() &&
					    !passValue["threadGroupSize"][dimension].is_number_integer())
					{
						return Result<ShaderAsset>::Failure("Invalid Shader kernel thread-group size");
					}
					const Int64 value = passValue["threadGroupSize"][dimension].get<Int64>();
					if (value <= 0 || value > 1024)
					{
						return Result<ShaderAsset>::Failure("Shader kernel thread-group size is out of range");
					}
					pass.threadGroupSize[dimension] = static_cast<UInt32>(value);
				}
			}
			if (!passValue.contains("tags") || !passValue.contains("renderStates") ||
			    !passValue.contains("reusedPasses") || !passValue.contains("variants") ||
			    !passValue["variants"].is_array())
			{
				return Result<ShaderAsset>::Failure("Invalid Shader Pass metadata");
			}
			auto passTags = DecodeTags(passValue["tags"]);
			auto passStates = DecodeStates(passValue["renderStates"]);
			auto reused = StringArray(passValue, "reusedPasses");
			if (!passTags || !passStates || !reused)
			{
				return Result<ShaderAsset>::Failure(!passTags     ? passTags.error
				                                    : !passStates ? passStates.error
				                                                  : reused.error);
			}
			pass.tags = std::move(passTags.value);
			pass.renderStates = std::move(passStates.value);
			pass.reusedPasses = std::move(reused.value);
			for (const Json& variantValue : passValue["variants"])
			{
				ShaderVariantAsset variant;
				auto keywords = StringArray(variantValue, "keywords");
				if (!keywords || !variantValue.contains("stages") || !variantValue["stages"].is_array())
				{
					return Result<ShaderAsset>::Failure(keywords ? "Invalid Shader variant stages" : keywords.error);
				}
				variant.keywords = std::move(keywords.value);
				if (version.value >= 4)
				{
					auto layoutIdentity = Text(variantValue, "layoutIdentity");
					if (!layoutIdentity || layoutIdentity.value.empty())
					{
						return Result<ShaderAsset>::Failure("Shader variant layout identity is missing");
					}
					variant.layoutIdentity = std::move(layoutIdentity.value);
				}
				if (version.value >= 2)
				{
					if (!variantValue.contains("required") || !variantValue["required"].is_boolean())
					{
						return Result<ShaderAsset>::Failure("Invalid Shader variant inclusion policy");
					}
					variant.required = variantValue["required"].get<bool>();
				}
				for (const Json& stageValue : variantValue["stages"])
				{
					ShaderStageAsset stage;
					auto targetName = Text(stageValue, "target");
					auto stageName = Text(stageValue, "stage");
					auto entryPoint = Text(stageValue, "entryPoint");
					auto reflectionJson = Text(stageValue, "reflectionJson");
					auto reflectionVersion = version.value >= 4
					                             ? Unsigned(stageValue, "reflectionVersion")
					                             : Result<UInt32>::Success(shaderReflectionFormatVersion);
					auto reflectionIdentity =
					    version.value >= 4 ? Text(stageValue, "reflectionIdentity") : Result<String>::Success({});
					if (!targetName || !stageName || !entryPoint || !reflectionJson || !reflectionVersion ||
					    !reflectionIdentity || !stageValue.contains("spirv"))
					{
						return Result<ShaderAsset>::Failure(!targetName       ? targetName.error
						                                    : !stageName      ? stageName.error
						                                    : !entryPoint     ? entryPoint.error
						                                    : !reflectionJson ? reflectionJson.error
						                                                      : "Missing Shader bytecode");
					}
					auto target = ParseTarget(targetName.value);
					auto parsedStage = ParseStage(stageName.value);
					auto bytecode = Bytes(stageValue["spirv"]);
					if (!target || !parsedStage || !bytecode || !ValidSpirv(bytecode.value))
					{
						return Result<ShaderAsset>::Failure(!target        ? target.error
						                                    : !parsedStage ? parsedStage.error
						                                    : !bytecode    ? bytecode.error
						                                                   : "Invalid Shader SPIR-V bytes");
					}
					auto reflection =
					    Json::parse(reflectionJson.value.begin(), reflectionJson.value.end(), nullptr, false);
					if (reflection.is_discarded() || !reflection.is_object())
					{
						return Result<ShaderAsset>::Failure("Invalid Shader SPIR-V reflection JSON");
					}
					stage.target = target.value;
					stage.stage = parsedStage.value;
					stage.entryPoint = std::move(entryPoint.value);
					if (reflectionVersion.value != shaderReflectionFormatVersion)
					{
						return Result<ShaderAsset>::Failure("Unsupported Shader reflection version");
					}
					stage.reflectionVersion = static_cast<UInt32>(reflectionVersion.value);
					stage.spirv = std::move(bytecode.value);
					stage.reflectionJson = std::move(reflectionJson.value);
					auto expectedReflectionIdentity = ReflectionIdentity(stage);
					if (!expectedReflectionIdentity ||
					    (version.value >= 4 && expectedReflectionIdentity.value != reflectionIdentity.value))
					{
						return Result<ShaderAsset>::Failure("Shader reflection identity is invalid");
					}
					stage.reflectionIdentity = version.value >= 4 ? std::move(reflectionIdentity.value)
					                                              : std::move(expectedReflectionIdentity.value);
					variant.stages.push_back(std::move(stage));
				}
				if (version.value >= 4)
				{
					auto expectedLayoutIdentity = LayoutIdentity(shader, variant);
					if (!expectedLayoutIdentity || expectedLayoutIdentity.value != variant.layoutIdentity)
					{
						return Result<ShaderAsset>::Failure(expectedLayoutIdentity
						                                        ? "Shader variant layout identity is invalid"
						                                        : expectedLayoutIdentity.error);
					}
				}
				pass.variants.push_back(std::move(variant));
			}
			subShader.passes.push_back(std::move(pass));
		}
		for (const SubShaderPassOrderEntry& entry : subShader.passOrder)
		{
			if ((entry.isReused && entry.reusedPass.empty()) ||
			    (!entry.isReused && entry.localPassIndex >= subShader.passes.size()))
			{
				return Result<ShaderAsset>::Failure("Shader SubShader pass order entry is out of range");
			}
		}
		shader.subShaders.push_back(std::move(subShader));
	}
	for (const Json& referenceValue : document["references"])
	{
		ShaderReference reference;
		auto shaderName = Text(referenceValue, "shaderName");
		auto referenceId = Id(referenceValue, "id");
		auto passName = Text(referenceValue, "passName");
		if (!shaderName || !referenceId || !passName || !referenceValue.contains("fallback") ||
		    !referenceValue["fallback"].is_boolean())
		{
			return Result<ShaderAsset>::Failure(!shaderName    ? shaderName.error
			                                    : !referenceId ? referenceId.error
			                                    : !passName    ? passName.error
			                                                   : "Invalid Shader reference metadata");
		}
		reference.shaderName = std::move(shaderName.value);
		reference.id = referenceId.value;
		reference.passName = std::move(passName.value);
		reference.fallback = referenceValue["fallback"].get<bool>();
		shader.references.push_back(std::move(reference));
	}
	auto includes = StringArray(document, "includeDependencies");
	if (!includes)
	{
		return Result<ShaderAsset>::Failure(includes.error);
	}
	shader.includeDependencies = std::move(includes.value);
	return Result<ShaderAsset>::Success(std::move(shader));
}

bool ValidSpirv(const Vector<UInt8>& bytes)
{
	return bytes.size() >= sizeof(UInt32) && bytes.size() % sizeof(UInt32) == 0 && bytes[0] == 0x03 &&
	       bytes[1] == 0x02 && bytes[2] == 0x23 && bytes[3] == 0x07;
}

Result<String> ShaderNameFromUsePass(const String& reference)
{
	const auto slash = reference.rfind('/');
	if (slash == String::npos || slash == 0 || slash + 1 >= reference.size())
	{
		return Result<String>::Failure("Invalid UsePass reference " + reference);
	}
	return Result<String>::Success(reference.substr(0, slash));
}
} // namespace

String ShaderAsset::SerializationType() const
{
	return "ursa.Shader";
}

UInt32 ShaderAsset::SerializationVersion() const
{
	return 4;
}

Result<void> ShaderAsset::Serialize(ArchiveWriter& writer) const
{
	if (!id || name.empty() || sourcePath.empty() || sourceFormatVersion == 0 || sourceHash.empty() ||
	    compilerVersion.empty())
	{
		return Result<void>::Failure("Shader asset identity, source version, hash, or compiler version is invalid");
	}
	for (UInt32 subShaderIndex = 0; subShaderIndex < subShaders.size(); ++subShaderIndex)
	{
		const ShaderSubShaderAsset& subShader = subShaders[subShaderIndex];
		for (UInt32 passIndex = 0; passIndex < subShader.passes.size(); ++passIndex)
		{
			const ShaderPassAsset& pass = subShader.passes[passIndex];
			if (pass.identity != PassIdentity(id, subShaderIndex, passIndex, pass.name))
			{
				return Result<void>::Failure("Shader Pass has a missing or stale stable identity");
			}
			for (const ShaderVariantAsset& variant : pass.variants)
			{
				if (variant.layoutIdentity.empty())
				{
					return Result<void>::Failure("Shader variant has no stable layout identity");
				}
				auto expectedLayoutIdentity = LayoutIdentity(*this, variant);
				if (!expectedLayoutIdentity || expectedLayoutIdentity.value != variant.layoutIdentity)
				{
					return Result<void>::Failure(expectedLayoutIdentity ? "Shader variant layout identity is stale"
					                                                    : expectedLayoutIdentity.error);
				}
				for (const ShaderStageAsset& stage : variant.stages)
				{
					auto expectedReflectionIdentity = ReflectionIdentity(stage);
					if (stage.reflectionVersion != shaderReflectionFormatVersion || !expectedReflectionIdentity ||
					    stage.reflectionIdentity != expectedReflectionIdentity.value)
					{
						return Result<void>::Failure("Shader stage reflection version or identity is invalid");
					}
				}
			}
		}
	}
	return ArchiveWriteJson(writer, ToJson(*this));
}

Result<void> ShaderAsset::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto document = ArchiveToJson(reader);
	if (!document)
	{
		return Result<void>::Failure(document.error);
	}
	auto decoded = DecodeShader(document.value);
	if (!decoded)
	{
		return Result<void>::Failure(decoded.error);
	}
	*this = std::move(decoded.value);
	return {};
}

Result<ShaderAsset> ShaderAsset::CopyWithIdentity(ResourceId newId) const
{
	if (!newId)
	{
		return Result<ShaderAsset>::Failure("Shader copy requires a nonzero identity");
	}
	ShaderAsset copy = *this;
	copy.id = newId;
	for (UInt32 subIndex = 0; subIndex < copy.subShaders.size(); ++subIndex)
	{
		for (UInt32 passIndex = 0; passIndex < copy.subShaders[subIndex].passes.size(); ++passIndex)
		{
			auto& pass = copy.subShaders[subIndex].passes[passIndex];
			pass.identity = PassIdentity(copy.id, subIndex, passIndex, pass.name);
			for (auto& variant : pass.variants)
			{
				auto layout = LayoutIdentity(copy, variant);
				if (!layout)
				{
					return Result<ShaderAsset>::Failure(layout.error);
				}
				variant.layoutIdentity = std::move(layout.value);
			}
		}
	}
	return Result<ShaderAsset>::Success(std::move(copy));
}

Result<ShaderAsset> BuildShaderAsset(const ShaderSourceAsset& source, const ShaderSourceLibrary& library,
                                     const ShaderDependencyReport& dependencies, const String& sourceHash,
                                     const String& compilerVersion, const Vector<ShaderPassCompilation>& compilations)
{
	if (!source.source.id || source.source.name.empty() || sourceHash.empty() || compilerVersion.empty())
	{
		return Result<ShaderAsset>::Failure("Shader source identity or compiler metadata is missing");
	}
	ShaderAsset asset;
	asset.id = source.source.id;
	asset.name = source.source.name;
	asset.customEditor = source.source.customEditor;
	asset.sourcePath = source.path.ToString();
	asset.sourceFormatVersion = source.source.formatVersion;
	asset.sourceHash = sourceHash;
	asset.compilerVersion = compilerVersion;
	asset.properties = source.source.properties;
	for (const VirtualPath& include : dependencies.includes)
	{
		asset.includeDependencies.push_back(include.ToString());
	}
	if (!source.source.fallback.empty())
	{
		const ShaderSourceAsset* fallback = library.Find(source.source.fallback);
		if (!fallback)
		{
			return Result<ShaderAsset>::Failure(source.path.ToString() + ": missing fallback Shader " +
			                                    source.source.fallback);
		}
		asset.references.push_back({fallback->source.name, fallback->source.id, {}, true});
	}
	for (UInt32 subShaderIndex = 0; subShaderIndex < source.source.subShaders.size(); ++subShaderIndex)
	{
		const SubShaderDeclaration& sourceSubShader = source.source.subShaders[subShaderIndex];
		ShaderSubShaderAsset subShader;
		subShader.tags = sourceSubShader.tags;
		subShader.renderStates = sourceSubShader.renderStates;
		subShader.passOrder = sourceSubShader.passOrder;
		for (const String& reference : sourceSubShader.reusedPasses)
		{
			auto shaderName = ShaderNameFromUsePass(reference);
			if (!shaderName)
			{
				return Result<ShaderAsset>::Failure(shaderName.error);
			}
			const ShaderSourceAsset* reusedShader = library.Find(shaderName.value);
			if (!reusedShader)
			{
				return Result<ShaderAsset>::Failure(source.path.ToString() + ": missing UsePass Shader " +
				                                    shaderName.value);
			}
			const String passName = reference.substr(reference.rfind('/') + 1);
			asset.references.push_back({reusedShader->source.name, reusedShader->source.id, passName, false});
		}
		for (UInt32 passIndex = 0; passIndex < sourceSubShader.passes.size(); ++passIndex)
		{
			const PassDeclaration& sourcePass = sourceSubShader.passes[passIndex];
			ShaderPassAsset pass;
			pass.name = sourcePass.name;
			pass.identity = PassIdentity(asset.id, subShaderIndex, passIndex, pass.name);
			pass.computeKernel = sourcePass.computeKernel;
			pass.threadGroupSize = sourcePass.threadGroupSize;
			pass.tags = sourcePass.tags;
			pass.renderStates = sourcePass.renderStates;
			pass.reusedPasses = sourcePass.reusedPasses;
			Vector<ProgramEntryPoint> expectedStages;
			for (const ProgramBlock& block : sourcePass.programs)
			{
				if (block.kind == ProgramBlockKind::Program)
				{
					expectedStages.insert(expectedStages.end(), block.entryPoints.begin(), block.entryPoints.end());
				}
			}
			for (const ShaderPassCompilation& compilation : compilations)
			{
				if (compilation.subShaderIndex >= source.source.subShaders.size() ||
				    compilation.passIndex >= source.source.subShaders[compilation.subShaderIndex].passes.size())
				{
					return Result<ShaderAsset>::Failure("Shader compilation refers to an invalid Pass index");
				}
				if (&source.source.subShaders[compilation.subShaderIndex] != &sourceSubShader ||
				    &sourceSubShader.passes[compilation.passIndex] != &sourcePass)
				{
					continue;
				}
				ShaderVariantAsset variant;
				variant.keywords = compilation.keywords;
				variant.required = compilation.required;
				std::sort(variant.keywords.begin(), variant.keywords.end());
				variant.keywords.erase(std::unique(variant.keywords.begin(), variant.keywords.end()),
				                       variant.keywords.end());
				for (const CompiledShaderStage& compiled : compilation.stages)
				{
					const auto entry = std::find_if(
					    expectedStages.begin(), expectedStages.end(), [&](const ProgramEntryPoint& expected)
					    { return expected.stage == compiled.stage && expected.name == compiled.entryPoint; });
					if (entry == expectedStages.end() || !ValidSpirv(compiled.spirv))
					{
						return Result<ShaderAsset>::Failure("Compiled stage does not match the declared Shader Pass");
					}
					Json reflection =
					    Json::parse(compiled.reflectionJson.begin(), compiled.reflectionJson.end(), nullptr, false);
					if (reflection.is_discarded() || !reflection.is_object())
					{
						return Result<ShaderAsset>::Failure("Compiled stage has invalid reflection metadata");
					}
					ShaderStageAsset stage{ShaderTarget::VulkanSpirv, compiled.stage, compiled.entryPoint,
					                       compiled.spirv, compiled.reflectionJson};
					auto reflectionIdentity = ReflectionIdentity(stage);
					if (!reflectionIdentity)
					{
						return Result<ShaderAsset>::Failure(reflectionIdentity.error);
					}
					stage.reflectionIdentity = std::move(reflectionIdentity.value);
					variant.stages.push_back(std::move(stage));
				}
				if (variant.stages.size() != expectedStages.size())
				{
					return Result<ShaderAsset>::Failure("Compiled variant is missing a declared Shader stage");
				}
				auto layoutIdentity = LayoutIdentity(asset, variant);
				if (!layoutIdentity)
				{
					return Result<ShaderAsset>::Failure(source.path.ToString() + ": " + layoutIdentity.error);
				}
				variant.layoutIdentity = std::move(layoutIdentity.value);
				pass.variants.push_back(std::move(variant));
			}
			if (!expectedStages.empty() && pass.variants.empty())
			{
				return Result<ShaderAsset>::Failure("Shader Pass has no compiled variants");
			}
			subShader.passes.push_back(std::move(pass));
		}
		asset.subShaders.push_back(std::move(subShader));
	}
	return Result<ShaderAsset>::Success(std::move(asset));
}
} // namespace Ursa::Shader
