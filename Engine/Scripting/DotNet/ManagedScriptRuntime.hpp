#pragma once

#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedBehaviourRuntime.hpp>
#include <Scripting/ManagedSceneApi.hpp>

namespace Ursa::Scripting
{
class ManagedScriptRuntime final : public ManagedBehaviourRuntime
{
public:
	ManagedScriptRuntime(Scene& scene, DotNetHost& host, ManagedSceneApi& sceneApi, UInt64 sessionId = 0);

	Result<ComponentId> AddExisting(const GameObject::Ptr& owner, const String& typeName,
	                                UInt64 instanceToken) override;

	Result<ComponentId> CreateAndAdd(const GameObject::Ptr& owner, const String& typeName) override;

	Result<void> AttachSerializedComponents();

	~ManagedScriptRuntime() override;

	Result<ManagedHandle> BoundHandle(UInt64 instanceToken) const override;

	Result<void> BindComponent(UInt64 instanceToken, Component& component) override;

	Result<void> Dispatch(UInt64 instanceToken, BehaviourEvent event, const FrameContext* context,
	                      Component* component) override;

	Result<void> Destroy(UInt64 instanceToken) override;

	Result<String> SerializeFields(UInt64 instanceToken) override;

	Result<void> DeserializeFields(UInt64 instanceToken, const String& payload) override;

private:
	Scene& scene;
	DotNetHost& host;
	ManagedSceneApi& sceneApi;
	UInt64 sessionId = 0;
	FrameContext lastFrameContext;
	bool hasFrameContext = false;
	Map<UInt64, ManagedHandle> boundHandles;
	std::function<void()> revokePreparation;
};
} // namespace Ursa::Scripting
