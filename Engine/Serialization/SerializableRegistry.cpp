#include <Serialization/SerializableRegistry.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/PrefabAsset.hpp>

namespace Ursa
{
Result<void> RegisterBuiltinSerializableTypes(SerializableRegistry& registry)
{
	auto result = registry.Register("ursa.GameObject", 1, [](SerializationContext&)
	                                { return Result<std::shared_ptr<Serializable>>::Success(GameObject::Create()); });
	if (result)
	{
		result =
		    registry.Register("ursa.Scene", 1, [](SerializationContext&)
		                      { return Result<std::shared_ptr<Serializable>>::Success(std::make_shared<Scene>()); });
	}
	if (result)
	{
		result = registry.Register(
		    "ursa.PrefabAsset", 1, [](SerializationContext&)
		    { return Result<std::shared_ptr<Serializable>>::Success(std::make_shared<PrefabAsset>()); });
	}
	if (result)
	{
		result = registry.Register("ursa.Material", 1,
		                           [](SerializationContext&)
		                           {
			                           return Result<std::shared_ptr<Serializable>>::Success(
			                               std::make_shared<MaterialInstance>(MaterialType::Unlit));
		                           });
	}
	if (result)
	{
		result = registry.Register(
		    "ursa.Texture", 1, [](SerializationContext&)
		    { return Result<std::shared_ptr<Serializable>>::Success(MaterialTexture::CreateForDeserialization()); });
	}
	return result;
}
} // namespace Ursa
