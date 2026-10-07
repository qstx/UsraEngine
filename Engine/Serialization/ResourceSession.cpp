#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderBuiltins.hpp>

namespace Ursa
{
Result<std::unique_ptr<ResourceSession>> ResourceSession::Open(const VirtualFileSystem& filesystem)
{
	Vector<ResourceMount> mounts;
	for (const auto& mount : filesystem.Providers())
	{
		mounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	return Open(mounts);
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Open(const Vector<ResourceMount>& mounts)
{
	auto session = std::unique_ptr<ResourceSession>(new ResourceSession);
	for (const ResourceMount& mount : mounts)
	{
		auto added = session->index.Mount(mount);
		if (!added)
		{
			return Result<std::unique_ptr<ResourceSession>>::Failure(added.error);
		}
	}
	auto scanned = session->index.Rebuild();
	if (!scanned)
	{
		return Result<std::unique_ptr<ResourceSession>>::Failure(scanned.error);
	}
	return Result<std::unique_ptr<ResourceSession>>::Success(std::move(session));
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Open(const std::filesystem::path& root, MountAccess access)
{
	Vector<ResourceMount> mounts;
	mounts.push_back(ResourceMount{projectScheme, root, access});
	return Open(mounts);
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::Refreshed() const
{
	return Open(index.Mounts());
}

Result<MaterialTexturePtr> ResourceSession::LoadTexture(ResourceId id)
{
	const auto key = id.ToString();
	auto cached = textures.find(key);
	if (cached != textures.end())
	{
		return Result<MaterialTexturePtr>::Success(cached->second);
	}
	auto location = index.Resolve(id);
	if (!location)
	{
		return Result<MaterialTexturePtr>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Texture)
	{
		return Result<MaterialTexturePtr>::Failure("Resource " + key +
		                                           " is not a texture: " + location.value.path.ToString());
	}

	auto loaded = LoadTextureResource(index.Filesystem(), location.value.path);
	if (!loaded)
	{
		return Result<MaterialTexturePtr>::Failure(loaded.error);
	}
	if (loaded.value.id != id)
	{
		return Result<MaterialTexturePtr>::Failure("Texture ID changed after indexing: " +
		                                           location.value.path.ToString());
	}
	textures.emplace(key, loaded.value.texture);
	URSA_LOG_DEBUG("Engine", "Loaded texture " + key + " from " + location.value.path.ToString());
	return Result<MaterialTexturePtr>::Success(std::move(loaded.value.texture));
}

Result<std::shared_ptr<MaterialInstance>> ResourceSession::LoadMaterial(ResourceId id)
{
	const auto key = id.ToString();
	auto cached = materials.find(key);
	if (cached != materials.end())
	{
		return Result<std::shared_ptr<MaterialInstance>>::Success(cached->second);
	}
	auto location = index.Resolve(id);
	if (!location)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Material)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(
		    "Resource " + key + " is not a material: " + location.value.path.ToString());
	}

	auto loaded = LoadMaterialResource(index.Filesystem(), location.value.path,
	                                   [this](ResourceId textureId) { return LoadTexture(textureId); });
	if (!loaded)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure(loaded.error);
	}
	if (loaded.value.material->IsShaderBacked())
	{
		auto shader = LoadShader(loaded.value.material->ShaderId());
		if (!shader)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(location.value.path.ToString() + ": " +
			                                                          shader.error);
		}
		auto validated = loaded.value.material->ValidateAgainstShader(*shader.value);
		if (!validated)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(location.value.path.ToString() + ": " +
			                                                          validated.error);
		}
		auto attachedShader = loaded.value.material->SetShaderAsset(shader.value);
		if (!attachedShader)
		{
			return Result<std::shared_ptr<MaterialInstance>>::Failure(location.value.path.ToString() + ": " +
			                                                          attachedShader.error);
		}
	}
	if (loaded.value.id != id)
	{
		return Result<std::shared_ptr<MaterialInstance>>::Failure("Material ID changed after indexing: " +
		                                                          location.value.path.ToString());
	}
	materials.emplace(key, loaded.value.material);
	URSA_LOG_DEBUG("Engine", "Loaded material " + key + " from " + location.value.path.ToString());
	return Result<std::shared_ptr<MaterialInstance>>::Success(std::move(loaded.value.material));
}

Result<std::shared_ptr<const Shader::ShaderAsset>> ResourceSession::LoadShader(ResourceId id)
{
	Set<String> active;
	return LoadShaderInternal(id, active);
}

Result<std::shared_ptr<const Shader::ShaderAsset>> ResourceSession::LoadShaderInternal(ResourceId id,
                                                                                       Set<String>& active)
{
	const auto key = id.ToString();
	if (active.find(key) != active.end())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure("Shader resource reference cycle at " + key);
	}
	auto cached = shaders.find(key);
	if (cached != shaders.end())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(cached->second);
	}
	auto location = index.Resolve(id);
	if (!location)
	{
		if (id == Shader::BuiltinUnlitShaderId())
		{
			auto builtin = std::make_shared<const Shader::ShaderAsset>(Shader::CreateBuiltinUnlitShaderAsset());
			shaders.emplace(key, builtin);
			return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(std::move(builtin));
		}
		if (id == Shader::BuiltinPbrShaderId())
		{
			auto builtin = std::make_shared<const Shader::ShaderAsset>(Shader::CreateBuiltinPbrShaderAsset());
			shaders.emplace(key, builtin);
			return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(std::move(builtin));
		}
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Shader)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(
		    "Resource " + key + " is not a Shader asset: " + location.value.path.ToString());
	}
	auto loaded = Shader::LoadShaderAssetResource(index.Filesystem(), location.value.path);
	if (!loaded)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(loaded.error);
	}
	if (loaded.value.id != id)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure("Shader ID changed after indexing: " +
		                                                                   location.value.path.ToString());
	}
	active.insert(key);
	auto mutableShader = std::make_shared<Shader::ShaderAsset>(std::move(loaded.value));
	for (const Shader::ShaderReference& reference : mutableShader->references)
	{
		auto referencedShader = LoadShaderInternal(reference.id, active);
		if (!referencedShader)
		{
			active.erase(key);
			return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(location.value.path.ToString() + ": " +
			                                                                   referencedShader.error);
		}
		mutableShader->resolvedReferences.push_back({reference, std::move(referencedShader.value)});
	}
	active.erase(key);
	std::shared_ptr<const Shader::ShaderAsset> shader = std::move(mutableShader);
	shaders.emplace(key, shader);
	URSA_LOG_DEBUG("Engine", "Loaded Shader " + key + " from " + location.value.path.ToString());
	return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(std::move(shader));
}

Result<ResourceId> ResourceSession::IdentifyMaterial(const std::shared_ptr<MaterialInstance>& material) const
{
	for (const auto& entry : materials)
	{
		if (entry.second == material)
		{
			return ResourceId::Parse(entry.first);
		}
	}
	return Result<ResourceId>::Failure("Material object is not in this resource session");
}
} // namespace Ursa
