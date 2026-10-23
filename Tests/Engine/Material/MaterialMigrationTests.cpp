#include <ResourceFixtures.hpp>
#include <Material/Material.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Vfs/MemoryFileProvider.hpp>

#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace
{
using namespace Ursa;
using Json = nlohmann::json;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* value)
{
	auto parsed = VirtualPath::Parse(value);
	Expect(static_cast<bool>(parsed), "Invalid fixture VFS path");
	return parsed.value;
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem filesystem;
		auto provider = std::make_shared<MemoryFileProvider>();
		Expect(static_cast<bool>(filesystem.MountProvider("prj", provider, MountAccess::ReadWrite)),
		       "Cannot mount migration fixture VFS");
		const ResourceId textureId = ResourceId::Generate();
		const ResourceId materialId = ResourceId::Generate();
		const ResourceId unlitId = ResourceId::Generate();
		auto texture = MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {210, 80, 20, 255});
		Expect(static_cast<bool>(texture), "Cannot create migration texture");
		Expect(static_cast<bool>(SaveTextureResource(filesystem, Path("prj:Base.ursatex"), textureId, *texture.value)),
		       "Cannot save migration texture");
		Expect(static_cast<bool>(WriteAssetMetadata(filesystem, Path("prj:Base.ursatex"), textureId)),
		       "Cannot save migration texture metadata");

		Json legacy = {{"format", "UrsaMaterial"},
		               {"version", resourceFormatVersion},
		               {"id", ToStdString(materialId.ToString())},
		               {"type", "PbrMetallicRoughness"},
		               {"parameters",
		                {{"baseColor", {0.2f, 0.4f, 0.6f, 1.0f}},
		                 {"metallic", 0.75f},
		                 {"roughness", 0.3f},
		                 {"normalScale", 1.5f},
		                 {"occlusionStrength", 0.6f},
		                 {"emissive", {0.1f, 0.2f, 0.3f, 1.0f}},
		                 {"alphaMode", "Mask"},
		                 {"alphaCutoff", 0.35f},
		                 {"sampler", {{"linear", false}, {"repeat", false}}}}},
		               {"textures", {{"baseColor", ToStdString(textureId.ToString())}}}};
		Expect(static_cast<bool>(WriteJsonResource(filesystem, Path("prj:Material.ursamat"), legacy)),
		       "Cannot write legacy material fixture");
		Expect(static_cast<bool>(WriteAssetMetadata(filesystem, Path("prj:Material.ursamat"), materialId)),
		       "Cannot write legacy material metadata");
		Json legacyUnlit = {{"format", "UrsaMaterial"},
		                    {"version", resourceFormatVersion},
		                    {"id", ToStdString(unlitId.ToString())},
		                    {"type", "Unlit"},
		                    {"parameters",
		                     {{"baseColor", {0.7f, 0.6f, 0.5f, 1.0f}},
		                      {"alphaMode", "Opaque"},
		                      {"alphaCutoff", 0.5f},
		                      {"sampler", {{"linear", true}, {"repeat", true}}}}},
		                    {"textures", Json::object()}};
		Expect(static_cast<bool>(WriteJsonResource(filesystem, Path("prj:Unlit.ursamat"), legacyUnlit)),
		       "Cannot write legacy Unlit material fixture");
		Expect(static_cast<bool>(WriteAssetMetadata(filesystem, Path("prj:Unlit.ursamat"), unlitId)),
		       "Cannot write legacy Unlit material metadata");

		const auto loadTexture = [textureId, texture](ResourceId id)
		{
			return id == textureId ? Result<MaterialTexturePtr>::Success(texture.value)
			                       : Result<MaterialTexturePtr>::Failure("Unknown migration texture");
		};
		auto loaded = LoadMaterialResource(filesystem, Path("prj:Material.ursamat"), loadTexture);
		Expect(static_cast<bool>(loaded), "Legacy material did not load");
		Expect(loaded.value.material->IsShaderBacked() &&
		           loaded.value.material->ShaderId() == Shader::BuiltinPbrShaderId() &&
		           loaded.value.material->Type() == MaterialType::PbrMetallicRoughness,
		       "Legacy PBR material did not map to the built-in PBR Shader identity");
		Expect(loaded.value.material->Parameters().metallic == 0.75f &&
		           loaded.value.material->Parameters().roughness == 0.3f &&
		           loaded.value.material->Parameters().normalScale == 1.5f &&
		           loaded.value.material->Parameters().occlusionStrength == 0.6f &&
		           loaded.value.material->Parameters().emissive.red == 0.1f &&
		           loaded.value.material->Parameters().alphaMode == AlphaMode::Mask &&
		           !loaded.value.material->Parameters().sampler.linear &&
		           !loaded.value.material->Parameters().sampler.repeat && loaded.value.material->SamplerOverridden(),
		       "Legacy PBR parameters changed during in-memory migration");
		Expect(loaded.value.material->TextureResourceId(MaterialTextureSlot::BaseColor) == textureId &&
		           loaded.value.material->PropertyOverrides().find("_Metallic") !=
		               loaded.value.material->PropertyOverrides().end() &&
		           loaded.value.material->PropertyOverrides().find("_Roughness") !=
		               loaded.value.material->PropertyOverrides().end() &&
		           loaded.value.material->PropertyOverrides().find("_NormalScale") !=
		               loaded.value.material->PropertyOverrides().end() &&
		           loaded.value.material->PropertyOverrides().find("_OcclusionStrength") !=
		               loaded.value.material->PropertyOverrides().end() &&
		           loaded.value.material->PropertyOverrides().find("_Emissive") !=
		               loaded.value.material->PropertyOverrides().end(),
		       "Migration did not preserve stable texture identity and typed property overrides");
		auto convenienceCopy = loaded.value.material->Clone();
		Expect(static_cast<bool>(convenienceCopy->SetRoughness(0.55f)) &&
		           convenienceCopy->PropertyOverrides().at("_Roughness").floatValue == 0.55f,
		       "Built-in PBR convenience setter did not update its Shader property");
		auto loadedUnlit = LoadMaterialResource(filesystem, Path("prj:Unlit.ursamat"), loadTexture);
		Expect(loadedUnlit && loadedUnlit.value.material->IsShaderBacked() &&
		           loadedUnlit.value.material->ShaderId() == Shader::BuiltinUnlitShaderId() &&
		           loadedUnlit.value.material->Type() == MaterialType::Unlit &&
		           loadedUnlit.value.material->Parameters().baseColor.red == 0.7f,
		       "Legacy Unlit material did not migrate to the built-in Unlit Shader");

		const ResourceId schemaMaterialId = ResourceId::Generate();
		Json oldUnlitSchema = legacyUnlit;
		oldUnlitSchema["version"] = materialResourceFormatVersion;
		oldUnlitSchema["id"] = ToStdString(schemaMaterialId.ToString());
		oldUnlitSchema["shaderId"] = {{"guid", ToStdString(Shader::BuiltinUnlitShaderId().ToString())},
		                              {"localFileId", 0}};
		oldUnlitSchema["propertyOverrides"] = {{"_MainTex", {{"type", "Texture2D"}, {"textureDefault", "white"}}}};
		oldUnlitSchema["keywords"] = Json::array();
		Expect(static_cast<bool>(WriteJsonResource(filesystem, Path("prj:SchemaUnlit.ursamat"), oldUnlitSchema)) &&
		           static_cast<bool>(WriteAssetMetadata(filesystem, Path("prj:SchemaUnlit.ursamat"), schemaMaterialId)),
		       "Cannot write pre-upgrade Unlit property schema fixture");
		auto upgradedUnlit = LoadMaterialResource(filesystem, Path("prj:SchemaUnlit.ursamat"), loadTexture);
		Expect(upgradedUnlit &&
		           upgradedUnlit.value.material->PropertyOverrides().find("_BaseMap") !=
		               upgradedUnlit.value.material->PropertyOverrides().end() &&
		           upgradedUnlit.value.material->PropertyOverrides().find("_MainTex") ==
		               upgradedUnlit.value.material->PropertyOverrides().end(),
		       "Unlit schema upgrade did not rename _MainTex to _BaseMap in memory");
		const auto baseMapBeforeRepeat = upgradedUnlit.value.material->PropertyOverrides().at("_BaseMap");
		const auto overrideCountBeforeRepeat = upgradedUnlit.value.material->PropertyOverrides().size();
		Expect(static_cast<bool>(upgradedUnlit.value.material->UpgradeBuiltinPropertySchema()),
		       "Repeated built-in material schema upgrade was rejected");
		const auto baseMapAfterRepeat = upgradedUnlit.value.material->PropertyOverrides().at("_BaseMap");
		Expect(upgradedUnlit.value.material->PropertyOverrides().size() == overrideCountBeforeRepeat &&
		           baseMapAfterRepeat.type == baseMapBeforeRepeat.type &&
		           baseMapAfterRepeat.textureId == baseMapBeforeRepeat.textureId &&
		           baseMapAfterRepeat.textureLocalFileId == baseMapBeforeRepeat.textureLocalFileId &&
		           baseMapAfterRepeat.textureDefault == baseMapBeforeRepeat.textureDefault &&
		           baseMapAfterRepeat.texture == baseMapBeforeRepeat.texture,
		       "Repeated built-in material schema upgrade changed migrated property data");
		Expect(static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:SchemaUnlit.ursamat"), schemaMaterialId,
		                                              *upgradedUnlit.value.material, {})),
		       "Upgraded Unlit schema could not be explicitly saved");
		auto upgradedDocument = ReadJsonResource(filesystem, Path("prj:SchemaUnlit.ursamat"), ResourceKind::Material);
		Expect(upgradedDocument && upgradedDocument.value["shaderPropertyVersion"] == 1 &&
		           upgradedDocument.value["propertyOverrides"].contains("_BaseMap") &&
		           !upgradedDocument.value["propertyOverrides"].contains("_MainTex"),
		       "Explicit save did not persist the versioned Unlit property schema");

		Json conflictUnlit = oldUnlitSchema;
		const ResourceId conflictMaterialId = ResourceId::Generate();
		conflictUnlit["id"] = ToStdString(conflictMaterialId.ToString());
		conflictUnlit["propertyOverrides"]["_BaseMap"] = {{"type", "Texture2D"}, {"textureDefault", "black"}};
		Expect(static_cast<bool>(WriteJsonResource(filesystem, Path("prj:ConflictUnlit.ursamat"), conflictUnlit)) &&
		           static_cast<bool>(
		               WriteAssetMetadata(filesystem, Path("prj:ConflictUnlit.ursamat"), conflictMaterialId)),
		       "Cannot write conflicting Unlit schema fixture");
		Expect(!LoadMaterialResource(filesystem, Path("prj:ConflictUnlit.ursamat"), loadTexture),
		       "Conflicting _MainTex and _BaseMap values were silently overwritten");
		auto beforeSave = ReadJsonResource(filesystem, Path("prj:Material.ursamat"), ResourceKind::Material);
		Expect(beforeSave && beforeSave.value["version"] == resourceFormatVersion,
		       "Loading a legacy material rewrote it before explicit save");

		Expect(static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:Material.ursamat"), materialId,
		                                              *loaded.value.material,
		                                              [&](MaterialTexturePtr selected)
		                                              {
			                                              return selected == texture.value
			                                                         ? Result<ResourceId>::Success(textureId)
			                                                         : Result<ResourceId>::Failure("Unknown texture");
		                                              })),
		       "Migrated material could not be saved");
		auto saved = ReadJsonResource(filesystem, Path("prj:Material.ursamat"), ResourceKind::Material);
		Expect(saved && saved.value["version"] == materialResourceFormatVersion &&
		           saved.value["id"] == ToStdString(materialId.ToString()) &&
		           saved.value["shaderId"]["guid"] == ToStdString(Shader::BuiltinPbrShaderId().ToString()) &&
		           saved.value["shaderId"]["localFileId"] == 0,
		       "Explicit save did not write the new versioned Shader-backed material format");

		saved.value["propertyOverrides"]["_RemovedProperty"] = {{"type", "Int"}, {"int", 77}};
		Expect(static_cast<bool>(WriteJsonResource(filesystem, Path("prj:Material.ursamat"), saved.value)),
		       "Cannot write unknown-property fixture");
		auto reopened = LoadMaterialResource(filesystem, Path("prj:Material.ursamat"), loadTexture);
		Expect(reopened && reopened.value.material->IsShaderBacked() &&
		           reopened.value.material->ShaderId() == Shader::BuiltinPbrShaderId() &&
		           reopened.value.material->PropertyOverrides().find("_RemovedProperty") !=
		               reopened.value.material->PropertyOverrides().end(),
		       "New material format did not preserve its Shader identity and unknown property override");
		Expect(static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:Material.ursamat"), materialId,
		                                              *reopened.value.material,
		                                              [&](MaterialTexturePtr selected)
		                                              {
			                                              return selected == texture.value
			                                                         ? Result<ResourceId>::Success(textureId)
			                                                         : Result<ResourceId>::Failure("Unknown texture");
		                                              })),
		       "Reopened Shader-backed material could not be saved");
		auto preserved = ReadJsonResource(filesystem, Path("prj:Material.ursamat"), ResourceKind::Material);
		Expect(preserved && preserved.value["propertyOverrides"].contains("_RemovedProperty") &&
		           preserved.value["propertyOverrides"]["_RemovedProperty"]["int"] == 77,
		       "Saving a Shader-backed material discarded an unknown property override");
		auto bytesBeforeBlockedSave = filesystem.Read(Path("prj:Material.ursamat"));
		VirtualFileSystem readonlyFilesystem;
		Expect(static_cast<bool>(readonlyFilesystem.MountProvider("prj", provider, MountAccess::ReadOnly)),
		       "Cannot mount a read-only material VFS");
		auto blockedSave =
		    SaveMaterialResource(readonlyFilesystem, Path("prj:Material.ursamat"), materialId, *reopened.value.material,
		                         [&](MaterialTexturePtr selected)
		                         {
			                         return selected == texture.value ? Result<ResourceId>::Success(textureId)
			                                                          : Result<ResourceId>::Failure("Unknown texture");
		                         });
		auto bytesAfterBlockedSave = filesystem.Read(Path("prj:Material.ursamat"));
		Expect(!blockedSave && bytesBeforeBlockedSave && bytesAfterBlockedSave &&
		           bytesBeforeBlockedSave.value == bytesAfterBlockedSave.value,
		       "Read-only save failure modified the migrated material resource");

		std::cout << "Material migration tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
