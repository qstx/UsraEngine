#include <Core/Log.hpp>
#include <Core/Profiler.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/Archive.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <nlohmann/json.hpp>

namespace Ursa::Scripting
{
namespace
{
Result<String> MergeUnknownFields(const String& existingPayload, const String& currentPayload)
{
	if (existingPayload.empty())
	{
		return Result<String>::Success(currentPayload);
	}
	try
	{
		nlohmann::json existing = nlohmann::json::parse(ToStdString(existingPayload));
		nlohmann::json current = nlohmann::json::parse(ToStdString(currentPayload));
		if (!existing.is_object() || !existing.contains("Fields") || !existing["Fields"].is_object() ||
		    !current.is_object() || !current.contains("Fields") || !current["Fields"].is_object())
		{
			return Result<String>::Failure("Managed script payload does not contain an object Fields table");
		}
		Set<String> formerNames;
		if (current.contains("FormerFieldNames") && current["FormerFieldNames"].is_object())
		{
			for (const auto& aliases : current["FormerFieldNames"].items())
			{
				if (aliases.value().is_array())
				{
					for (const auto& alias : aliases.value())
					{
						if (alias.is_string())
						{
							formerNames.insert(ToUrsaString(alias.get<std::string>()));
						}
					}
				}
			}
		}
		for (const auto& field : existing["Fields"].items())
		{
			const String name = ToUrsaString(field.key());
			if (!current["Fields"].contains(field.key()) && !formerNames.count(name))
			{
				current["Fields"][field.key()] = field.value();
			}
		}
		return Result<String>::Success(ToUrsaString(current.dump()));
	}
	catch (const std::exception& error)
	{
		return Result<String>::Failure("Cannot preserve unknown managed script fields: " + ToUrsaString(error.what()));
	}
}
} // namespace

ScriptBehaviourComponent::ScriptBehaviourComponent(String typeName) : typeName(std::move(typeName))
{
}

ScriptBehaviourComponent::ScriptBehaviourComponent(ManagedBehaviourRuntime& runtime, String typeName,
                                                   UInt64 instanceToken)
    : runtime(&runtime), typeName(std::move(typeName)), instanceToken(instanceToken)
{
}

Result<void> ScriptBehaviourComponent::Serialize(ArchiveWriter& writer) const
{
	auto savedType = writer.Write("typeName", typeName);
	if (!savedType)
	{
		return savedType;
	}
	String payload = serializedPayload;
	if (instanceToken != 0)
	{
		auto current = runtime->SerializeFields(instanceToken);
		if (!current)
		{
			return Result<void>::Failure(current.error);
		}
		auto merged = MergeUnknownFields(serializedPayload, current.value);
		if (!merged)
		{
			return Result<void>::Failure(merged.error);
		}
		payload = std::move(merged.value);
	}
	try
	{
		nlohmann::json document = nlohmann::json::parse(ToStdString(payload));
		auto object = writer.Object("scriptPayload");
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		return ArchiveWriteJson(object.value, document);
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Cannot write managed script payload: " + ToUrsaString(error.what()));
	}
}

Result<void> ScriptBehaviourComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto savedType = reader.Text("typeName");
	if (!savedType || savedType.value.empty())
	{
		return Result<void>::Failure(savedType ? "Script behaviour type name is empty" : savedType.error);
	}
	typeName = std::move(savedType.value);
	if (reader.Contains("scriptPayload"))
	{
		auto payload = reader.Object("scriptPayload");
		if (!payload)
		{
			return Result<void>::Failure(payload.error);
		}
		auto document = ArchiveToJson(payload.value);
		if (!document)
		{
			return Result<void>::Failure(document.error);
		}
		serializedPayload = ToUrsaString(document.value.dump());
		if (!serializedPayload.empty())
		{
			if (auto* componentContext = dynamic_cast<ComponentLoadContext*>(&context))
			{
				context.Defer([this, componentContext]() { return RemapReferences(*componentContext); });
			}
		}
	}
	return {};
}

Result<void> ScriptBehaviourComponent::RemapReferences(ComponentLoadContext& context)
{
	if (serializedPayload.empty())
	{
		return {};
	}
	try
	{
		nlohmann::json payload = nlohmann::json::parse(ToStdString(serializedPayload));
		if (!payload.is_object() || !payload.contains("Fields") || !payload["Fields"].is_object())
		{
			return Result<void>::Failure("Managed script payload has no Fields object");
		}
		const auto remap = [&context](auto&& self, nlohmann::json& value) -> Result<void>
		{
			if (value.is_array())
			{
				for (auto& item : value)
				{
					auto result = self(self, item);
					if (!result)
					{
						return result;
					}
				}
				return {};
			}
			if (!value.is_object())
			{
				return {};
			}
			if (value.contains("$ursaReference"))
			{
				auto& reference = value["$ursaReference"];
				if (!reference.is_object() || !reference.contains("Kind") || !reference["Kind"].is_string() ||
				    !reference.contains("Id") || !reference["Id"].is_string())
				{
					return Result<void>::Failure("Managed script reference payload is malformed");
				}
				auto id = ResourceId::Parse(ToUrsaString(reference["Id"].get<std::string>()));
				if (!id)
				{
					return Result<void>::Failure(id.error);
				}
				const std::string kind = reference["Kind"].get<std::string>();
				ResourceId remappedId;
				if (kind == "GameObject")
				{
					auto object = context.GameObjectById(id.value);
					if (!object)
					{
						return Result<void>::Failure(object.error);
					}
					remappedId = object.value->Id();
				}
				else if (kind == "Component")
				{
					auto component = context.ComponentObjectById(id.value);
					if (!component)
					{
						return Result<void>::Failure(component.error);
					}
					remappedId = component.value->PersistentId();
				}
				else
				{
					return Result<void>::Failure("Unknown managed script reference kind " + ToUrsaString(kind));
				}
				reference["Id"] = ToStdString(remappedId.ToString());
				return {};
			}
			for (auto& entry : value.items())
			{
				auto result = self(self, entry.value());
				if (!result)
				{
					return result;
				}
			}
			return {};
		};
		auto remapped = remap(remap, payload["Fields"]);
		if (!remapped)
		{
			return remapped;
		}
		const std::string encoded = payload.dump();
		serializedPayload = ToUrsaString(encoded);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Cannot remap managed script references: " + ToUrsaString(error.what()));
	}
}

