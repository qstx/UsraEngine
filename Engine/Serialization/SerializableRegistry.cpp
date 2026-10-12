#include <Serialization/SerializableRegistry.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Shader/ShaderAsset.hpp>
#include <Mesh/Mesh.hpp>

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
		result = registry.Register("ursa.Mesh", 1,
		                           [](SerializationContext&) -> Result<std::shared_ptr<Serializable>>
		                           {
			                           auto mesh = Mesh::Create();
			                           return mesh ? Result<std::shared_ptr<Serializable>>::Success(mesh.value)
			                                       : Result<std::shared_ptr<Serializable>>::Failure(mesh.error);
		                           });
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
	if (result)
	{
		result = registry.Register(
		    "ursa.Shader", 1, [](SerializationContext&)
		    { return Result<std::shared_ptr<Serializable>>::Success(std::make_shared<Shader::ShaderAsset>()); });
	}
	return result;
}
} // namespace Ursa
