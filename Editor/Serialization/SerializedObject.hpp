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

struct SerializedTargetCheckpoint
{
	nlohmann::json serializedPayload;
	nlohmann::json sourceData;
	nlohmann::json prefabOverrides;
	UInt64 revision = 0;
	bool dirty = false;
	Vector<std::shared_ptr<void>> retainedResources;
	nlohmann::json backendToken;
};

struct SerializedTargetAdapter
{
	std::function<Result<nlohmann::json>()> capture;
	// Commits a complete typed payload to the owning draft and returns its canonical snapshot.
	std::function<Result<nlohmann::json>(const nlohmann::json&)> apply;
	std::function<Result<void>(const nlohmann::json&, const nlohmann::json&)> validateReference;
	// Optional transaction hooks. Batch editing requires the complete set; the token must retain
	// everything needed to either publish the prepared candidate or restore the captured target.
	std::function<Result<SerializedTargetCheckpoint>(const nlohmann::json&, const nlohmann::json&)> prepare;
	std::function<Result<void>(const SerializedTargetCheckpoint&)> validate;
	std::function<Result<nlohmann::json>(const SerializedTargetCheckpoint&)> publish;
	std::function<Result<void>(const SerializedTargetCheckpoint&)> restore;
	std::function<void(const nlohmann::json&)> refresh;
	ResourceSession* resources = nullptr;
};

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
	static Result<std::unique_ptr<SerializedObject>> Create(SerializedTargetAdapter adapter);

	static Result<std::unique_ptr<SerializedObject>> Create(Scripting::ScriptBehaviourComponent& target,
	                                                        ResourceSession* resources = nullptr);

	static Result<std::unique_ptr<SerializedObject>> Create(Component& target, const ComponentRegistry& registry,
	                                                        ComponentLoadContext& context,
	                                                        const ComponentSaveContext& saveContext = {});

	Result<SerializedProperty> FindProperty(const String& path);

	Result<void> Update();

	Result<nlohmann::json> DescribeProperty(const String& path) const;

	Vector<String> PropertyPaths() const;

	Result<void> Apply();
	static Result<void> ApplyAtomically(const Vector<SerializedObject*>& targets);
	Result<bool> ApplyFields(const nlohmann::json& fields);
	Result<void> ValidateAll() const;

	const nlohmann::json& Snapshot() const
	{
		return pendingPayload;
	}

	bool HasModifiedProperties() const
	{
		return !modifiedProperties.empty();
	}

private:
	friend class SerializedProperty;
	friend class SerializedObjectGroup;

	explicit SerializedObject(Component& target, nlohmann::json payload)
	    : target(&target), originalPayload(payload), pendingPayload(std::move(payload))
	{
	}

	explicit SerializedObject(nlohmann::json payload) : originalPayload(payload), pendingPayload(std::move(payload))
	{
	}

	Result<nlohmann::json> PropertyValue(const String& path) const;

	Result<void> SetPropertyValue(const String& path, nlohmann::json value);

	Result<void> ValidateCandidate(const nlohmann::json& candidate) const;
	Result<SerializedTargetCheckpoint> PrepareAtomic() const;
	Result<void> ValidatePrepared(const SerializedTargetCheckpoint& prepared) const;
	Result<nlohmann::json> PublishPrepared(const SerializedTargetCheckpoint& prepared);
	Result<void> RestorePrepared(const SerializedTargetCheckpoint& prepared);

	Component* target = nullptr;
	std::function<Result<nlohmann::json>(const nlohmann::json&)> adapterApply;
	std::function<Result<void>(const nlohmann::json&, const nlohmann::json&)> adapterReference;
	std::function<Result<nlohmann::json>(const nlohmann::json&)> nativeApply;
	std::function<Result<nlohmann::json>()> captureCurrent;
	std::function<Result<SerializedTargetCheckpoint>(const nlohmann::json&, const nlohmann::json&)> adapterPrepare;
	std::function<Result<void>(const SerializedTargetCheckpoint&)> adapterValidate;
	std::function<Result<nlohmann::json>(const SerializedTargetCheckpoint&)> adapterPublish;
	std::function<Result<void>(const SerializedTargetCheckpoint&)> adapterRestore;
	std::function<void(const nlohmann::json&)> adapterRefresh;
	ResourceSession* resources = nullptr;
	ComponentLoadContext* referenceContext = nullptr;
	nlohmann::json originalPayload;
	nlohmann::json pendingPayload;
	Map<String, nlohmann::json> modifiedProperties;
};

class SerializedObjectGroup final
{
public:
	SerializedObjectGroup() = default;

	static Result<SerializedObjectGroup> Create(Vector<SerializedObject*> targets, Size primaryIndex = 0);

	Vector<String> CommonPropertyPaths() const;
	Vector<bool> PendingTargetChanges() const;
	Result<void> Update();
	Result<nlohmann::json> TargetSnapshot(Size targetIndex) const;
	Result<nlohmann::json> DescribeProperty(const String& path) const;
	Result<nlohmann::json> TargetValue(Size targetIndex, const String& path) const;
	Result<nlohmann::json> TargetValues(const String& path) const;
	Result<Vector<bool>> CompareTargetValues(const String& path, const nlohmann::json& checkpoint,
	                                         const nlohmann::json& candidate) const;
	Result<void> SetValue(const String& path, nlohmann::json value);
	Result<void> SetTargetValues(const String& path, const nlohmann::json& values);
	Result<void> RestoreTargetValues(const String& path, const nlohmann::json& values);
	Result<bool> SetTargetFields(Size targetIndex, const nlohmann::json& fields);
	Result<void> ResizeCollection(const String& path, Size newSize);
	Result<void> MoveCollectionElement(const String& path, Size fromIndex, Size toIndex);
	Result<void> AppendReferences(const String& path, const Vector<nlohmann::json>& references);
	Result<void> Apply();

	Size TargetCount() const
	{
		return targets.size();
	}

	Size PrimaryIndex() const
	{
		return primaryIndex;
	}

private:
	SerializedObjectGroup(Vector<SerializedObject*> inputTargets, Size activeIndex)
	    : targets(std::move(inputTargets)), primaryIndex(activeIndex)
	{
	}

	Vector<SerializedObject*> targets;
	Size primaryIndex = 0;
};
} // namespace Ursa::Editor
