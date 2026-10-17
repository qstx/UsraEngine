#include <Scripting/Bindings/EditorServices.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <imgui.h>
#include <limits>

namespace Ursa::Editor::Bindings
{
namespace
{
using Json = nlohmann::json;
using P = B::Wire::Enum_Ursa_Editor_SerializedPropertyType;
using K = B::Wire::Enum_Ursa_Editor_EditorReferenceKind;

struct PropertyIdScope
{
	~PropertyIdScope()
	{
		ImGui::PopID();
	}
};

bool UnsignedEnum(const Json& descriptor)
{
	const auto type = descriptor.value("EnumUnderlyingType", std::string{});
	return type == "System.Byte" || type == "System.UInt16" || type == "System.UInt32" || type == "System.UInt64" ||
	       type == "byte" || type == "ushort" || type == "uint" || type == "ulong";
}

const char* ReferenceName(UInt32 kind)
{
	switch (kind)
	{
		case K::V_GameObject:
			return "GameObject";
		case K::V_Component:
			return "Component";
		case K::V_Mesh:
			return "Mesh";
		case K::V_Material:
			return "Material";
		case K::V_Texture:
			return "Texture";
		case K::V_ScriptableObject:
			return "ScriptableObject";
		case K::V_Shader:
			return "Shader";
		case K::V_ComputeShader:
			return "ComputeShader";
		default:
			throw std::runtime_error("Unsupported serialized reference kind");
	}
}

Json DefaultValue(const Json& descriptor)
{
	switch (EditorServices::PropertyType(descriptor))
	{
		case P::V_Boolean:
			return false;
		case P::V_Integer:
			return Int64{0};
		case P::V_UnsignedInteger:
			return UInt64{0};
		case P::V_Float:
			return 0.0;
		case P::V_String:
			return nullptr;
		case P::V_Enum:
			return UnsignedEnum(descriptor) ? Json(UInt64{0}) : Json(Int64{0});
		case P::V_Vector2:
			return {0.0, 0.0};
		case P::V_Vector3:
			return {0.0, 0.0, 0.0};
		case P::V_Vector4:
		case P::V_Quaternion:
		case P::V_Color:
			return {0.0, 0.0, 0.0, 0.0};
		case P::V_ObjectReference:
		case P::V_AssetReference:
			return nullptr;
		default:
			throw std::runtime_error("Unsupported collection element type");
	}
}
} // namespace

nlohmann::json EditorServices::Descriptor(EditorInvocation& scope, UInt64 revision, const String& path)
{
	scope.RequireProperties(revision);
	return B::TakeBindingResult(scope.properties->DescribeProperty(path));
}

UInt32 EditorServices::PropertyType(const Json& descriptor)
{
	const auto kind = descriptor.value("Kind", std::string{});
	if (kind == "Boolean")
	{
		return P::V_Boolean;
	}
	if (kind == "SignedInteger")
	{
		return P::V_Integer;
	}
	if (kind == "UnsignedInteger")
	{
		return P::V_UnsignedInteger;
	}
	if (kind == "FloatingPoint")
	{
		return P::V_Float;
	}
	if (kind == "String")
	{
		return P::V_String;
	}
	if (kind == "Enumeration")
	{
		return P::V_Enum;
	}
	if (kind == "Vector2")
	{
		return P::V_Vector2;
	}
	if (kind == "Vector3")
	{
		return P::V_Vector3;
	}
	if (kind == "Vector4")
	{
		return P::V_Vector4;
	}
	if (kind == "Quaternion")
	{
		return P::V_Quaternion;
	}
	if (kind == "Color")
	{
		return P::V_Color;
	}
	if (kind == "Array")
	{
		return P::V_Array;
	}
	if (kind == "List")
	{
		return P::V_List;
	}
	if (kind == "GameObjectReference" || kind == "ComponentReference")
	{
		return P::V_ObjectReference;
	}
	if (ReferenceKind(descriptor) != K::V_None)
	{
		return P::V_AssetReference;
	}
	return P::V_Unsupported;
}

UInt32 EditorServices::ReferenceKind(const Json& descriptor)
{
	const auto kind = descriptor.value("Kind", std::string{});
	if (kind == "GameObjectReference")
	{
		return K::V_GameObject;
	}
	if (kind == "ComponentReference")
	{
		return K::V_Component;
	}
	if (kind == "AssetReference")
	{
		return K::V_Asset;
	}
	if (kind == "MeshReference")
	{
		return K::V_Mesh;
	}
	if (kind == "MaterialReference")
	{
		return K::V_Material;
	}
	if (kind == "TextureReference")
	{
		return K::V_Texture;
	}
	if (kind == "ScriptableObjectReference")
	{
		return K::V_ScriptableObject;
	}
	if (kind == "ShaderReference")
	{
		return K::V_Shader;
	}
	if (kind == "ComputeShaderReference")
	{
		return K::V_ComputeShader;
	}
	return K::V_None;
}

EditorServices::PropertyValue EditorServices::ReadValue(B::BindingCall& call, const Json& value, const Json& descriptor)
{
	PropertyValue result;
	result.F_Type = PropertyType(descriptor);
	result.F_IsNull = value.is_null() ? 1u : 0u;
	result.F_EnumUnsigned = UnsignedEnum(descriptor) ? 1u : 0u;
	if (value.is_null() || result.F_Type == P::V_Unsupported)
	{
		return result;
	}
	switch (result.F_Type)
	{
		case P::V_Boolean:
			result.F_Boolean = value.get<bool>() ? 1u : 0u;
			break;
		case P::V_Integer:
			result.F_Integer = value.get<Int64>();
			break;
		case P::V_UnsignedInteger:
			result.F_UnsignedInteger = value.get<UInt64>();
			break;
		case P::V_Float:
			result.F_Float = value.get<Float64>();
			break;
		case P::V_String:
			result.F_String = call.arena.StringValue(ToUrsaString(value.get<std::string>()));
			break;
		case P::V_Enum:
			if (result.F_EnumUnsigned)
			{
				result.F_UnsignedInteger = value.get<UInt64>();
			}
			else
			{
				result.F_Integer = value.get<Int64>();
			}
			break;
		case P::V_Vector2:
		case P::V_Vector3:
		case P::V_Vector4:
		case P::V_Quaternion:
		case P::V_Color:
			result.F_Vector.F_X = value.at(0).get<Float32>();
			result.F_Vector.F_Y = value.at(1).get<Float32>();
			if (result.F_Type != P::V_Vector2)
			{
				result.F_Vector.F_Z = value.at(2).get<Float32>();
			}
			if (result.F_Type != P::V_Vector2 && result.F_Type != P::V_Vector3)
			{
				result.F_Vector.F_W = value.at(3).get<Float32>();
			}
			break;
		case P::V_Array:
		case P::V_List:
			if (value.size() > static_cast<Size>(std::numeric_limits<Int32>::max()))
			{
				throw std::runtime_error("Serialized collection exceeds the supported length");
			}
			result.F_ArraySize = static_cast<Int32>(value.size());
			break;
		case P::V_ObjectReference:
		case P::V_AssetReference:
		{
			EditorReferenceData reference;
			reference.kind = ReferenceKind(descriptor);
			reference.typeId = ToUrsaString(descriptor.value("TypeName", std::string{}));
			if (reference.kind == K::V_Asset)
			{
				reference.hasAsset = true;
				reference.asset = B::TakeBindingResult(AssetReferenceFromJson(value, "SerializedProperty"));
			}
			else
			{
				const auto& data = value.at("$ursaReference");
				if (data.at("Kind").get<std::string>() != ReferenceName(reference.kind))
				{
					throw std::runtime_error("Saved reference kind differs from its property descriptor");
				}
				if (reference.kind <= K::V_Component)
				{
					reference.id = ToUrsaString(data.at("Id").get<std::string>());
				}
				else
				{
					const auto& resource = data.at("Resource");
					if ((reference.kind == K::V_Mesh || reference.kind == K::V_Material) &&
					    resource.value("kind", std::string{}) == "local")
					{
						reference.id = ToUrsaString(resource.at("id").get<std::string>());
					}
					else
					{
						reference.hasAsset = true;
						reference.asset = B::TakeBindingResult(AssetReferenceFromJson(
						    reference.kind == K::V_Mesh || reference.kind == K::V_Material ? resource.at("asset")
						                                                                   : resource,
						    "SerializedProperty"));
					}
				}
			}
			result.F_Reference = {1, WriteReference(call, reference)};
			break;
		}
		default:
			break;
	}
	return result;
}

nlohmann::json EditorServices::WriteValue(EditorInvocation& scope, const PropertyValue& value, const Json& descriptor,
                                          bool validateReference)
{
	const UInt32 type = PropertyType(descriptor);
	if (value.F_Type != type || type == P::V_Unsupported)
	{
		throw std::runtime_error("SerializedProperty accessor does not match its declared type");
	}
	if (value.F_IsNull)
	{
		if (type != P::V_String && type != P::V_ObjectReference && type != P::V_AssetReference && type != P::V_Array &&
		    type != P::V_List)
		{
			throw std::runtime_error("This serialized value cannot be null");
		}
		return nullptr;
	}
	switch (type)
	{
		case P::V_Boolean:
			return value.F_Boolean != 0;
		case P::V_Integer:
			return value.F_Integer;
		case P::V_UnsignedInteger:
			return value.F_UnsignedInteger;
		case P::V_Float:
			return value.F_Float;
		case P::V_String:
			if (!value.F_String.present)
			{
				throw std::runtime_error("StringValue is null without the null tag");
			}
			return ToStdString(B::ReadString(value.F_String));
		case P::V_Enum:
			if ((value.F_EnumUnsigned != 0) != UnsignedEnum(descriptor))
			{
				throw std::runtime_error("Enum signedness does not match its underlying type");
			}
			return value.F_EnumUnsigned ? Json(value.F_UnsignedInteger) : Json(value.F_Integer);
		case P::V_Vector2:
			return {value.F_Vector.F_X, value.F_Vector.F_Y};
		case P::V_Vector3:
			return {value.F_Vector.F_X, value.F_Vector.F_Y, value.F_Vector.F_Z};
		case P::V_Vector4:
		case P::V_Quaternion:
		case P::V_Color:
			return {value.F_Vector.F_X, value.F_Vector.F_Y, value.F_Vector.F_Z, value.F_Vector.F_W};
		case P::V_ObjectReference:
		case P::V_AssetReference:
		{
			if (!value.F_Reference.present)
			{
				throw std::runtime_error("ReferenceValue is null without the null tag");
			}
			auto reference = ReadReference(scope, value.F_Reference.F_value);
			const UInt32 expected = ReferenceKind(descriptor);
			if (reference.kind != expected &&
			    !(expected == K::V_Asset && reference.kind >= K::V_Asset && reference.hasAsset))
			{
				throw std::runtime_error("Reference does not match the serialized property kind");
			}
			if (validateReference)
			{
				reference = Resolve(scope, expected, reference.id, reference.asset);
			}
			if (expected == K::V_Asset)
			{
				return AssetReferenceToJson(reference.asset);
			}
			Json data{{"Kind", ReferenceName(expected)}, {"Id", ""}};
			if (expected <= K::V_Component)
			{
				data["Id"] = ToStdString(reference.id);
			}
			else if (expected == K::V_Mesh || expected == K::V_Material)
			{
				data["Resource"] = reference.hasAsset
				                       ? Json{{"kind", "asset"}, {"asset", AssetReferenceToJson(reference.asset)}}
				                       : Json{{"kind", "local"}, {"id", ToStdString(reference.id)}};
			}
			else
			{
				data["Resource"] = AssetReferenceToJson(reference.asset);
			}
			return Json{{"$ursaReference", std::move(data)}};
		}
		default:
			throw std::runtime_error("Use ArraySize and GetArrayElementAtIndex to edit collections");
	}
}

Result<EditorServices::PropertyState> EditorServices::GetProperty(B::BindingCall& call, UInt64 revision,
                                                                  const B::Utf8View& path)
{
	auto& scope = Scope(call);
	const String fieldPath = B::ReadString(path);
	const Json descriptor = Descriptor(scope, revision, fieldPath);
	const auto property = B::TakeBindingResult(scope.properties->FindProperty(fieldPath));
	PropertyState result;
	result.F_Type = PropertyType(descriptor);
	Json element = descriptor;
	element["Kind"] = descriptor.value("ElementKind", std::string{});
	result.F_ElementType = PropertyType(element);
	result.F_ReferenceKind = ReferenceKind(descriptor);
	result.F_TypeName = call.arena.StringValue(ToUrsaString(descriptor.value("TypeName", std::string{})));
	Vector<B::Utf8View> names;
	Vector<Int64> signedValues;
	Vector<UInt64> unsignedValues;
	if (result.F_Type == P::V_Enum && descriptor.contains("EnumValues") && descriptor.contains("EnumNumericValues"))
	{
		const auto& labels = descriptor.at("EnumValues");
		const auto& values = descriptor.at("EnumNumericValues");
		if (labels.size() != values.size())
		{
			throw std::runtime_error("Enum descriptor names and values have different lengths");
		}
		for (Size index = 0; index < labels.size(); ++index)
		{
			names.push_back(call.arena.StringValue(ToUrsaString(labels[index].get<std::string>())));
			if (UnsignedEnum(descriptor))
			{
				unsignedValues.push_back(values[index].get<UInt64>());
			}
			else
			{
				signedValues.push_back(values[index].get<Int64>());
			}
		}
	}
	result.F_EnumNames = call.arena.Array(names.data(), names.size());
	result.F_EnumSignedValues = call.arena.Array(signedValues.data(), signedValues.size());
	result.F_EnumUnsignedValues = call.arena.Array(unsignedValues.data(), unsignedValues.size());
	result.F_Value = ReadValue(call, B::TakeBindingResult(property.Value()), descriptor);
	return Result<PropertyState>::Success(result);
}

Result<void> EditorServices::SetProperty(B::BindingCall& call, UInt64 revision, const B::Utf8View& path,
                                         const PropertyValue& value)
{
	auto& scope = Scope(call);
	const String fieldPath = B::ReadString(path);
	const auto descriptor = Descriptor(scope, revision, fieldPath);
	auto property = B::TakeBindingResult(scope.properties->FindProperty(fieldPath));
	return property.SetValue(WriteValue(scope, value, descriptor));
}

Result<void> EditorServices::ResizeArray(B::BindingCall& call, UInt64 revision, const B::Utf8View& path, Int32 size)
{
	auto& scope = Scope(call);
	const String fieldPath = B::ReadString(path);
	const auto descriptor = Descriptor(scope, revision, fieldPath);
	const auto type = PropertyType(descriptor);
	if ((type != P::V_Array && type != P::V_List) || size < 0 ||
	    static_cast<UInt64>(size) > scope.TransferBudget() / sizeof(PropertyValue))
	{
		throw std::runtime_error("Invalid serialized collection type or size");
	}
	auto property = B::TakeBindingResult(scope.properties->FindProperty(fieldPath));
	Json value = B::TakeBindingResult(property.Value());
	if (value.is_null())
	{
		value = Json::array();
	}
	Json element = descriptor;
	element["Kind"] = descriptor.value("ElementKind", std::string{});
	while (value.size() < static_cast<Size>(size))
	{
		value.push_back(DefaultValue(element));
	}
	while (value.size() > static_cast<Size>(size))
	{
		value.erase(value.end() - 1);
	}
	return property.SetValue(std::move(value));
}

Result<UInt64> EditorServices::UpdateProperties(B::BindingCall& call)
{
	auto& scope = Scope(call);
	scope.RequireProperties(scope.revision);
	if (scope.revision == std::numeric_limits<UInt64>::max())
	{
		throw std::runtime_error("SerializedObject revision capacity exhausted");
	}
	B::CompleteBindingResult(scope.properties->Update());
	return Result<UInt64>::Success(++scope.revision);
}

Result<bool> EditorServices::ApplyProperties(B::BindingCall& call, UInt64 revision)
{
	auto& scope = Scope(call);
	scope.RequireProperties(revision);
	const bool modified = scope.properties->HasModifiedProperties();
	B::CompleteBindingResult(scope.properties->Apply());
	if (modified)
	{
		scope.changed = true;
		if (scope.source.document)
		{
			scope.source.document->MarkDirty();
		}
	}
	return Result<bool>::Success(modified);
}

Result<bool> EditorServices::HasModifiedProperties(B::BindingCall& call, UInt64 revision)
{
	auto& scope = Scope(call);
	scope.RequireProperties(revision);
	return Result<bool>::Success(scope.properties->HasModifiedProperties());
}

Result<B::DataView> EditorServices::GetPropertyPaths(B::BindingCall& call, UInt64 revision)
{
	auto& scope = Scope(call);
	scope.RequireProperties(revision);
	return Result<B::DataView>::Success(B::WriteArray<B::Utf8View>(call.arena, scope.properties->PropertyPaths(),
	                                                               [&](const String& value)
	                                                               { return call.arena.StringValue(value); }));
}

void EditorServices::DrawProperty(B::BindingCall& call, const String& path, const String& label)
{
	auto& scope = Scope(call);
	const auto fieldPath = call.arena.StringValue(path);
	const auto caption = call.arena.StringValue(label);
	auto state = B::TakeBindingResult(GetProperty(call, scope.revision, fieldPath));
	auto value = state.F_Value;
	ImGui::PushID(path.c_str());
	PropertyIdScope idScope;
	switch (state.F_Type)
	{
		case P::V_Boolean:
			value.F_Boolean = B::TakeBindingResult(Toggle(call, caption, value.F_Boolean != 0)) ? 1u : 0u;
			break;
		case P::V_Integer:
			value.F_Integer = B::TakeBindingResult(IntField(call, caption, value.F_Integer));
			break;
		case P::V_UnsignedInteger:
			ImGui::InputScalar(label.c_str(), ImGuiDataType_U64, &value.F_UnsignedInteger);
			break;
		case P::V_Float:
			value.F_Float = B::TakeBindingResult(FloatField(call, caption, value.F_Float));
			break;
		case P::V_String:
		{
			const auto text = value.F_IsNull ? call.arena.StringValue("") : value.F_String;
			const auto edited = B::TakeBindingResult(TextField(call, caption, text));
			if (B::ReadString(text) != B::ReadString(edited))
			{
				value.F_IsNull = 0;
				value.F_String = edited;
			}
			break;
		}
		case P::V_Enum:
		{
			const auto names = B::ReadArray<B::Utf8View, String>(state.F_EnumNames,
			                                                     [](const auto& name) { return B::ReadString(name); });
			const auto* signedValues = static_cast<const Int64*>(state.F_EnumSignedValues.data);
			const auto* unsignedValues = static_cast<const UInt64*>(state.F_EnumUnsignedValues.data);
			Int32 selected = -1;
			for (Size index = 0; index < names.size(); ++index)
			{
				if (value.F_EnumUnsigned ? unsignedValues[index] == value.F_UnsignedInteger
				                         : signedValues[index] == value.F_Integer)
				{
					selected = static_cast<Int32>(index);
				}
			}
			selected = B::TakeBindingResult(Popup(call, caption, selected, state.F_EnumNames));
			if (selected >= 0)
			{
				if (value.F_EnumUnsigned)
				{
					value.F_UnsignedInteger = unsignedValues[selected];
				}
				else
				{
					value.F_Integer = signedValues[selected];
				}
			}
			break;
		}
		case P::V_Vector2:
		{
			const auto edited =
			    B::TakeBindingResult(Vector2Field(call, caption, {value.F_Vector.F_X, value.F_Vector.F_Y}));
			value.F_Vector = {edited.F_X, edited.F_Y, 0, 0};
			break;
		}
		case P::V_Vector3:
		{
			const auto edited = B::TakeBindingResult(
			    Vector3Field(call, caption, {value.F_Vector.F_X, value.F_Vector.F_Y, value.F_Vector.F_Z}));
			value.F_Vector = {edited.F_X, edited.F_Y, edited.F_Z, 0};
			break;
		}
		case P::V_Vector4:
			value.F_Vector = B::TakeBindingResult(Vector4Field(call, caption, value.F_Vector));
			break;
		case P::V_Quaternion:
		{
			const auto edited = B::TakeBindingResult(QuaternionField(
			    call, caption, {value.F_Vector.F_X, value.F_Vector.F_Y, value.F_Vector.F_Z, value.F_Vector.F_W}));
			value.F_Vector = {edited.F_X, edited.F_Y, edited.F_Z, edited.F_W};
			break;
		}
		case P::V_Color:
		{
			const auto edited = B::TakeBindingResult(ColorField(
			    call, caption, {value.F_Vector.F_X, value.F_Vector.F_Y, value.F_Vector.F_Z, value.F_Vector.F_W}));
			value.F_Vector = {edited.F_R, edited.F_G, edited.F_B, edited.F_A};
			break;
		}
		case P::V_ObjectReference:
			value.F_Reference =
			    B::TakeBindingResult(ObjectField(call, caption, value.F_Reference, state.F_ReferenceKind));
			value.F_IsNull = !value.F_Reference.present;
			break;
		case P::V_AssetReference:
			value.F_Reference =
			    B::TakeBindingResult(AssetField(call, caption, value.F_Reference, state.F_ReferenceKind));
			value.F_IsNull = !value.F_Reference.present;
			break;
		case P::V_Array:
		case P::V_List:
		{
			Int32 size = value.F_ArraySize;
			if (ImGui::InputInt(label.c_str(), &size) && size >= 0)
			{
				B::CompleteBindingResult(ResizeArray(call, scope.revision, fieldPath, size));
			}
			const auto refreshed = B::TakeBindingResult(GetProperty(call, scope.revision, fieldPath));
			for (Int32 index = 0; index < refreshed.F_Value.F_ArraySize; ++index)
			{
				DrawProperty(call, path + "/" + ToUrsaString(std::to_string(index)),
				             "Element " + ToUrsaString(std::to_string(index)));
			}
			return;
		}
		default:
			ImGui::TextDisabled("%s: preserved unsupported field", label.c_str());
			return;
	}
	const auto descriptor = Descriptor(scope, scope.revision, path);
	const auto property = B::TakeBindingResult(scope.properties->FindProperty(path));
	const auto before = B::TakeBindingResult(property.Value());
	const auto after = WriteValue(scope, value, descriptor, false);
	if (before != after)
	{
		B::CompleteBindingResult(SetProperty(call, scope.revision, fieldPath, value));
	}
}

Result<void> EditorServices::PropertyField(B::BindingCall& call, UInt64 revision, const B::Utf8View& path,
                                           const B::Utf8View& label)
{
	RequireGui(call);
	auto& scope = Scope(call);
	scope.RequireProperties(revision);
	DrawProperty(call, B::ReadString(path), B::ReadString(label));
	return {};
}

Result<bool> EditorServices::DrawDefaultInspector(B::BindingCall& call, UInt64 revision)
{
	RequireGui(call);
	auto& scope = Scope(call);
	scope.RequireProperties(revision);
	for (const auto& path : scope.properties->PropertyPaths())
	{
		DrawProperty(call, path, path.substr(String("/Fields/").size()));
	}
	return ApplyProperties(call, revision);
}
} // namespace Ursa::Editor::Bindings
