#include <Scene/RendererComponent.hpp>
#include <Scene/GameObject.hpp>
#include <Serialization/RenderResourceReferences.hpp>

namespace Ursa
{
const char* RendererComponent::UniquenessGroup() const
{
	return "ursa.Renderer";
}

Result<void> RendererComponent::Serialize(ArchiveWriter& writer) const
{
	auto shadowResult = writer.Write("castShadows", castShadows);
	if (shadowResult)
	{
		shadowResult = writer.Write("receiveShadows", receiveShadows);
	}
	if (!shadowResult)
	{
		return shadowResult;
	}
	auto array = writer.Array("materials");
	if (!array)
	{
		return Result<void>::Failure(array.error);
	}
	for (Size i = 0; i < materials.size(); ++i)
	{
		auto slot = array.value.AddObject();
		if (!slot)
		{
			return Result<void>::Failure(slot.error);
		}
		auto result = WriteMaterialBinding(slot.value, "value", materials[i]);
		if (result)
		{
			result = slot.value.Write("instantiated", instantiated[i]);
		}
		if (!result)
		{
			return result;
		}
	}
	return {};
}

Result<void> RendererComponent::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	const auto loadedCastShadows =
	    reader.Contains("castShadows") ? reader.Boolean("castShadows") : Result<bool>::Success(true);
	const auto loadedReceiveShadows =
	    reader.Contains("receiveShadows") ? reader.Boolean("receiveShadows") : Result<bool>::Success(true);
	if (!loadedCastShadows || !loadedReceiveShadows)
	{
		return Result<void>::Failure("Invalid Renderer shadow settings");
	}
	auto array = reader.Array("materials");
	if (!array || array.value.size() > Mesh::maxSubMeshes)
	{
		return Result<void>::Failure("Invalid Renderer material slot array");
	}
	Vector<std::shared_ptr<MaterialInstance>> loaded;
	Vector<bool> flags;
	for (const auto& slot : array.value)
	{
		auto result = ReadMaterialBinding(slot, "value", context);
		if (!result)
		{
			return Result<void>::Failure(result.error);
		}
		auto flag = slot.Contains("instantiated") ? slot.Boolean("instantiated") : Result<bool>::Success(false);
		if (!flag || (flag.value && result.value && result.value->AssetIdentity().guid))
		{
			return Result<void>::Failure("Invalid Renderer material instance marker");
		}
		flags.push_back(flag.value);
		loaded.push_back(std::move(result.value));
	}
	auto result = SetSharedMaterials(std::move(loaded));
	if (result)
	{
		instantiated = std::move(flags);
		castShadows = loadedCastShadows.value;
		receiveShadows = loadedReceiveShadows.value;
	}
	return result;
}

std::shared_ptr<MaterialInstance> RendererComponent::SharedMaterial() const
{
	return materials.empty() ? nullptr : materials.front();
}

Vector<std::shared_ptr<MaterialInstance>> RendererComponent::SharedMaterials() const
{
	return materials;
}

Result<void> RendererComponent::SetSharedMaterial(std::shared_ptr<MaterialInstance> value)
{
	if (value && !value->IsValid())
	{
		return Result<void>::Failure("Renderer cannot bind a destroyed material");
	}
	if (materials.empty())
	{
		Vector<bool> flags(1, false);
		materials.resize(1);
		instantiated = std::move(flags);
	}
	materials.front() = std::move(value);
	instantiated.front() = false;
	return {};
}

Result<void> RendererComponent::SetSharedMaterials(Vector<std::shared_ptr<MaterialInstance>> values)
{
	if (values.size() > Mesh::maxSubMeshes)
	{
		return Result<void>::Failure("Renderer material slot count exceeds capacity");
	}
	for (const auto& value : values)
	{
		if (value && !value->IsValid())
		{
			return Result<void>::Failure("Renderer cannot bind a destroyed material");
		}
	}
	Vector<bool> flags(values.size(), false);
	materials = std::move(values);
	instantiated = std::move(flags);
	return {};
}

Result<void> RendererComponent::SetMaterial(std::shared_ptr<MaterialInstance> value)
{
	auto result = SetSharedMaterial(std::move(value));
	if (result)
	{
		instantiated.front() = !materials.front() || !materials.front()->AssetIdentity().guid;
	}
	return result;
}

Result<void> RendererComponent::SetMaterials(Vector<std::shared_ptr<MaterialInstance>> values)
{
	auto result = SetSharedMaterials(std::move(values));
	if (result)
	{
		for (Size i = 0; i < materials.size(); ++i)
		{
			instantiated[i] = !materials[i] || !materials[i]->AssetIdentity().guid;
		}
	}
	return result;
}

Result<void> RendererComponent::RequireInstanceAccess() const
{
	auto owner = GetGameObject();
	return owner && owner->IsAssetTemplate()
	           ? Result<void>::Failure("Prefab asset Renderer cannot instantiate materials; use SharedMaterials")
	           : Result<void>{};
}

Result<std::shared_ptr<MaterialInstance>> RendererComponent::Material()
{
	auto allowed = RequireInstanceAccess();
	if (!allowed)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(allowed.error);
	}
	if (materials.empty() || !materials.front())
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(nullptr);
	}
	if (!materials.front()->IsValid())
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure("Renderer material has been destroyed");
	}
	try
	{
		if (!instantiated.front())
		{
			materials.front() = materials.front()->Clone();
			instantiated.front() = true;
		}
		return Result<std::shared_ptr<MaterialInstance>>::Success(materials.front());
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(String("Cannot instantiate material: ") +
		                                                          error.what());
	}
}

Result<Vector<std::shared_ptr<MaterialInstance>>> RendererComponent::Materials()
{
	auto allowed = RequireInstanceAccess();
	if (!allowed)
	{
		return Result<Vector<std::shared_ptr<MaterialInstance>>>::Failure(allowed.error);
	}
	try
	{
		auto candidates = materials;
		for (Size i = 0; i < candidates.size(); ++i)
		{
			if (candidates[i] && !candidates[i]->IsValid())
			{
				return Result<Vector<std::shared_ptr<MaterialInstance>>>::Failure(
				    "Renderer material has been destroyed");
			}
			if (candidates[i] && !instantiated[i])
			{
				candidates[i] = candidates[i]->Clone();
			}
		}
		Vector<bool> flags(candidates.size(), true);
		materials = std::move(candidates);
		instantiated = std::move(flags);
		return Result<Vector<std::shared_ptr<MaterialInstance>>>::Success(materials);
	}
	catch (const std::exception& error)
	{
		return Result<Vector<std::shared_ptr<MaterialInstance>>>::Failure(String("Cannot instantiate materials: ") +
		                                                                  error.what());
	}
}
} // namespace Ursa
