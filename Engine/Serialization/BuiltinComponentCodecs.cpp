#include <Serialization/ComponentRegistry.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>

namespace Ursa
{
Result<void> RegisterBuiltinComponents(ComponentRegistry& registry)
{
	auto result = registry.RegisterSerializable<CameraComponent>("ursa.Camera", 1,
	                                                             [](const GameObject::Ptr& owner, ComponentLoadContext&)
	                                                             { return owner->AddComponent<CameraComponent>(); });
	if (result)
	{
		result = registry.RegisterSerializable<BoxRenderComponent>(
		    "ursa.BoxRender", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<BoxRenderComponent>(); });
	}
	if (result)
	{
		result = registry.RegisterSerializable<MeshRendererComponent>(
		    "ursa.MeshRenderer", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<MeshRendererComponent>(); });
	}
	if (result)
	{
		result = registry.RegisterSerializable<SkinnedMeshRendererComponent>(
		    "ursa.SkinnedMeshRenderer", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<SkinnedMeshRendererComponent>(); });
	}
	if (result)
	{
		result = registry.RegisterSerializable<AnimatorComponent>(
		    "ursa.Animator", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<AnimatorComponent>(); });
	}
	if (result)
	{
		result = registry.RegisterSerializable<DirectionalLightComponent>(
		    "ursa.DirectionalLight", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<DirectionalLightComponent>(); });
	}
	if (result)
	{
		result = registry.RegisterSerializable<PointLightComponent>(
		    "ursa.PointLight", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<PointLightComponent>(); });
	}
	if (result)
	{
		result = registry.RegisterSerializable<Scripting::ScriptBehaviourComponent>(
		    "ursa.ScriptBehaviour", 1, [](const GameObject::Ptr& owner, ComponentLoadContext&)
		    { return owner->AddComponent<Scripting::ScriptBehaviourComponent>(String{}); });
	}
	return result;
}
} // namespace Ursa
