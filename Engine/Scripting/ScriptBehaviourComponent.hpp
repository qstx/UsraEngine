#pragma once

#include <Scene/Component.hpp>
#include <Scripting/ManagedBehaviourRuntime.hpp>

namespace Ursa
{
class ComponentLoadContext;
}

namespace Ursa::Scripting
{
class ScriptBehaviourComponent final : public Component
{
public:
	explicit ScriptBehaviourComponent(String typeName = {});

	ScriptBehaviourComponent(ManagedBehaviourRuntime& runtime, String typeName, UInt64 instanceToken);

	String SerializationType() const override
	{
		return "ursa.ScriptBehaviour";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	const String& TypeName() const
	{
		return typeName;
	}

	void SetTypeName(String value)
	{
		typeName = std::move(value);
	}

	bool HasSerializedPayload() const
	{
		return !serializedPayload.empty();
	}

	void SetSerializedPayload(String value)
	{
		serializedPayload = std::move(value);
	}

	const String& SerializedPayload() const
	{
		return serializedPayload;
	}

	Result<void> RestoreSerializedPayload();

	Result<void> AttachManagedRuntime(ManagedBehaviourRuntime& managedRuntime, UInt64 token);

	UInt64 InstanceToken() const
	{
		return instanceToken;
	}

	Result<void> PrepareRuntimeBinding() override;

	Result<void> Awake(const FrameContext& context) override;

	void OnEnable() override;

	Result<void> Start(const FrameContext& context) override;

	Result<void> FixedUpdate(const FrameContext& context) override;

	Result<void> Update(const FrameContext& context) override;

	Result<void> LateUpdate(const FrameContext& context) override;

	void OnDisable() override;

	void OnDestroy() override;

	void OnRemove() override;

private:
	Result<void> Dispatch(BehaviourEvent event, const FrameContext* context, bool disableOnFailure);

	Result<void> RemapReferences(ComponentLoadContext& context);

	ManagedBehaviourRuntime* runtime = nullptr;
	String typeName;
	String serializedPayload;
	UInt64 instanceToken = 0;
	bool callbackFailed = false;
	bool destroyed = false;
};
} // namespace Ursa::Scripting
