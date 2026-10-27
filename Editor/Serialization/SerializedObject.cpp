#include <Serialization/SerializedObject.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <Serialization/RenderResourceReferences.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/RendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <string>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

String LowerAscii(String value)
{
	for (char& character : value)
	{
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	return value;
}

Json ComparablePropertyValue(const Json& value, const Json& descriptor)
{
	if (value.is_null())
	{
		return nullptr;
	}
	const String kind = ToUrsaString(descriptor.value("Kind", std::string{}));
	if (kind == "AssetReference" && value.is_object() && value.contains("guid"))
	{
		const String guid =
		    value.at("guid").is_string() ? LowerAscii(ToUrsaString(value.at("guid").get<std::string>())) : String{};
		UInt64 localFileId = 0;
		if (value.contains("localFileId") && value.at("localFileId").is_number_unsigned())
		{
			localFileId = value.at("localFileId").get<UInt64>();
		}
		else if (value.contains("localFileId") && value.at("localFileId").is_number_integer())
		{
			const auto id = value.at("localFileId").get<Int64>();
			localFileId = id > 0 ? static_cast<UInt64>(id) : 0;
		}
		return {{"guid", ToStdString(guid)}, {"localFileId", localFileId}};
	}
	const auto normalizeBinding = [](const Json& binding) -> Json
	{
		if (!binding.is_object() || !binding.contains("kind") || !binding["kind"].is_string())
		{
			return binding;
		}
		if (binding["kind"] == "asset" && binding.contains("asset") && binding["asset"].is_object())
		{
			const Json& asset = binding["asset"];
			const String guid = asset.value("guid", std::string{}).empty()
			                        ? String{}
			                        : LowerAscii(ToUrsaString(asset.value("guid", std::string{})));
			const UInt64 localFileId = asset.value("localFileId", UInt64(0));
			return {{"kind", "asset"}, {"guid", ToStdString(guid)}, {"localFileId", localFileId}};
		}
		if (binding["kind"] == "local")
		{
			return {{"kind", "local"}, {"id", binding.value("id", std::string{})}};
		}
		return binding;
	};
	if (kind == "NativeMeshBinding")
	{
		return normalizeBinding(value);
	}
	if (kind == "NativeMaterialBinding" && value.is_object() && value.contains("value"))
	{
		return {{"value", normalizeBinding(value["value"])}, {"instantiated", value.value("instantiated", false)}};
	}
	if ((kind == "Array" || kind == "List") &&
	    descriptor.value("ElementKind", std::string{}) == "NativeMaterialBinding" && value.is_array())
	{
		Json normalized = Json::array();
		for (const auto& entry : value)
		{
			normalized.push_back({{"value", normalizeBinding(entry.value("value", Json(nullptr)))},
			                      {"instantiated", entry.value("instantiated", false)}});
		}
		return normalized;
	}
	return value;
}

String PropertyValueState(const Json& value)
{
	if (value.is_null())
	{
		return "Null";
	}
	if (value.is_object() && ((value.contains("missing") && value.at("missing") == true) ||
	                          (value.contains("Missing") && value.at("Missing") == true)))
	{
		return "MissingReference";
	}
	return "Value";
}

Result<Json> DefaultCollectionElement(const Json& descriptor)
{
	const String kind = ToUrsaString(descriptor.value("ElementKind", std::string("Unsupported")));
	if (kind == "Boolean")
	{
		return Result<Json>::Success(false);
	}
	if (kind == "String")
	{
		return Result<Json>::Success("");
	}
	if (kind == "UnsignedInteger")
	{
		return Result<Json>::Success(UInt64(0));
	}
	if (kind == "FloatingPoint")
	{
		return Result<Json>::Success(0.0);
	}
	if (kind == "SignedInteger")
	{
		return Result<Json>::Success(0);
	}
	if (kind == "Enumeration")
	{
		const auto values = descriptor.value("EnumNumericValues", Json::array());
		if (values.is_array() && !values.empty())
		{
			return Result<Json>::Success(values.front());
		}
		return Result<Json>::Success(0);
	}
	if (kind == "AssetReference" || kind == "GameObjectReference" || kind == "ComponentReference" ||
	    kind == "MaterialReference" || kind == "TextureReference" || kind == "MeshReference")
	{
		return Result<Json>::Success(nullptr);
	}
	if (kind == "NativeMaterialBinding")
	{
		return Result<Json>::Success({{"value", nullptr}, {"instantiated", false}});
	}
	return Result<Json>::Failure("Collection element type has no supported default value: " + kind);
}

Json DescribeNativePayload(const Json& data)
{
	Json payload{{"Fields", data}, {"FieldTypes", Json::object()}};
	for (const auto& field : data.items())
	{
		const Json& value = field.value();
		const String kind = value.is_boolean()                                                 ? "Boolean"
		                    : value.is_string()                                                ? "String"
		                    : value.is_number_unsigned()                                       ? "UnsignedInteger"
		                    : value.is_number_integer()                                        ? "SignedInteger"
		                    : value.is_number_float()                                          ? "FloatingPoint"
		                    : value.is_array()                                                 ? "Array"
		                    : value.is_null() || (value.is_object() && value.contains("guid")) ? "AssetReference"
		                                                                                       : "Unsupported";
		payload["FieldTypes"][field.key()] = {{"Kind", ToStdString(kind)},
		                                      {"ElementKind", value.is_array() ? "FloatingPoint" : "Unsupported"}};
	}
	return payload;
}

Json DescribeNativeComponentPayload(const Component& component, const Json& data)
{
	Json payload = DescribeNativePayload(data);
	if ((dynamic_cast<const DirectionalLightComponent*>(&component) ||
	     dynamic_cast<const PointLightComponent*>(&component)) &&
	    payload["FieldTypes"].contains("color"))
	{
		payload["FieldTypes"]["color"] = {
		    {"Kind", "Color"}, {"ElementKind", "Unsupported"}, {"TypeName", "Ursa.Color"}};
	}
	if (dynamic_cast<const MeshFilterComponent*>(&component) && payload["FieldTypes"].contains("mesh"))
	{
		payload["FieldTypes"]["mesh"] = {
		    {"Kind", "NativeMeshBinding"}, {"ElementKind", "Unsupported"}, {"TypeName", "Ursa.Mesh"}};
	}
	else if (dynamic_cast<const RendererComponent*>(&component) && payload["FieldTypes"].contains("materials"))
	{
		payload["FieldTypes"]["materials"] = {
		    {"Kind", "Array"}, {"ElementKind", "NativeMaterialBinding"}, {"ElementTypeName", "Ursa.MaterialBinding"}};
	}
	if (dynamic_cast<const SkinnedMeshRendererComponent*>(&component) && payload["FieldTypes"].contains("mesh"))
	{
		payload["FieldTypes"]["mesh"] = {
		    {"Kind", "NativeMeshBinding"}, {"ElementKind", "Unsupported"}, {"TypeName", "Ursa.Mesh"}};
	}
	return payload;
}

Result<Json::json_pointer> ParsePropertyPath(const String& path)
{
	if (path.rfind("/Fields/", 0) != 0)
	{
		return Result<Json::json_pointer>::Failure("Serialized script paths must begin with /Fields/");
	}
	try
	{
		Json::json_pointer pointer(ToStdString(path));
		return pointer.empty() ? Result<Json::json_pointer>::Failure("Serialized script field path is empty")
		                       : Result<Json::json_pointer>::Success(std::move(pointer));
	}
	catch (const Json::exception& error)
	{
		return Result<Json::json_pointer>::Failure("Invalid serialized script field path: " +
		                                           ToUrsaString(error.what()));
	}
}

String FieldName(const String& path)
{
	const Size firstSeparator = path.find('/', 1);
	if (firstSeparator == String::npos)
	{
		return {};
	}
	const Size secondSeparator = path.find('/', firstSeparator + 1);
	String encoded = path.substr(firstSeparator + 1,
	                             secondSeparator == String::npos ? String::npos : secondSeparator - firstSeparator - 1);
	String decoded;
	for (Size index = 0; index < encoded.size(); ++index)
	{
		if (encoded[index] == '~' && index + 1 < encoded.size())
		{
			++index;
			decoded += encoded[index] == '0' ? '~' : encoded[index] == '1' ? '/' : encoded[index];
		}
		else
		{
			decoded += encoded[index];
		}
	}
	return decoded;
}

bool IsFiniteNumber(const Json& value)
{
	return value.is_number() && std::isfinite(value.get<Float64>());
}

Result<void> ValidateReference(const Json& value, const String& expectedKind)
{
	if (value.is_null())
	{
		return {};
	}
	if (!value.is_object() || !value.contains("$ursaReference") || !value["$ursaReference"].is_object())
	{
		return Result<void>::Failure("Expected a serialized object/component reference");
	}
	const Json& reference = value["$ursaReference"];
	if (!reference.contains("Kind") || !reference["Kind"].is_string() ||
	    reference["Kind"].get<std::string>() != ToStdString(expectedKind) || !reference.contains("Id") ||
	    !reference["Id"].is_string())
	{
		return Result<void>::Failure("Serialized reference kind or ID is invalid");
	}
	auto id = ResourceId::Parse(ToUrsaString(reference["Id"].get<std::string>()));
	return id ? Result<void>{} : Result<void>::Failure(id.error);
}

Result<void> ValidateAssetReference(const Json& value)
{
	if (value.is_null())
	{
		return {};
	}
	if (!value.is_object() || !value.contains("guid") || !value["guid"].is_string() || !value.contains("localFileId") ||
	    !value["localFileId"].is_number_unsigned())
	{
		return Result<void>::Failure("Expected a serialized AssetReference");
	}
	auto id = ResourceId::Parse(ToUrsaString(value["guid"].get<std::string>()));
	return id ? Result<void>{} : Result<void>::Failure(id.error);
}

Result<void> ValidateValue(const Json& value, const String& kind, const String& elementKind, const Json& descriptor)
{
	const std::string type = ToStdString(kind);
	const std::string typeName = descriptor.value("TypeName", std::string{});
	if (value.is_null() && (type == "String" || type == "Array" || type == "List"))
	{
		return {};
	}
	if (type == "Boolean")
	{
		return value.is_boolean() ? Result<void>{} : Result<void>::Failure("Expected a Boolean value");
	}
	if (type == "SignedInteger")
	{
		if (!value.is_number_integer() ||
		    (value.is_number_unsigned() &&
		     value.get<UInt64>() > static_cast<UInt64>(std::numeric_limits<Int64>::max())))
		{
			return Result<void>::Failure("Expected an integer within the signed range");
		}
		const Int64 integer = value.get<Int64>();
		Int64 minimum = std::numeric_limits<Int64>::min();
		Int64 maximum = std::numeric_limits<Int64>::max();
		if (typeName == "sbyte" || typeName == "System.SByte")
		{
			minimum = -128;
			maximum = 127;
		}
		else if (typeName == "short" || typeName == "System.Int16")
		{
			minimum = -32768;
			maximum = 32767;
		}
		else if (typeName == "int" || typeName == "System.Int32")
		{
			minimum = std::numeric_limits<Int32>::min();
			maximum = std::numeric_limits<Int32>::max();
		}
		return integer >= minimum && integer <= maximum
		           ? Result<void>{}
		           : Result<void>::Failure("Signed integer is outside its declared type range");
	}
	if (type == "UnsignedInteger")
	{
		if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<Int64>() < 0))
		{
			return Result<void>::Failure("Expected a nonnegative integer");
		}
		const UInt64 integer = value.get<UInt64>();
		UInt64 maximum = std::numeric_limits<UInt64>::max();
		if (typeName == "byte" || typeName == "System.Byte")
		{
			maximum = 255;
		}
		else if (typeName == "ushort" || typeName == "System.UInt16")
		{
			maximum = 65535;
		}
		else if (typeName == "uint" || typeName == "System.UInt32")
		{
			maximum = std::numeric_limits<UInt32>::max();
		}
		return integer <= maximum ? Result<void>{}
		                          : Result<void>::Failure("Unsigned integer is outside its declared type range");
	}
	if (type == "FloatingPoint")
	{
		if (!IsFiniteNumber(value))
		{
			return Result<void>::Failure("Expected a finite floating-point value");
		}
		const Float64 number = value.get<Float64>();
		return (typeName != "float" && typeName != "System.Single") ||
		               std::abs(number) <= std::numeric_limits<Float32>::max()
		           ? Result<void>{}
		           : Result<void>::Failure("Number exceeds the Single precision range");
	}
	if (type == "String")
	{
		return value.is_string() ? Result<void>{} : Result<void>::Failure("Expected a String value");
	}
	if (type == "Enumeration")
	{
		Json numericDescriptor = descriptor;
		const String underlying = ToUrsaString(descriptor.value("EnumUnderlyingType", std::string{}));
		if (underlying.empty())
		{
			return Result<void>::Failure("Enumeration field has no underlying type descriptor");
		}
		numericDescriptor["TypeName"] = ToStdString(underlying);
		const bool isUnsigned = underlying == "byte" || underlying == "ushort" || underlying == "uint" ||
		                        underlying == "ulong" || underlying == "System.Byte" || underlying == "System.UInt16" ||
		                        underlying == "System.UInt32" || underlying == "System.UInt64";
		return ValidateValue(value, isUnsigned ? "UnsignedInteger" : "SignedInteger", {}, numericDescriptor);
	}
	if (type == "Vector2" || type == "Vector3" || type == "Vector4" || type == "Quaternion" || type == "Color")
	{
		const Size expected = type == "Vector2" ? 2 : type == "Vector3" ? 3 : 4;
		if (!value.is_array() || value.size() != expected)
		{
			return Result<void>::Failure("Serialized vector/color field has an invalid component count");
		}
		for (const Json& component : value)
		{
			if (!IsFiniteNumber(component))
			{
				return Result<void>::Failure("Serialized vector/color field contains a non-finite component");
			}
		}
		return {};
	}
	if (type == "AssetReference")
	{
		return ValidateAssetReference(value);
	}
	if (type == "NativeMeshBinding")
	{
		if (value.is_null())
		{
			return {};
		}
		if (!value.is_object() || !value.contains("kind") || !value["kind"].is_string())
		{
			return Result<void>::Failure("Malformed native Mesh binding");
		}
		if (value["kind"] == "asset")
		{
			return value.contains("asset") ? ValidateAssetReference(value["asset"])
			                               : Result<void>::Failure("Native Mesh asset binding has no identity");
		}
		if (value["kind"] == "local" && value.contains("id") && value["id"].is_string())
		{
			const auto id = ResourceId::Parse(ToUrsaString(value["id"].get<std::string>()));
			return id ? Result<void>{} : Result<void>::Failure(id.error);
		}
		return Result<void>::Failure("Native Mesh binding kind is invalid");
	}
	if (type == "NativeMaterialBinding")
	{
		if (!value.is_object() || !value.contains("value") || !value.contains("instantiated") ||
		    !value["instantiated"].is_boolean())
		{
			return Result<void>::Failure("Malformed native Material binding slot");
		}
		return ValidateValue(value["value"], "NativeMeshBinding", "Unsupported", descriptor);
	}
	if (type == "GameObjectReference")
	{
		return ValidateReference(value, "GameObject");
	}
	if (type == "MeshReference" || type == "MaterialReference" || type == "TextureReference" ||
	    type == "ScriptableObjectReference" || type == "ShaderReference" || type == "ComputeShaderReference")
	{
		if (value.is_null())
		{
			return {};
		}
		const std::string expectedReferenceKind = type.substr(0, type.size() - std::string("Reference").size());
		if (!value.is_object() || !value.contains("$ursaReference") || !value["$ursaReference"].is_object() ||
		    value["$ursaReference"].value("Kind", std::string{}) != expectedReferenceKind ||
		    !value["$ursaReference"].contains("Resource"))
		{
			return Result<void>::Failure("Malformed serialized resource reference");
		}
		const auto& binding = value["$ursaReference"]["Resource"];
		if (type != "MeshReference" && type != "MaterialReference")
		{
			if (binding.is_object() && binding.value("kind", std::string{}) == "asset" && binding.contains("asset"))
			{
				return ValidateAssetReference(binding["asset"]);
			}
			return ValidateAssetReference(binding);
		}
		if (!binding.is_object() || !binding.contains("kind") || !binding["kind"].is_string())
		{
			return Result<void>::Failure("Resource binding kind is missing");
		}
		if (binding["kind"] == "asset")
		{
			return binding.contains("asset") ? ValidateAssetReference(binding["asset"])
			                                 : Result<void>::Failure("Asset binding has no identity");
		}
		if (binding["kind"] != "local" || !binding.contains("id") || !binding["id"].is_string())
		{
			return Result<void>::Failure("Malformed document-local resource binding");
		}
		const auto id = ResourceId::Parse(ToUrsaString(binding["id"].get<std::string>()));
		return id ? Result<void>{} : Result<void>::Failure(id.error);
	}
	if (type == "ComponentReference")
	{
		return ValidateReference(value, "Component");
	}
	if (type == "Array" || type == "List")
	{
		if (!value.is_array())
		{
			return Result<void>::Failure("Serialized array/list field requires an array value");
		}
		for (const Json& item : value)
		{
			Json itemDescriptor = descriptor;
			itemDescriptor["TypeName"] = descriptor.value("ElementTypeName", std::string{});
			auto valid = ValidateValue(item, elementKind, {}, itemDescriptor);
			if (!valid)
			{
				return valid;
			}
		}
		return {};
	}
	return Result<void>::Failure("Unsupported serialized script field type " + kind);
}

