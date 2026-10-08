#include <Serialization/SerializedObject.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <cmath>
#include <limits>
#include <string>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

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
	if (type == "Vector3" || type == "Quaternion" || type == "Color")
	{
		const Size expected = type == "Vector3" ? 3 : 4;
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
	if (type == "GameObjectReference")
	{
		return ValidateReference(value, "GameObject");
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

Result<void> ValidateLiveReferences(const Json& value, const Json& descriptor, const Component& target,
                                    ResourceSession* resources)
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
			auto valid = ValidateLiveReferences(item, element, target, resources);
			if (!valid)
			{
				return valid;
			}
		}
		return {};
	}
	if (kind == "AssetReference" && resources)
	{
		auto id = ResourceId::Parse(ToUrsaString(value.at("guid").get<std::string>()));
		auto resolved = resources->ResolveAsset(id.value, value.at("localFileId").get<UInt64>());
		return resolved ? Result<void>{} : Result<void>::Failure(resolved.error);
	}
	if (kind != "GameObjectReference" && kind != "ComponentReference")
	{
		return {};
	}
	auto owner = target.GetGameObject();
	if (!owner)
	{
		return {};
	}
	Vector<GameObject::Ptr> pending;
	if (target.TryGetScene())
	{
		pending = target.TryGetScene()->RootGameObjects();
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
		return Create(*script, &context.Resources());
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
	Json payload{{"Fields", data.value}, {"FieldTypes", Json::object()}};
	for (const auto& field : data.value.items())
	{
		const Json& value = field.value();
		const String kind = value.is_boolean()                                                 ? "Boolean"
		                    : value.is_string()                                                ? "String"
		                    : value.is_number()                                                ? "FloatingPoint"
		                    : value.is_array()                                                 ? "Array"
		                    : value.is_null() || (value.is_object() && value.contains("guid")) ? "AssetReference"
		                                                                                       : "Unsupported";
		payload["FieldTypes"][field.key()] = {{"Kind", ToStdString(kind)},
		                                      {"ElementKind", value.is_array() ? "FloatingPoint" : "Unsupported"}};
	}
	auto object = std::unique_ptr<SerializedObject>(new SerializedObject(target, std::move(payload)));
	const ComponentCodec copiedCodec = *codec.value;
	object->nativeApply = [&target, &context, copiedCodec, saveContext](const Json& candidate) -> Result<void>
	{
		auto previous = copiedCodec.save(target, saveContext);
		if (!previous)
		{
			return Result<void>::Failure(previous.error);
		}
		ComponentLoadContext candidateContext = context;
		auto candidateOwner = GameObject::Create("Serialized property validation");
		auto candidateComponent = copiedCodec.createDetached(candidateOwner, candidate, candidateContext);
		if (!candidateComponent)
		{
			return Result<void>::Failure(candidateComponent.error);
		}
		auto bound = candidateContext.ResolveReferences();
		if (!bound)
		{
			return bound;
		}
		auto reader = ArchiveFromJson(candidate);
		if (!reader)
		{
			return Result<void>::Failure(reader.error);
		}
		ComponentLoadContext applyContext = context;
		auto applied = target.Deserialize(reader.value, applyContext);
		if (applied)
		{
			applied = applyContext.ResolveReferences();
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
		return applied;
	};
	return Result<std::unique_ptr<SerializedObject>>::Success(std::move(object));
}

Result<SerializedProperty> SerializedObject::FindProperty(const String& path)
{
	auto value = PropertyValue(path);
	return value ? Result<SerializedProperty>::Success(SerializedProperty(*this, path))
	             : Result<SerializedProperty>::Failure(value.error);
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
		pendingPayload[pointer.value] = value;
		modifiedProperties[path] = std::move(value);
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
		auto referenced = ValidateLiveReferences(fieldValue, descriptor, target, resources);
		if (!referenced)
		{
			return Result<void>::Failure("Invalid reference for field " + field + ": " + referenced.error);
		}
	}
	return {};
}

Result<void> SerializedObject::Apply()
{
	if (modifiedProperties.empty())
	{
		return {};
	}
	auto valid = ValidateCandidate(pendingPayload);
	if (!valid)
	{
		return valid;
	}
	if (nativeApply)
	{
		auto applied = nativeApply(pendingPayload["Fields"]);
		if (!applied)
		{
			return applied;
		}
	}
	else
	{
		auto& script = static_cast<Scripting::ScriptBehaviourComponent&>(target);
		const String previous = script.SerializedPayload();
		const String candidate = ToUrsaString(pendingPayload.dump());
		script.SetSerializedPayload(candidate);
		if (script.InstanceToken() != 0)
		{
			auto restored = script.RestoreSerializedPayload();
			if (!restored)
			{
				script.SetSerializedPayload(previous);
				script.RestoreSerializedPayload();
				return Result<void>::Failure(restored.error);
			}
		}
	}
	originalPayload = pendingPayload;
	modifiedProperties.clear();
	return {};
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
