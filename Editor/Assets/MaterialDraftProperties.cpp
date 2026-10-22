#include <Assets/MaterialDraft.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Serialization/AssetReference.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;
using P = Shader::PropertyType;

Result<Json> Capture(const MaterialInstance& material)
{
	try
	{
		if (!material.IsValid() || !material.Shader())
		{
			return Result<Json>::Failure("Material property editing requires a live material and its Shader schema");
		}
		Json payload{{"Revision", material.Version()},
		             {"Shader", ToStdString(material.ShaderId().ToString())},
		             {"Fields", Json::object()},
		             {"FieldTypes", Json::object()}};
		for (const auto& property : material.Shader()->properties)
		{
			const auto value =
			    Scripting::Bindings::TakeBindingResult(material.PropertyValue(property.identifier, *material.Shader()));
			const auto name = ToStdString(property.identifier);
			const char* kind = property.type == P::Int         ? "SignedInteger"
			                   : property.type == P::Color     ? "Color"
			                   : property.type == P::Vector    ? "Vector4"
			                   : property.type == P::Texture2D ? "TextureReference"
			                                                   : "FloatingPoint";
			payload["FieldTypes"][name] = {{"Kind", kind},
			                               {"ElementKind", "Unsupported"},
			                               {"TypeName", property.type == P::Int         ? "System.Int32"
			                                            : property.type == P::Color     ? "Ursa.Color"
			                                            : property.type == P::Vector    ? "Ursa.Vector4"
			                                            : property.type == P::Texture2D ? "Ursa.TextureAsset"
			                                                                            : "System.Single"}};
			auto& descriptor = payload["FieldTypes"][name];
			descriptor["DisplayName"] =
			    ToStdString(property.displayName.empty() ? property.identifier : property.displayName);
			descriptor["Hidden"] = std::find(property.attributes.begin(), property.attributes.end(),
			                                 "HideInInspector") != property.attributes.end();
			descriptor["HDR"] =
			    std::find(property.attributes.begin(), property.attributes.end(), "HDR") != property.attributes.end();
			if (property.type == P::Range)
			{
				descriptor["Minimum"] = property.rangeMinimum;
				descriptor["Maximum"] = property.rangeMaximum;
			}
			switch (property.type)
			{
				case P::Int:
					payload["Fields"][name] = value.intValue;
					break;
				case P::Color:
				case P::Vector:
					payload["Fields"][name] = {value.vectorValue[0], value.vectorValue[1], value.vectorValue[2],
					                           value.vectorValue[3]};
					break;
				case P::Texture2D:
					payload["Fields"][name] =
					    value.textureId
					        ? Json{{"$ursaReference",
					                {{"Kind", "Texture"},
					                 {"Id", ""},
					                 {"Resource", AssetReferenceToJson({value.textureId, value.textureLocalFileId})}}}}
					        : Json(nullptr);
					break;
				default:
					payload["Fields"][name] = value.floatValue;
					break;
			}
		}
		return Result<Json>::Success(std::move(payload));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace

Result<nlohmann::json> MaterialDraft::DescribeProperties(const MaterialInstance& material)
{
	return Capture(material);
}

SerializedTargetAdapter MaterialDraft::Properties(ResourceSession& session, bool writable,
                                                  std::function<Result<void>()> validate)
{
	const auto owner = weak_from_this();
	SerializedTargetAdapter adapter;
	adapter.resources = &session;
	adapter.capture = [owner, validate]() -> Result<Json>
	{
		const auto draft = owner.lock();
		if (!draft)
		{
			return Result<Json>::Failure("Material Inspector target is closed");
		}
		if (validate)
		{
			const auto valid = validate();
			if (!valid)
			{
				return Result<Json>::Failure(valid.error);
			}
		}
		return Capture(*draft->material);
	};
	if (writable)
	{
		adapter.apply = [owner, &session, validate](const Json& payload) -> Result<Json>
		{
			try
			{
				const auto draft = owner.lock();
				if (!draft)
				{
					return Result<Json>::Failure("Material Inspector target is closed");
				}
				if (validate)
				{
					Scripting::Bindings::CompleteBindingResult(validate());
				}
				const auto previous = Scripting::Bindings::TakeBindingResult(Capture(*draft->material));
				auto candidate = draft->material->Clone();
				const auto shader = candidate->Shader();
				for (const auto& field : payload.at("Fields").items())
				{
					if (field.value() == previous.at("Fields").at(field.key()))
					{
						continue;
					}
					const auto property =
					    std::find_if(shader->properties.begin(), shader->properties.end(),
					                 [&](const auto& value) { return ToStdString(value.identifier) == field.key(); });
					if (property == shader->properties.end())
					{
						return Result<Json>::Failure("Material property schema changed");
					}
					MaterialPropertyValue value;
					value.type = property->type;
					switch (property->type)
					{
						case P::Int:
							value.intValue = field.value().get<Int32>();
							break;
						case P::Color:
						case P::Vector:
							for (Size index = 0; index < 4; ++index)
							{
								value.vectorValue[index] = field.value().at(index).get<Float32>();
							}
							break;
						case P::Texture2D:
						{
							if (field.value().is_null())
							{
								Scripting::Bindings::CompleteBindingResult(
								    candidate->RemovePropertyOverride(property->identifier));
								continue;
							}
							const auto reference = Scripting::Bindings::TakeBindingResult(AssetReferenceFromJson(
							    field.value().at("$ursaReference").at("Resource"), "Material texture property"));
							value.textureId = reference.guid;
							value.textureLocalFileId = reference.localFileId;
							value.texture = Scripting::Bindings::TakeBindingResult(
							    session.LoadTexture(reference.guid, reference.localFileId));
							break;
						}
						default:
							value.floatValue = field.value().get<Float32>();
							break;
					}
					Scripting::Bindings::CompleteBindingResult(
					    candidate->SetPropertyValue(property->identifier, *shader, std::move(value)));
				}
				Scripting::Bindings::CompleteBindingResult(candidate->ValidateAgainstShader(*shader));
				Scripting::Bindings::CompleteBindingResult(
				    candidate->BindLocalIdentity(draft->material->LocalIdentity()));
				if (draft->material->AssetIdentity().guid)
				{
					Scripting::Bindings::CompleteBindingResult(
					    candidate->BindAssetIdentity(draft->material->AssetIdentity()));
				}
				draft->material = std::move(candidate);
				return Capture(*draft->material);
			}
			catch (const std::exception& error)
			{
				return Result<Json>::Failure(ToUrsaString(error.what()));
			}
		};
	}
	return adapter;
}
} // namespace Ursa::Editor