Result<void> ScriptBehaviourComponent::RestoreSerializedPayload()
{
	if (serializedPayload.empty())
	{
		return {};
	}
	if (instanceToken == 0)
	{
		return Result<void>::Failure("Cannot restore script fields before creating a managed instance");
	}
	return runtime ? runtime->DeserializeFields(instanceToken, serializedPayload)
	               : Result<void>::Failure("Managed Behaviour runtime is not attached");
}

Result<void> ScriptBehaviourComponent::AttachManagedRuntime(ManagedBehaviourRuntime& managedRuntime, UInt64 token)
{
	if (runtime || instanceToken != 0 || token == 0 || typeName.empty())
	{
		return Result<void>::Failure("Managed script component is already active or has invalid type/token state");
	}
	runtime = &managedRuntime;
	instanceToken = token;
	return {};
}

Result<void> ScriptBehaviourComponent::PrepareRuntimeBinding()
{
	return runtime && instanceToken ? runtime->BindComponent(instanceToken, *this) : Result<void>{};
}

Result<void> ScriptBehaviourComponent::Awake(const FrameContext& context)
{
	return Dispatch(BehaviourEvent::Awake, &context, true);
}

void ScriptBehaviourComponent::OnEnable()
{
	Dispatch(BehaviourEvent::OnEnable, nullptr, true);
}

Result<void> ScriptBehaviourComponent::Start(const FrameContext& context)
{
	return Dispatch(BehaviourEvent::Start, &context, true);
}

Result<void> ScriptBehaviourComponent::FixedUpdate(const FrameContext& context)
{
	return Dispatch(BehaviourEvent::FixedUpdate, &context, true);
}

Result<void> ScriptBehaviourComponent::Update(const FrameContext& context)
{
	return Dispatch(BehaviourEvent::Update, &context, true);
}

Result<void> ScriptBehaviourComponent::LateUpdate(const FrameContext& context)
{
	return Dispatch(BehaviourEvent::LateUpdate, &context, true);
}

void ScriptBehaviourComponent::OnDisable()
{
	Dispatch(BehaviourEvent::OnDisable, nullptr, false);
}

void ScriptBehaviourComponent::OnDestroy()
{
	Dispatch(BehaviourEvent::OnDestroy, nullptr, false);
}

void ScriptBehaviourComponent::OnRemove()
{
	if (destroyed)
	{
		return;
	}
	destroyed = true;
	if (instanceToken != 0)
	{
		auto result = runtime ? runtime->Destroy(instanceToken)
		                      : Result<void>::Failure("Managed Behaviour runtime is not attached");
		if (!result)
		{
			URSA_LOG_ERROR("Script", "托管行为实例销毁失败：" + result.error);
		}
		instanceToken = 0;
	}
}

Result<void> ScriptBehaviourComponent::Dispatch(BehaviourEvent event, const FrameContext* context,
                                                bool disableOnFailure)
{
	if (destroyed || !runtime || instanceToken == 0 || (callbackFailed && disableOnFailure))
	{
		return {};
	}
	const char* callbackName = "Unknown";
	switch (event)
	{
		case BehaviourEvent::Awake:
			callbackName = "Awake";
			break;
		case BehaviourEvent::OnEnable:
			callbackName = "OnEnable";
			break;
		case BehaviourEvent::Start:
			callbackName = "Start";
			break;
		case BehaviourEvent::FixedUpdate:
			callbackName = "FixedUpdate";
			break;
		case BehaviourEvent::Update:
			callbackName = "Update";
			break;
		case BehaviourEvent::LateUpdate:
			callbackName = "LateUpdate";
			break;
		case BehaviourEvent::OnDisable:
			callbackName = "OnDisable";
			break;
		case BehaviourEvent::OnDestroy:
			callbackName = "OnDestroy";
			break;
	}
	const String zoneName = typeName + "::" + callbackName;
	URSA_PROFILE_SCOPE_DYNAMIC(zoneName.data(), zoneName.size());
	auto invoked = runtime->Dispatch(instanceToken, event, context, this);
	if (invoked)
	{
		return {};
	}
	callbackFailed = true;
	URSA_LOG_ERROR("Script", "托管行为 " + typeName + " 回调失败：" + invoked.error);
	if (disableOnFailure && IsEnabled())
	{
		SetEnabled(false);
	}
	return {};
}
} // namespace Ursa::Scripting
