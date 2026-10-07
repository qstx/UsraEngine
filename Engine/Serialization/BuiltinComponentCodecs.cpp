#include <Serialization/ComponentRegistry.hpp>

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
	return result;
}
} // namespace Ursa
