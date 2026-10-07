#include <Serialization/MaterialResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/JsonArchiveInterop.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Shader/ShaderBuiltins.hpp>

namespace Ursa
{
Result<void> SaveMaterialResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                  const MaterialInstance& material, const TextureIdResolver& resolveTexture)
{
	auto kind = ResourceKindForPath(path);
	if (!id || !kind || kind.value != ResourceKind::Material)
	{
		return Result<void>::Failure("Invalid material resource ID or .ursamat path: " + path.ToString());
	}
	auto serializedMaterial = material.Clone();
	if (!serializedMaterial->IsShaderBacked())
	{
		for (Ursa::Size slot = 0; slot < 5; ++slot)
		{
			const auto textureSlot = static_cast<MaterialTextureSlot>(slot);
			auto texture = serializedMaterial->Texture(textureSlot);
			if (!texture)
			{
				continue;
			}
			if (!resolveTexture)
			{
				return Result<void>::Failure("Missing texture ID resolver for built-in Shader migration");
			}
			auto textureId = resolveTexture(texture);
			if (!textureId)
			{
				return Result<void>::Failure(textureId.error);
			}
			auto assigned = serializedMaterial->SetTextureResourceId(textureSlot, textureId.value);
			if (!assigned)
			{
				return assigned;
			}
		}
		const ResourceId builtinShader = serializedMaterial->Type() == MaterialType::Unlit
		                                     ? Shader::BuiltinUnlitShaderId()
		                                     : Shader::BuiltinPbrShaderId();
		auto migrated = serializedMaterial->MigrateLegacyToShader(builtinShader);
		if (!migrated)
		{
			return Result<void>::Failure("Cannot migrate material to a built-in Shader: " + migrated.error);
		}
	}
	SerializationContext context;
	context.SetResourceIdResolver(
	    [&](const Serializable& value) -> Result<ResourceId>
	    {
		    auto* texture = dynamic_cast<const MaterialTexture*>(&value);
		    return texture && resolveTexture
		               ? resolveTexture(MaterialTexturePtr(texture, [](const MaterialTexture*) {}))
		               : Result<ResourceId>::Failure("Missing texture ID resolver");
	    });
	ArchiveWriter writer;
	writer.SetContext(&context);
	auto saved = serializedMaterial->Serialize(writer);
	if (!saved)
	{
		return Result<void>::Failure(path.ToString() + ": " + saved.error);
	}
	auto document = ArchiveToJson(writer);
	if (!document)
	{
		return Result<void>::Failure(document.error);
	}
	document.value["format"] = "UrsaMaterial";
	document.value["version"] = materialResourceFormatVersion;
	document.value["id"] = id.ToString();
	return WriteJsonResource(filesystem, path, document.value);
}

Result<LoadedMaterialResource> LoadMaterialResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                    const TextureResourceLoader& loadTexture)
{
	auto document = ReadJsonResource(filesystem, path, ResourceKind::Material);
	if (!document)
	{
		return Result<LoadedMaterialResource>::Failure(document.error);
	}
	const Ursa::UInt32 resourceVersion = document.value.value("version", Ursa::UInt32{0});
	if (resourceVersion != resourceFormatVersion && resourceVersion != materialResourceFormatVersion)
	{
		return Result<LoadedMaterialResource>::Failure("Unsupported material resource version at " + path.ToString());
	}
	auto reader = ArchiveFromJson(document.value);
	if (!reader)
	{
		return Result<LoadedMaterialResource>::Failure(reader.error);
	}
	SerializationContext context;
	auto loader = loadTexture;
	context.Provide(loader);
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	auto loaded = material->Deserialize(reader.value, context);
	if (!loaded)
	{
		return Result<LoadedMaterialResource>::Failure(path.ToString() + ": " + loaded.error);
	}
	if (resourceVersion == resourceFormatVersion)
	{
		const ResourceId builtinId =
		    material->Type() == MaterialType::Unlit ? Shader::BuiltinUnlitShaderId() : Shader::BuiltinPbrShaderId();
		auto migrated = material->MigrateLegacyToShader(builtinId);
		if (!migrated)
		{
			return Result<LoadedMaterialResource>::Failure(path.ToString() + ": " + migrated.error);
		}
	}
	auto id = ResourceId::Parse(document.value["id"].get<String>());
	return id ? Result<LoadedMaterialResource>::Success({id.value, std::move(material)})
	          : Result<LoadedMaterialResource>::Failure(id.error);
}

} // namespace Ursa