Result<void> ValidateLiveReferences(const Json& value, const Json& descriptor, const Component* target,
                                    ResourceSession* resources, ComponentLoadContext* referenceContext)
{
	const std::string kind = descriptor.value("Kind", std::string{});
	if (value.is_null())
	{
		return {};
	}
	if (kind == "Array" || kind == "List")
	{
		Json element = descriptor;
		element["Kind"] = descriptor.value("ElementKind", std::string{});
		element["TypeName"] = descriptor.value("ElementTypeName", std::string{});
		for (const auto& item : value)
		{
			auto valid = ValidateLiveReferences(item, element, target, resources, referenceContext);
			if (!valid)
			{
				return valid;
			}
		}
		return {};
	}
	if (kind == "AssetReference")
	{
		if (!resources)
		{
			return Result<void>::Failure("Asset reference validation requires a ResourceSession");
		}
		auto id = ResourceId::Parse(ToUrsaString(value.at("guid").get<std::string>()));
		auto resolved = resources->ResolveAsset(id.value, value.at("localFileId").get<UInt64>());
		return resolved ? Result<void>{} : Result<void>::Failure(resolved.error);
	}
	if (kind == "NativeMeshBinding")
	{
		if (!referenceContext)
		{
			return Result<void>::Failure("Mesh binding validation requires the owning document context");
		}
		auto reader = ArchiveFromJson(Json{{"mesh", value}});
		if (!reader)
		{
			return Result<void>::Failure(reader.error);
		}
		auto loaded = ReadMeshBinding(reader.value, "mesh", *referenceContext);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	if (kind == "NativeMaterialBinding")
	{
		if (!referenceContext || !value.is_object() || !value.contains("value"))
		{
			return Result<void>::Failure("Material binding validation requires the owning document context");
		}
		auto reader = ArchiveFromJson(Json{{"value", value.at("value")}});
		if (!reader)
		{
			return Result<void>::Failure(reader.error);
		}
		auto loaded = ReadMaterialBinding(reader.value, "value", *referenceContext);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	if (kind == "MeshReference" || kind == "MaterialReference")
	{
		const auto& binding = value.at("$ursaReference").at("Resource");
		if (referenceContext)
		{
			auto reader = ArchiveFromJson(Json{{"value", binding}});
			if (!reader)
			{
				return Result<void>::Failure(reader.error);
			}
			if (kind == "MeshReference")
			{
				auto loaded = ReadMeshBinding(reader.value, "value", *referenceContext);
				return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
			}
			auto loaded = ReadMaterialBinding(reader.value, "value", *referenceContext);
			return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
		}
		if (!resources || binding.at("kind") != "asset")
		{
			return Result<void>::Failure("Resource reference requires the owning document's serialization services");
		}
		const auto reference = AssetReferenceFromJson(binding.at("asset"), "SerializedProperty");
		if (!reference)
		{
			return Result<void>::Failure(reference.error);
		}
		if (kind == "MeshReference")
		{
			auto loaded = resources->LoadMesh(reference.value.guid, reference.value.localFileId);
			return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
		}
		auto loaded = resources->LoadMaterial(reference.value.guid, reference.value.localFileId);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	if (kind == "TextureReference" || kind == "ScriptableObjectReference" || kind == "ShaderReference" ||
	    kind == "ComputeShaderReference")
	{
		if (!resources)
		{
			return Result<void>::Failure("Resource reference validation requires a ResourceSession");
		}
		const auto reference = AssetReferenceFromJson(value.at("$ursaReference").at("Resource"), "SerializedProperty");
		if (!reference)
		{
			return Result<void>::Failure(reference.error);
		}
		if (kind == "TextureReference")
		{
			auto loaded = resources->LoadTexture(reference.value.guid, reference.value.localFileId);
			return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
		}
		if (kind == "ScriptableObjectReference")
		{
			auto loaded = LoadScriptableAssetGraph(*resources, reference.value);
			return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
		}
		if (reference.value.localFileId)
		{
			return Result<void>::Failure("Shader references require a root asset identity");
		}
		const auto shader = resources->LoadShader(reference.value.guid);
		if (!shader)
		{
			return Result<void>::Failure(shader.error);
		}
		for (const auto& subShader : shader.value->subShaders)
		{
			for (const auto& pass : subShader.passes)
			{
				if (pass.computeKernel == (kind == "ComputeShaderReference"))
				{
					return {};
				}
			}
		}
		return Result<void>::Failure("Shader asset does not match its declared graphics/compute kind");
	}
	if (kind != "GameObjectReference" && kind != "ComponentReference")
	{
		return {};
	}
	if (!target)
	{
		return Result<void>::Failure("A standalone asset cannot reference a Scene object without its owning graph");
	}
	auto owner = target->GetGameObject();
	if (!owner)
	{
		return {};
	}
	Vector<GameObject::Ptr> pending;
	if (target->TryGetScene())
	{
		pending = target->TryGetScene()->RootGameObjects();
	}
	else
	{
		while (owner->Parent())
		{
			owner = owner->Parent();
		}
		pending = {owner};
	}
	const auto& reference = value.at("$ursaReference");
	const String id = ToUrsaString(reference.at("Id").get<std::string>());
	const String expectedType = ToUrsaString(descriptor.value("TypeName", std::string{}));
	while (!pending.empty())
	{
		auto object = std::move(pending.back());
		pending.pop_back();
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
		if (kind == "GameObjectReference" && object->Id().ToString() == id)
		{
			return {};
		}
		if (kind != "ComponentReference")
		{
			continue;
		}
		for (Component* component : object->Components())
		{
			if (component->PersistentId().ToString() != id)
			{
				continue;
			}
			auto* script = dynamic_cast<Scripting::ScriptBehaviourComponent*>(component);
			if (expectedType.empty() || expectedType == "Ursa.Component" ||
			    (script && expectedType == "Ursa.Behaviour"))
			{
				return {};
			}
			String actual =
			    script ? script->TypeName().substr(0, script->TypeName().find(',')) : component->SerializationType();
			String expected =
			    expectedType.substr(expectedType.rfind('.') == String::npos ? 0 : expectedType.rfind('.') + 1);
			if (!script && expected.size() >= 9 && expected.substr(expected.size() - 9) == "Component")
			{
				expected = String("ursa.") + expected.substr(0, expected.size() - 9);
			}
			else
			{
				expected = expectedType;
			}
			if (actual == expected)
			{
				return {};
			}
			return Result<void>::Failure("Component reference does not match its declared type");
		}
	}
	return Result<void>::Failure("Object/component reference does not resolve in this editing graph: " + id);
}

} // namespace

Result<std::unique_ptr<SerializedObject>> SerializedObject::Create(SerializedTargetAdapter adapter)
{
	if (!adapter.capture)
	{
		return Result<std::unique_ptr<SerializedObject>>::Failure("Serialized asset target requires a capture service");
	}
	auto payload = adapter.capture();
	if (!payload || !payload.value.is_object() || !payload.value.contains("Fields") ||
	    !payload.value["Fields"].is_object() || !payload.value.contains("FieldTypes") ||
	    !payload.value["FieldTypes"].is_object())
	{
		return Result<std::unique_ptr<SerializedObject>>::Failure(
		    payload ? "Serialized target requires typed Fields and FieldTypes" : payload.error);
	}
	auto result = std::unique_ptr<SerializedObject>(new SerializedObject(std::move(payload.value)));
	result->captureCurrent = std::move(adapter.capture);
	result->adapterApply = std::move(adapter.apply);
	result->adapterReference = std::move(adapter.validateReference);
	result->adapterPrepare = std::move(adapter.prepare);
	result->adapterValidate = std::move(adapter.validate);
	result->adapterPublish = std::move(adapter.publish);
	result->adapterRestore = std::move(adapter.restore);
	result->adapterRefresh = std::move(adapter.refresh);
	result->resources = adapter.resources;
	return Result<std::unique_ptr<SerializedObject>>::Success(std::move(result));
}

Result<std::unique_ptr<SerializedObject>> SerializedObject::Create(Scripting::ScriptBehaviourComponent& target,
                                                                   ResourceSession* resources)
{
	if (!target.HasSerializedPayload())
	{
		return Result<std::unique_ptr<SerializedObject>>::Failure("Script component has no serialized field payload");
	}
	try
	{
		Json payload = Json::parse(ToStdString(target.SerializedPayload()));
		if (!payload.is_object() || !payload.contains("Fields") || !payload["Fields"].is_object())
		{
			return Result<std::unique_ptr<SerializedObject>>::Failure(
			    "Script payload does not contain a Fields object");
		}
		auto object = std::unique_ptr<SerializedObject>(new SerializedObject(target, std::move(payload)));
		object->resources = resources;
		object->captureCurrent = [&target]() -> Result<Json>
		{
			try
			{
				return Result<Json>::Success(Json::parse(ToStdString(target.SerializedPayload())));
			}
			catch (const std::exception& error)
			{
				return Result<Json>::Failure(ToUrsaString(error.what()));
			}
		};
		return Result<std::unique_ptr<SerializedObject>>::Success(std::move(object));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<SerializedObject>>::Failure("Cannot read script fields: " +
		                                                          ToUrsaString(error.what()));
	}
}

Result<std::unique_ptr<SerializedObject>> SerializedObject::Create(Component& target, const ComponentRegistry& registry,
                                                                   ComponentLoadContext& context,
                                                                   const ComponentSaveContext& saveContext)
{
	if (auto* script = dynamic_cast<Scripting::ScriptBehaviourComponent*>(&target))
	{
		auto created = Create(*script, &context.Resources());
		if (created)
		{
			created.value->referenceContext = &context;
		}
		return created;
	}
	auto codec = registry.FindFor(target);
	if (!codec || !codec.value->createDetached)
	{
		return Result<std::unique_ptr<SerializedObject>>::Failure(
		    "Native component is not exposed for serialized editing");
	}
	auto data = codec.value->save(target, saveContext);
	if (!data)
	{
		return Result<std::unique_ptr<SerializedObject>>::Failure(data.error);
	}
	Json payload = DescribeNativeComponentPayload(target, data.value);
	auto object = std::unique_ptr<SerializedObject>(new SerializedObject(target, std::move(payload)));
	object->resources = &context.Resources();
	object->referenceContext = &context;
	const ComponentCodec copiedCodec = *codec.value;
	object->captureCurrent = [&target, copiedCodec, saveContext]() -> Result<Json>
	{
		auto saved = copiedCodec.save(target, saveContext);
		return saved ? Result<Json>::Success(DescribeNativeComponentPayload(target, saved.value))
		             : Result<Json>::Failure(saved.error);
	};
	object->nativeApply = [&target, &context, copiedCodec, saveContext](const Json& candidate) -> Result<Json>
	{
		auto previous = copiedCodec.save(target, saveContext);
		if (!previous)
		{
			return Result<Json>::Failure(previous.error);
		}
		ComponentLoadContext candidateContext = context;
		auto candidateOwner = GameObject::Create("Serialized property validation");
		auto candidateComponent = copiedCodec.createDetached(candidateOwner, candidate, candidateContext);
		if (!candidateComponent)
		{
			return Result<Json>::Failure(candidateComponent.error);
		}
		auto bound = candidateContext.ResolveReferences();
		if (!bound)
		{
			return Result<Json>::Failure(bound.error);
		}
		auto reader = ArchiveFromJson(candidate);
		if (!reader)
		{
			return Result<Json>::Failure(reader.error);
		}
		ComponentLoadContext applyContext = context;
		auto applied = target.Deserialize(reader.value, applyContext);
		if (applied)
		{
			applied = applyContext.ResolveReferences();
		}
		if (applied)
		{
			auto committed = copiedCodec.save(target, saveContext);
			if (committed)
			{
				return committed;
			}
			applied = Result<void>::Failure(committed.error);
		}
		if (!applied)
		{
			auto rollback = ArchiveFromJson(previous.value);
			if (rollback)
			{
				ComponentLoadContext rollbackContext = context;
				target.Deserialize(rollback.value, rollbackContext);
				rollbackContext.ResolveReferences();
			}
		}
		return Result<Json>::Failure(applied.error);
	};
	return Result<std::unique_ptr<SerializedObject>>::Success(std::move(object));
}

Result<SerializedProperty> SerializedObject::FindProperty(const String& path)
{
	auto value = PropertyValue(path);
	return value ? Result<SerializedProperty>::Success(SerializedProperty(*this, path))
	             : Result<SerializedProperty>::Failure(value.error);
}

Result<void> SerializedObject::Update()
{
	if (!captureCurrent)
	{
		return Result<void>::Failure("The serialized target has no refresh service");
	}
	auto snapshot = captureCurrent();
	if (!snapshot || !snapshot.value.is_object() || !snapshot.value.contains("Fields") ||
	    !snapshot.value["Fields"].is_object())
	{
		return Result<void>::Failure(snapshot ? "The refreshed target has no Fields object" : snapshot.error);
	}
	originalPayload = snapshot.value;
	pendingPayload = std::move(snapshot.value);
	modifiedProperties.clear();
	return {};
}

Result<Json> SerializedObject::DescribeProperty(const String& path) const
{
	const auto value = PropertyValue(path);
	if (!value)
	{
		return Result<Json>::Failure(value.error);
	}
	const String field = FieldName(path);
	if (!pendingPayload.contains("FieldTypes") || !pendingPayload["FieldTypes"].contains(ToStdString(field)))
	{
		return Result<Json>::Success({{"Kind", "Unsupported"}, {"ElementKind", "Unsupported"}});
	}
	Json descriptor = pendingPayload["FieldTypes"].at(ToStdString(field));
	Size separator = path.find('/', String("/Fields/").size());
	while (separator != String::npos)
	{
		const auto kind = descriptor.value("Kind", std::string{});
		if (kind == "Array" || kind == "List")
		{
			descriptor["Kind"] = descriptor.value("ElementKind", std::string("Unsupported"));
			descriptor["TypeName"] = descriptor.value("ElementTypeName", std::string{});
			descriptor["ElementKind"] = "Unsupported";
		}
		else if (kind == "Vector2" || kind == "Vector3" || kind == "Vector4" || kind == "Quaternion" || kind == "Color")
		{
			descriptor = {{"Kind", "FloatingPoint"}, {"ElementKind", "Unsupported"}, {"TypeName", "System.Single"}};
		}
		else
		{
			return Result<Json>::Failure(
			    "Only declared collection elements and math components can be accessed as child properties");
		}
		separator = path.find('/', separator + 1);
	}
	return Result<Json>::Success(std::move(descriptor));
}

Vector<String> SerializedObject::PropertyPaths() const
{
	Vector<String> paths;
	for (const auto& field : pendingPayload.at("Fields").items())
	{
		String escaped;
		for (const char value : field.key())
		{
			escaped += value == '~' ? String("~0") : value == '/' ? String("~1") : String(1, value);
		}
		paths.push_back("/Fields/" + escaped);
	}
	return paths;
}

Result<Json> SerializedObject::PropertyValue(const String& path) const
{
	auto pointer = ParsePropertyPath(path);
	if (!pointer)
	{
		return Result<Json>::Failure(pointer.error);
	}
	try
	{
		return Result<Json>::Success(pendingPayload.at(pointer.value));
	}
	catch (const Json::exception& error)
	{
		return Result<Json>::Failure("Unknown serialized script property " + path + ": " + ToUrsaString(error.what()));
	}
}

Result<void> SerializedObject::SetPropertyValue(const String& path, Json value)
{
	auto pointer = ParsePropertyPath(path);
	if (!pointer)
	{
		return Result<void>::Failure(pointer.error);
	}
	try
	{
		(void)pendingPayload.at(pointer.value);
		Json originalValue;
		bool hasOriginalValue = true;
		try
		{
			originalValue = originalPayload.at(pointer.value);
		}
		catch (const Json::out_of_range&)
		{
			hasOriginalValue = false;
		}
		pendingPayload[pointer.value] = value;
		if (hasOriginalValue && pendingPayload.at(pointer.value) == originalValue)
		{
			modifiedProperties.erase(path);
		}
		else
		{
			modifiedProperties[path] = std::move(value);
		}
		return {};
	}
	catch (const Json::exception& error)
	{
		return Result<void>::Failure("Unknown serialized script property " + path + ": " + ToUrsaString(error.what()));
	}
}

Result<void> SerializedObject::ValidateCandidate(const Json& candidate) const
{
	for (const auto& modified : modifiedProperties)
	{
		auto pointer = ParsePropertyPath(modified.first);
		if (!pointer)
		{
			return Result<void>::Failure(pointer.error);
		}
		const String field = FieldName(modified.first);
		if (!candidate.contains("FieldTypes") || !candidate["FieldTypes"].is_object() ||
		    !candidate["FieldTypes"].contains(ToStdString(field)))
		{
			return Result<void>::Failure("Script payload has no field descriptor for " + field);
		}
		const Json& descriptor = candidate["FieldTypes"][ToStdString(field)];
		if (!descriptor.is_object() || !descriptor.contains("Kind") || !descriptor["Kind"].is_string() ||
		    !descriptor.contains("ElementKind") || !descriptor["ElementKind"].is_string())
		{
			return Result<void>::Failure("Script field descriptor is malformed for " + field);
		}
		const Json& fieldValue = candidate["Fields"].at(ToStdString(field));
		auto valid = ValidateValue(fieldValue, ToUrsaString(descriptor["Kind"].get<std::string>()),
		                           ToUrsaString(descriptor["ElementKind"].get<std::string>()), descriptor);
		if (!valid)
		{
			return Result<void>::Failure("Invalid value for script field " + field + ": " + valid.error);
		}
		auto referenced = adapterReference
		                      ? adapterReference(fieldValue, descriptor)
		                      : ValidateLiveReferences(fieldValue, descriptor, target, resources, referenceContext);
		if (!referenced)
		{
			return Result<void>::Failure("Invalid reference for field " + field + ": " + referenced.error);
		}
	}
	return {};
}

Result<SerializedTargetCheckpoint> SerializedObject::PrepareAtomic() const
{
	if (adapterApply && !(adapterPrepare && adapterValidate && adapterPublish && adapterRestore))
	{
		return Result<SerializedTargetCheckpoint>::Failure(
		    "Serialized target adapter does not support atomic batch editing");
	}
	if (adapterPrepare)
	{
		return adapterPrepare(originalPayload, pendingPayload);
	}
	SerializedTargetCheckpoint checkpoint;
	checkpoint.serializedPayload = originalPayload;
	checkpoint.backendToken = {{"Original", originalPayload}, {"Candidate", pendingPayload}};
	return Result<SerializedTargetCheckpoint>::Success(std::move(checkpoint));
}

Result<void> SerializedObject::ValidatePrepared(const SerializedTargetCheckpoint& prepared) const
{
	return adapterValidate ? adapterValidate(prepared) : Result<void>{};
}

Result<Json> SerializedObject::PublishPrepared(const SerializedTargetCheckpoint& prepared)
{
	if (adapterPublish)
	{
		return adapterPublish(prepared);
	}
	const Json& candidate = prepared.backendToken.at("Candidate");
	if (adapterApply)
	{
		return adapterApply(candidate);
	}
	if (nativeApply)
	{
		auto applied = nativeApply(candidate.at("Fields"));
		return applied ? Result<Json>::Success(DescribeNativeComponentPayload(*target, applied.value)) : applied;
	}
	if (!target)
	{
		return Result<Json>::Failure("This serialized asset is read-only; edit its source or importer");
	}
	auto& script = static_cast<Scripting::ScriptBehaviourComponent&>(*target);
	auto applied = script.ApplySerializedPayload(ToUrsaString(candidate.dump()), resources);
	return applied ? Result<Json>::Success(candidate) : Result<Json>::Failure(applied.error);
}

Result<void> SerializedObject::RestorePrepared(const SerializedTargetCheckpoint& prepared)
{
	if (adapterRestore)
	{
		return adapterRestore(prepared);
	}
	const Json& original = prepared.backendToken.at("Original");
	if (adapterApply)
	{
		auto restored = adapterApply(original);
		return restored ? Result<void>{} : Result<void>::Failure(restored.error);
	}
	if (nativeApply)
	{
		auto restored = nativeApply(original.at("Fields"));
		return restored ? Result<void>{} : Result<void>::Failure(restored.error);
	}
	if (!target)
	{
		return Result<void>::Failure("This serialized asset has no restore service");
	}
	auto& script = static_cast<Scripting::ScriptBehaviourComponent&>(*target);
	return script.ApplySerializedPayload(ToUrsaString(original.dump()), resources);
}

Result<void> SerializedObject::ApplyAtomically(const Vector<SerializedObject*>& targets)
{
	if (targets.empty())
	{
		return {};
	}
	Set<SerializedObject*> uniqueTargets;
	for (SerializedObject* target : targets)
	{
		if (!target || !uniqueTargets.insert(target).second)
		{
			return Result<void>::Failure("Atomic serialized edit contains an invalid or duplicate target");
		}
		if (target->captureCurrent)
		{
			auto current = target->captureCurrent();
			if (!current || current.value != target->originalPayload)
			{
				return Result<void>::Failure(current ? "Serialized target changed since Update; refresh before applying"
				                                     : current.error);
			}
		}
		auto valid = target->ValidateCandidate(target->pendingPayload);
		if (!valid)
		{
			return valid;
		}
	}
	Vector<SerializedObject*> changedTargets;
	for (SerializedObject* target : targets)
	{
		if (target->HasModifiedProperties())
		{
			changedTargets.push_back(target);
		}
	}
	if (changedTargets.empty())
	{
		return {};
	}

	Vector<SerializedTargetCheckpoint> prepared;
	prepared.reserve(changedTargets.size());
	for (SerializedObject* target : changedTargets)
	{
		auto candidate = target->PrepareAtomic();
		if (!candidate)
		{
			return Result<void>::Failure(candidate.error);
		}
		prepared.push_back(std::move(candidate.value));
	}
	for (Size index = 0; index < changedTargets.size(); ++index)
	{
		auto valid = changedTargets[index]->ValidatePrepared(prepared[index]);
		if (!valid)
		{
			return valid;
		}
	}

	Vector<Json> published;
	published.reserve(changedTargets.size());
	for (Size index = 0; index < changedTargets.size(); ++index)
	{
		auto result = changedTargets[index]->PublishPrepared(prepared[index]);
		if (!result)
		{
			String error = result.error;
			for (Size restoreIndex = index + 1; restoreIndex > 0; --restoreIndex)
			{
				auto restored = changedTargets[restoreIndex - 1]->RestorePrepared(prepared[restoreIndex - 1]);
				if (!restored)
				{
					error += "; rollback failed for target " + ToUrsaString(std::to_string(restoreIndex - 1)) + ": " +
					         restored.error;
				}
			}
			return Result<void>::Failure(std::move(error));
		}
		published.push_back(std::move(result.value));
	}

	for (Size index = 0; index < changedTargets.size(); ++index)
	{
		changedTargets[index]->pendingPayload = std::move(published[index]);
		changedTargets[index]->originalPayload = changedTargets[index]->pendingPayload;
		changedTargets[index]->modifiedProperties.clear();
	}
	for (SerializedObject* target : changedTargets)
	{
		if (target->adapterRefresh)
		{
			target->adapterRefresh(target->originalPayload);
		}
	}
	return {};
}

Result<bool> SerializedObject::ApplyFields(const Json& fields)
{
	if (!fields.is_object())
	{
		return Result<bool>::Failure("Serialized target fields must be an object");
	}
	const auto previous = pendingPayload;
	const auto modified = modifiedProperties;
	for (const auto& field : fields.items())
	{
		if (!pendingPayload.at("FieldTypes").contains(field.key()) ||
		    !pendingPayload.at("Fields").contains(field.key()))
		{
			pendingPayload = previous;
			modifiedProperties = modified;
			return Result<bool>::Failure("Serialized target field is not in its current schema: " +
			                             ToUrsaString(field.key()));
		}
		if (pendingPayload["Fields"][field.key()] == field.value())
		{
			continue;
		}
		String escaped;
		for (char value : field.key())
		{
			escaped += value == '~' ? String("~0") : value == '/' ? String("~1") : String(1, value);
		}
		const auto edited = SetPropertyValue("/Fields/" + escaped, field.value());
		if (!edited)
		{
			pendingPayload = previous;
			modifiedProperties = modified;
			return Result<bool>::Failure(edited.error);
		}
	}
	const bool changed = HasModifiedProperties();
	const auto applied = Apply();
	if (!applied)
	{
		pendingPayload = previous;
		modifiedProperties = modified;
		return Result<bool>::Failure(applied.error);
	}
	return Result<bool>::Success(changed);
}

Result<void> SerializedObject::ValidateAll() const
{
	if (captureCurrent)
	{
		const auto current = captureCurrent();
		if (!current || current.value != originalPayload)
		{
			return Result<void>::Failure(current ? "Serialized target changed since Update" : current.error);
		}
	}
	SerializedObject validation = *this;
	validation.modifiedProperties.clear();
	for (const auto& path : PropertyPaths())
	{
		const auto name = FieldName(path);
		if (!pendingPayload.at("FieldTypes").contains(ToStdString(name)))
		{
			continue;
		}
		validation.modifiedProperties[path] = pendingPayload.at("Fields").at(ToStdString(name));
	}
	return validation.ValidateCandidate(pendingPayload);
}

Result<void> SerializedObject::Apply()
{
	if (modifiedProperties.empty())
	{
		return {};
	}
	if (captureCurrent)
	{
		const auto current = captureCurrent();
		if (!current || current.value != originalPayload)
		{
			return Result<void>::Failure(current ? "Serialized target changed since Update; refresh before applying"
			                                     : current.error);
		}
	}
	auto valid = ValidateCandidate(pendingPayload);
	if (!valid)
	{
		return valid;
	}
	if (adapterApply)
	{
		auto applied = adapterApply(pendingPayload);
		if (!applied)
		{
			return Result<void>::Failure(applied.error);
		}
		pendingPayload = std::move(applied.value);
	}
	else if (nativeApply)
	{
		auto applied = nativeApply(pendingPayload["Fields"]);
		if (!applied)
		{
			return Result<void>::Failure(applied.error);
		}
		pendingPayload = DescribeNativeComponentPayload(*target, applied.value);
	}
	else
	{
		if (!target)
		{
			return Result<void>::Failure("This serialized asset is read-only; edit its source or importer");
		}
		auto& script = static_cast<Scripting::ScriptBehaviourComponent&>(*target);
		auto applied = script.ApplySerializedPayload(ToUrsaString(pendingPayload.dump()), resources);
		if (!applied)
		{
			return applied;
		}
	}
	originalPayload = pendingPayload;
	modifiedProperties.clear();
	if (adapterRefresh)
	{
		adapterRefresh(originalPayload);
	}
	return {};
}

Result<SerializedObjectGroup> SerializedObjectGroup::Create(Vector<SerializedObject*> targets, Size primaryIndex)
{
	if (targets.empty() || primaryIndex >= targets.size())
	{
		return Result<SerializedObjectGroup>::Failure(
		    "Serialized object group requires targets and a valid primary index");
	}
	Set<SerializedObject*> uniqueTargets;
	for (SerializedObject* target : targets)
	{
		if (!target || !uniqueTargets.insert(target).second)
		{
			return Result<SerializedObjectGroup>::Failure(
			    "Serialized object group contains an invalid or duplicate target");
		}
	}
	return Result<SerializedObjectGroup>::Success(SerializedObjectGroup(std::move(targets), primaryIndex));
}

Vector<String> SerializedObjectGroup::CommonPropertyPaths() const
{
	Vector<String> paths;
	if (targets.empty())
	{
		return paths;
	}
	for (const String& path : targets[primaryIndex]->PropertyPaths())
	{
		auto primaryDescriptor = targets[primaryIndex]->DescribeProperty(path);
		if (!primaryDescriptor || primaryDescriptor.value.value("Kind", std::string("Unsupported")) == "Unsupported")
		{
			continue;
		}
		bool shared = true;
		for (Size index = 0; index < targets.size(); ++index)
		{
			if (index == primaryIndex)
			{
				continue;
			}
			auto descriptor = targets[index]->DescribeProperty(path);
			if (!descriptor || descriptor.value != primaryDescriptor.value)
			{
				shared = false;
				break;
			}
		}
		if (shared)
		{
			paths.push_back(path);
			const String kind = ToUrsaString(primaryDescriptor.value.value("Kind", std::string{}));
			if (kind == "Array" || kind == "List")
			{
				Size commonLength = std::numeric_limits<Size>::max();
				for (SerializedObject* target : targets)
				{
					auto property = target->FindProperty(path);
					if (!property)
					{
						commonLength = 0;
						break;
					}
					auto contents = property.value.Value();
					commonLength =
					    std::min(commonLength, contents && contents.value.is_array() ? contents.value.size() : 0);
				}
				for (Size elementIndex = 0; elementIndex < commonLength; ++elementIndex)
				{
					const String elementPath = path + "/" + ToUrsaString(std::to_string(elementIndex));
					const auto expected = targets[primaryIndex]->DescribeProperty(elementPath);
					bool commonElement = static_cast<bool>(expected);
					for (SerializedObject* target : targets)
					{
						auto descriptor = target->DescribeProperty(elementPath);
						if (!descriptor || !expected || descriptor.value != expected.value)
						{
							commonElement = false;
							break;
						}
					}
					if (commonElement)
					{
						paths.push_back(elementPath);
					}
				}
			}
			const Size componentCount = kind == "Vector2"                                              ? 2
			                            : kind == "Vector3"                                            ? 3
			                            : kind == "Vector4" || kind == "Quaternion" || kind == "Color" ? 4
			                                                                                           : 0;
			for (Size component = 0; component < componentCount; ++component)
			{
				const String componentPath = path + "/" + ToUrsaString(std::to_string(component));
				bool commonComponent = true;
				for (Size index = 0; index < targets.size(); ++index)
				{
					auto descriptor = targets[index]->DescribeProperty(componentPath);
					auto expected = targets[primaryIndex]->DescribeProperty(componentPath);
					if (!descriptor || !expected || descriptor.value != expected.value ||
					    descriptor.value.value("Kind", std::string("Unsupported")) != "FloatingPoint")
					{
						commonComponent = false;
						break;
					}
				}
				if (commonComponent)
				{
					paths.push_back(componentPath);
				}
			}
		}
	}
	return paths;
}

Vector<bool> SerializedObjectGroup::PendingTargetChanges() const
{
	Vector<bool> changes;
	changes.reserve(targets.size());
	for (const SerializedObject* target : targets)
	{
		changes.push_back(target->HasModifiedProperties());
	}
	return changes;
}

Result<void> SerializedObjectGroup::Update()
{
	for (SerializedObject* target : targets)
	{
		auto updated = target->Update();
		if (!updated)
		{
			return Result<void>::Failure(updated.error);
		}
	}
	return {};
}

Result<Json> SerializedObjectGroup::TargetSnapshot(Size targetIndex) const
{
	if (targetIndex >= targets.size())
	{
		return Result<Json>::Failure("Serialized target index is out of range");
	}
	return Result<Json>::Success(targets[targetIndex]->Snapshot());
}

Result<Json> SerializedObjectGroup::DescribeProperty(const String& path) const
{
	const auto common = CommonPropertyPaths();
	if (std::find(common.begin(), common.end(), path) == common.end())
	{
		return Result<Json>::Failure("Property is not present with matching semantics on every target: " + path);
	}
	auto primaryValue = targets[primaryIndex]->FindProperty(path);
	if (!primaryValue)
	{
		return Result<Json>::Failure(primaryValue.error);
	}
	auto representative = primaryValue.value.Value();
	if (!representative)
	{
		return Result<Json>::Failure(representative.error);
	}
	auto descriptor = targets[primaryIndex]->DescribeProperty(path);
	if (!descriptor)
	{
		return Result<Json>::Failure(descriptor.error);
	}
	const Json comparable = ComparablePropertyValue(representative.value, descriptor.value);
	bool mixed = false;
	String valueState = PropertyValueState(representative.value);
	for (SerializedObject* target : targets)
	{
		auto property = target->FindProperty(path);
		if (!property)
		{
			return Result<Json>::Failure(property.error);
		}
		auto value = property.value.Value();
		if (!value)
		{
			return Result<Json>::Failure(value.error);
		}
		mixed = mixed || ComparablePropertyValue(value.value, descriptor.value) != comparable;
		if (PropertyValueState(value.value) != valueState)
		{
			mixed = true;
		}
	}
	Json result = descriptor.value;
	result["Value"] = representative.value;
	result["ValueState"] = ToStdString(valueState);
	result["HasMultipleDifferentValues"] = mixed;
	result["TargetCount"] = targets.size();
	const String kind = ToUrsaString(descriptor.value.value("Kind", std::string{}));
	if (kind == "Array" || kind == "List")
	{
		Size minimumLength = std::numeric_limits<Size>::max();
		bool hasMixedLength = false;
		Size firstLength = 0;
		bool firstWasNull = false;
		for (Size index = 0; index < targets.size(); ++index)
		{
			auto property = targets[index]->FindProperty(path);
			if (!property)
			{
				return Result<Json>::Failure(property.error);
			}
			auto value = property.value.Value();
			if (!value)
			{
				return Result<Json>::Failure(value.error);
			}
			const bool isNull = value.value.is_null();
			const Size length = value.value.is_array() ? value.value.size() : 0;
			minimumLength = std::min(minimumLength, length);
			if (index == 0)
			{
				firstLength = length;
				firstWasNull = isNull;
			}
			else if (firstLength != length || firstWasNull != isNull)
			{
				hasMixedLength = true;
			}
		}
		result["ArraySize"] = minimumLength;
		result["HasMultipleDifferentArraySizes"] = hasMixedLength;
		if (representative.value.is_array())
		{
			result["PrimaryArraySize"] = representative.value.size();
		}
		else
		{
			result["PrimaryArraySize"] = nullptr;
		}
		result["IsNullCollection"] = representative.value.is_null();
	}
	return Result<Json>::Success(std::move(result));
}

Result<Json> SerializedObjectGroup::TargetValue(Size targetIndex, const String& path) const
{
	if (targetIndex >= targets.size())
	{
		return Result<Json>::Failure("Serialized object target index is out of range");
	}
	auto property = targets[targetIndex]->FindProperty(path);
	if (!property)
	{
		return Result<Json>::Failure(property.error);
	}
	return property.value.Value();
}

Result<Json> SerializedObjectGroup::TargetValues(const String& path) const
{
	const auto common = CommonPropertyPaths();
	if (std::find(common.begin(), common.end(), path) == common.end())
	{
		return Result<Json>::Failure("Cannot capture a property without matching semantics on every target: " + path);
	}
	Json values = Json::array();
	for (Size index = 0; index < targets.size(); ++index)
	{
		auto value = TargetValue(index, path);
		if (!value)
		{
			return Result<Json>::Failure(value.error);
		}
		values.push_back(std::move(value.value));
	}
	return Result<Json>::Success(std::move(values));
}

Result<Vector<bool>> SerializedObjectGroup::CompareTargetValues(const String& path, const Json& checkpoint,
                                                                const Json& candidate) const
{
	if (!checkpoint.is_array() || checkpoint.size() != targets.size())
	{
		return Result<Vector<bool>>::Failure("Serialized target checkpoint does not match the target group");
	}
	const auto commonPaths = CommonPropertyPaths();
	if (std::find(commonPaths.begin(), commonPaths.end(), path) == commonPaths.end())
	{
		return Result<Vector<bool>>::Failure("Property is not present with matching semantics on every target: " +
		                                     path);
	}
	auto descriptor = targets[primaryIndex]->DescribeProperty(path);
	if (!descriptor)
	{
		return Result<Vector<bool>>::Failure(descriptor.error);
	}
	const Json comparableCandidate = ComparablePropertyValue(candidate, descriptor.value);
	const String candidateState = PropertyValueState(candidate);
	Vector<bool> changes;
	changes.reserve(targets.size());
	for (const Json& value : checkpoint)
	{
		changes.push_back(ComparablePropertyValue(value, descriptor.value) != comparableCandidate ||
		                  PropertyValueState(value) != candidateState);
	}
	return Result<Vector<bool>>::Success(std::move(changes));
}

Result<void> SerializedObjectGroup::SetValue(const String& path, Json value)
{
	const auto common = CommonPropertyPaths();
	if (std::find(common.begin(), common.end(), path) == common.end())
	{
		return Result<void>::Failure("Cannot edit a property without matching semantics on every target: " + path);
	}
	Vector<Json> previousPayloads;
	Vector<Map<String, Json>> previousModifications;
	previousPayloads.reserve(targets.size());
	previousModifications.reserve(targets.size());
	for (SerializedObject* target : targets)
	{
		previousPayloads.push_back(target->pendingPayload);
		previousModifications.push_back(target->modifiedProperties);
	}
	for (SerializedObject* target : targets)
	{
		auto edited = target->SetPropertyValue(path, value);
		if (!edited)
		{
			for (Size index = 0; index < targets.size(); ++index)
			{
				targets[index]->pendingPayload = std::move(previousPayloads[index]);
				targets[index]->modifiedProperties = std::move(previousModifications[index]);
			}
			return edited;
		}
	}
	return {};
}

Result<void> SerializedObjectGroup::SetTargetValues(const String& path, const Json& values)
{
	const auto common = CommonPropertyPaths();
	if (std::find(common.begin(), common.end(), path) == common.end() || !values.is_array() ||
	    values.size() != targets.size())
	{
		return Result<void>::Failure("Serialized per-target values do not match the common property group: " + path);
	}
	Vector<Json> previousPayloads;
	Vector<Map<String, Json>> previousModifications;
	for (const SerializedObject* target : targets)
	{
		previousPayloads.push_back(target->pendingPayload);
		previousModifications.push_back(target->modifiedProperties);
	}
	for (Size index = 0; index < targets.size(); ++index)
	{
		auto edited = targets[index]->SetPropertyValue(path, values[index]);
		if (!edited)
		{
			for (Size restore = 0; restore < targets.size(); ++restore)
			{
				targets[restore]->pendingPayload = std::move(previousPayloads[restore]);
				targets[restore]->modifiedProperties = std::move(previousModifications[restore]);
			}
			return edited;
		}
	}
	return {};
}

Result<void> SerializedObjectGroup::RestoreTargetValues(const String& path, const Json& values)
{
	const auto common = CommonPropertyPaths();
	if (std::find(common.begin(), common.end(), path) == common.end())
	{
		return Result<void>::Failure("Cannot restore a property without matching semantics on every target: " + path);
	}
	if (!values.is_array() || values.size() != targets.size())
	{
		return Result<void>::Failure("Serialized property checkpoint does not match the target group");
	}
	Vector<Json> previousPayloads;
	Vector<Map<String, Json>> previousModifications;
	previousPayloads.reserve(targets.size());
	previousModifications.reserve(targets.size());
	for (SerializedObject* target : targets)
	{
		previousPayloads.push_back(target->pendingPayload);
		previousModifications.push_back(target->modifiedProperties);
	}
	const auto restorePending = [&]
	{
		for (Size index = 0; index < targets.size(); ++index)
		{
			targets[index]->pendingPayload = previousPayloads[index];
			targets[index]->modifiedProperties = previousModifications[index];
		}
	};
	for (Size index = 0; index < targets.size(); ++index)
	{
		auto restored = targets[index]->SetPropertyValue(path, values[index]);
		if (!restored)
		{
			restorePending();
			return restored;
		}
	}
	auto applied = Apply();
	if (!applied)
	{
		restorePending();
		return applied;
	}
	return {};
}

Result<bool> SerializedObjectGroup::SetTargetFields(Size targetIndex, const Json& fields)
{
	if (targetIndex >= targets.size())
	{
		return Result<bool>::Failure("Serialized target index is out of range");
	}
	return targets[targetIndex]->ApplyFields(fields);
}

Result<void> SerializedObjectGroup::ResizeCollection(const String& path, Size newSize)
{
	auto propertyDescription = DescribeProperty(path);
	if (!propertyDescription)
	{
		return Result<void>::Failure(propertyDescription.error);
	}
	const String kind = ToUrsaString(propertyDescription.value.value("Kind", std::string{}));
	if (kind != "Array" && kind != "List")
	{
		return Result<void>::Failure("Property is not a resizable collection: " + path);
	}
	Json defaultValue;
	if (newSize > propertyDescription.value.value("ArraySize", Size(0)))
	{
		auto elementDefault = DefaultCollectionElement(propertyDescription.value);
		if (!elementDefault)
		{
			return Result<void>::Failure(elementDefault.error);
		}
		defaultValue = std::move(elementDefault.value);
	}
	Vector<Json> previousPayloads;
	Vector<Map<String, Json>> previousModifications;
	for (SerializedObject* target : targets)
	{
		previousPayloads.push_back(target->pendingPayload);
		previousModifications.push_back(target->modifiedProperties);
	}
	const auto rollbackPending = [&]
	{
		for (Size index = 0; index < targets.size(); ++index)
		{
			targets[index]->pendingPayload = std::move(previousPayloads[index]);
			targets[index]->modifiedProperties = std::move(previousModifications[index]);
		}
	};
	for (SerializedObject* target : targets)
	{
		auto property = target->FindProperty(path);
		if (!property)
		{
			rollbackPending();
			return Result<void>::Failure(property.error);
		}
		auto current = property.value.Value();
		if (!current)
		{
			rollbackPending();
			return Result<void>::Failure(current.error);
		}
		if (!current.value.is_array() && !current.value.is_null())
		{
			rollbackPending();
			return Result<void>::Failure("Cannot resize a malformed non-array collection: " + path);
		}
		Json values = current.value.is_array() ? current.value : Json::array();
		while (values.size() > newSize)
		{
			values.erase(values.end() - 1);
		}
		while (values.size() < newSize)
		{
			values.push_back(defaultValue);
		}
		auto edited = target->SetPropertyValue(path, std::move(values));
		if (!edited)
		{
			rollbackPending();
			return edited;
		}
	}
	return {};
}

Result<void> SerializedObjectGroup::MoveCollectionElement(const String& path, Size fromIndex, Size toIndex)
{
	auto propertyDescription = DescribeProperty(path);
	if (!propertyDescription)
	{
		return Result<void>::Failure(propertyDescription.error);
	}
	const String kind = ToUrsaString(propertyDescription.value.value("Kind", std::string{}));
	const Size commonLength = propertyDescription.value.value("ArraySize", Size(0));
	if ((kind != "Array" && kind != "List") || fromIndex >= commonLength || toIndex >= commonLength)
	{
		return Result<void>::Failure("Collection reorder range is not common to every target: " + path);
	}
	if (fromIndex == toIndex)
	{
		return {};
	}
	Vector<Json> previousPayloads;
	Vector<Map<String, Json>> previousModifications;
	for (SerializedObject* target : targets)
	{
		previousPayloads.push_back(target->pendingPayload);
		previousModifications.push_back(target->modifiedProperties);
	}
	const auto rollbackPending = [&]
	{
		for (Size index = 0; index < targets.size(); ++index)
		{
			targets[index]->pendingPayload = std::move(previousPayloads[index]);
			targets[index]->modifiedProperties = std::move(previousModifications[index]);
		}
	};
	for (SerializedObject* target : targets)
	{
		auto property = target->FindProperty(path);
		auto current = property ? property.value.Value() : Result<Json>::Failure(property.error);
		if (!current || !current.value.is_array())
		{
			rollbackPending();
			return Result<void>::Failure(current ? "Collection is not initialized" : current.error);
		}
		Json values = current.value;
		Json moved = std::move(values.at(fromIndex));
		if (fromIndex < toIndex)
		{
			for (Size index = fromIndex; index < toIndex; ++index)
			{
				values[index] = std::move(values[index + 1]);
			}
		}
		else
		{
			for (Size index = fromIndex; index > toIndex; --index)
			{
				values[index] = std::move(values[index - 1]);
			}
		}
		values[toIndex] = std::move(moved);
		auto edited = target->SetPropertyValue(path, std::move(values));
		if (!edited)
		{
			rollbackPending();
			return edited;
		}
	}
	return {};
}

Result<void> SerializedObjectGroup::AppendReferences(const String& path, const Vector<Json>& references)
{
	if (references.empty())
	{
		return {};
	}
	auto propertyDescription = DescribeProperty(path);
	if (!propertyDescription)
	{
		return Result<void>::Failure(propertyDescription.error);
	}
	const String kind = ToUrsaString(propertyDescription.value.value("Kind", std::string{}));
	const String elementKind = ToUrsaString(propertyDescription.value.value("ElementKind", std::string("Unsupported")));
	if ((kind != "Array" && kind != "List") ||
	    (elementKind != "AssetReference" && elementKind != "GameObjectReference" &&
	     elementKind != "ComponentReference" && elementKind != "MaterialReference" &&
	     elementKind != "TextureReference" && elementKind != "MeshReference" &&
	     elementKind != "ScriptableObjectReference" && elementKind != "ShaderReference" &&
	     elementKind != "ComputeShaderReference"))
	{
		return Result<void>::Failure("Collection does not contain appendable object references: " + path);
	}
	Vector<Json> previousPayloads;
	Vector<Map<String, Json>> previousModifications;
	for (SerializedObject* target : targets)
	{
		previousPayloads.push_back(target->pendingPayload);
		previousModifications.push_back(target->modifiedProperties);
	}
	const auto rollbackPending = [&]
	{
		for (Size index = 0; index < targets.size(); ++index)
		{
			targets[index]->pendingPayload = std::move(previousPayloads[index]);
			targets[index]->modifiedProperties = std::move(previousModifications[index]);
		}
	};
	for (SerializedObject* target : targets)
	{
		auto property = target->FindProperty(path);
		auto current = property ? property.value.Value() : Result<Json>::Failure(property.error);
		if (!current)
		{
			rollbackPending();
			return Result<void>::Failure(current.error);
		}
		if (!current.value.is_array() && !current.value.is_null())
		{
			rollbackPending();
			return Result<void>::Failure("Cannot append to a malformed non-array collection: " + path);
		}
		Json values = current.value.is_array() ? current.value : Json::array();
		for (const Json& reference : references)
		{
			values.push_back(reference);
		}
		auto edited = target->SetPropertyValue(path, std::move(values));
		if (!edited)
		{
			rollbackPending();
			return edited;
		}
		auto validated = target->ValidateAll();
		if (!validated)
		{
			rollbackPending();
			return Result<void>::Failure("Reference append rejected for one target: " + validated.error);
		}
	}
	return {};
}

Result<void> SerializedObjectGroup::Apply()
{
	return SerializedObject::ApplyAtomically(targets);
}

Result<Json> SerializedProperty::Value() const
{
	return owner ? owner->PropertyValue(path) : Result<Json>::Failure("SerializedProperty has no owner");
}

Result<void> SerializedProperty::SetValue(Json value)
{
	return owner ? owner->SetPropertyValue(path, std::move(value))
	             : Result<void>::Failure("SerializedProperty has no owner");
}
} // namespace Ursa::Editor
