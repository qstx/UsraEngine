#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <memory>

namespace Ursa::Editor
{
class SerializedObject;

class SerializedProperty final
{
public:
	SerializedProperty() = default;

	Result<nlohmann::json> Value() const;

	Result<void> SetValue(nlohmann::json value);

	const String& Path() const
	{
		return path;
	}

private:
	friend class SerializedObject;

	SerializedProperty(SerializedObject& owner, String fieldPath) : owner(&owner), path(std::move(fieldPath))
	{
	}

	SerializedObject* owner = nullptr;
	String path;
};

class SerializedObject final
{
public:
	static Result<std::unique_ptr<SerializedObject>> Create(Scripting::ScriptBehaviourComponent& target,
	                                                        ResourceSession* resources = nullptr);

	static Result<std::unique_ptr<SerializedObject>> Create(Component& target, const ComponentRegistry& registry,
	                                                        ComponentLoadContext& context,
	                                                        const ComponentSaveContext& saveContext = {});

	Result<SerializedProperty> FindProperty(const String& path);

	Result<void> Apply();

	bool HasModifiedProperties() const
	{
		return !modifiedProperties.empty();
	}

private:
	friend class SerializedProperty;

	explicit SerializedObject(Component& target, nlohmann::json payload)
	    : target(target), originalPayload(payload), pendingPayload(std::move(payload))
	{
	}

	Result<nlohmann::json> PropertyValue(const String& path) const;

	Result<void> SetPropertyValue(const String& path, nlohmann::json value);

	Result<void> ValidateCandidate(const nlohmann::json& candidate) const;

	Component& target;
	std::function<Result<void>(const nlohmann::json&)> nativeApply;
	ResourceSession* resources = nullptr;
	nlohmann::json originalPayload;
	nlohmann::json pendingPayload;
	Map<String, nlohmann::json> modifiedProperties;
};
} // namespace Ursa::Editor
