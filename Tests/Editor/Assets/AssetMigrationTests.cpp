#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetProjectMigrator.hpp>
#include <Core/StringInterop.hpp>
#include <Material/Material.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/TextureResource.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
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
	if (!parsed)
	{
		throw std::runtime_error(ToStdString(parsed.error));
	}
	return std::move(parsed.value);
}

Vector<UInt8> Bytes(const char* value)
{
	Vector<UInt8> bytes;
	while (*value != '\0')
	{
		bytes.push_back(static_cast<UInt8>(*value++));
	}
	return bytes;
}

void WriteHostFile(const std::filesystem::path& path, const std::string& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << text;
	if (!output)
	{
		throw std::runtime_error("Cannot create migration fixture");
	}
}

void WriteMetadataFile(const std::filesystem::path& path, ResourceId guid)
{
	WriteHostFile(path, Json{{"version", assetMetadataVersion},
	                         {"guid", ToStdString(guid.ToString())},
	                         {"folder", false},
	                         {"importer", ""},
	                         {"importerVersion", 0u},
	                         {"settings", Json::object()}}
	                        .dump(2));
}

AssetMetadata Metadata(AssetDatabase& database, const char* path)
{
	auto result = database.Metadata(Path(path));
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void TestMigrationAndRestore(const std::filesystem::path& root)
{
	const auto assetsRoot = root / "Assets";
	const auto libraryRoot = root / "Library";
	std::filesystem::create_directories(assetsRoot);
	std::filesystem::create_directories(libraryRoot);

	const ResourceId shaderId = ResourceId::Parse("10000000-0000-4000-8000-000000000001").value;
	const ResourceId textureId = ResourceId::Parse("20000000-0000-4000-8000-000000000002").value;
	const ResourceId materialId = ResourceId::Parse("30000000-0000-4000-8000-000000000003").value;
	const ResourceId prefabId = ResourceId::Parse("40000000-0000-4000-8000-000000000004").value;
	const ResourceId sceneId = ResourceId::Parse("50000000-0000-4000-8000-000000000005").value;
	const String objectId = "60000000-0000-4000-8000-000000000006";
	const String componentId = "70000000-0000-4000-8000-000000000007";
	const String instanceId = "80000000-0000-4000-8000-000000000008";

	WriteHostFile(assetsRoot / "Shaders" / "Legacy.ursashader",
	              "Shader \"Legacy\"\n{\n\tID \"" + ToStdString(shaderId.ToString()) + "\"\n}\n");
	WriteHostFile(assetsRoot / "Textures" / "Logo.png", "raw-image-source");
	WriteHostFile(assetsRoot / "Prefabs" / "Legacy.ursaprefab", Json{{"format", "UrsaPrefab"},
	                                                                 {"version", resourceFormatVersion},
	                                                                 {"id", ToStdString(prefabId.ToString())},
	                                                                 {"gameObjects", Json::array()}}
	                                                                .dump(2));
	const Json scene = {{"format", "UrsaScene"},
	                    {"version", resourceFormatVersion},
	                    {"id", ToStdString(sceneId.ToString())},
	                    {"gameObjects",
	                     {{{"id", ToStdString(objectId)},
	                       {"components",
	                        {{{"id", ToStdString(componentId)},
	                          {"type", "ursa.BoxRender"},
	                          {"version", 1},
	                          {"data", {{"material", ToStdString(materialId.ToString())}}}}}}}}},
	                    {"instances",
	                     {{{"id", ToStdString(instanceId)},
	                       {"prefab", ToStdString(prefabId.ToString())},
	                       {"parentId", nullptr},
	                       {"overrides", Json::object()}}}}};
	WriteHostFile(assetsRoot / "Scenes" / "Legacy.ursascene", scene.dump(2));

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount project migration fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount migration backup Library");
	auto texture = MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {12, 34, 56, 255});
	Expect(texture && static_cast<bool>(SaveTextureResource(filesystem, Path("prj:Textures/Legacy.ursatex"), textureId,
	                                                        *texture.value)),
	       "Cannot create embedded-ID texture fixture");
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Expect(static_cast<bool>(material->SetTexture(MaterialTextureSlot::BaseColor, texture.value)),
	       "Cannot set the material migration texture");
	Expect(static_cast<bool>(material->SetTextureResourceId(MaterialTextureSlot::BaseColor, textureId)),
	       "Cannot set the material migration texture GUID");
	Expect(static_cast<bool>(material->SetShader(shaderId)), "Cannot set the material migration Shader");
	auto savedMaterial =
	    SaveMaterialResource(filesystem, Path("prj:Materials/Legacy.ursamat"), materialId, *material,
	                         [textureId](const MaterialTexturePtr&) { return Result<ResourceId>::Success(textureId); });
	Expect(static_cast<bool>(savedMaterial), "Cannot serialize the material migration fixture");
	auto legacyMaterialText = filesystem.ReadText(Path("prj:Materials/Legacy.ursamat"));
	Expect(static_cast<bool>(legacyMaterialText), "Cannot read the material migration fixture");
	Json legacyMaterial = Json::parse(ToStdString(legacyMaterialText.value));
	legacyMaterial["shaderId"] = legacyMaterial["shaderId"]["guid"];
	legacyMaterial["textures"]["baseColor"] = legacyMaterial["textures"]["baseColor"]["guid"];
	legacyMaterial["propertyOverrides"]["_BaseMap"] = {{"type", "Texture2D"},
	                                                   {"textureId", ToStdString(textureId.ToString())}};
	Expect(static_cast<bool>(
	           filesystem.WriteAtomic(Path("prj:Materials/Legacy.ursamat"), Bytes(legacyMaterial.dump(2).c_str()))),
	       "Cannot downgrade references in the legacy material fixture");
	const ResourceId oldShaderGuid = ResourceId::Generate();
	const ResourceId oldTextureGuid = ResourceId::Generate();
	const ResourceId oldMaterialGuid = ResourceId::Generate();
	const ResourceId oldPrefabGuid = ResourceId::Generate();
	const ResourceId oldSceneGuid = ResourceId::Generate();
	WriteMetadataFile(assetsRoot / "Shaders" / "Legacy.ursashader.meta", oldShaderGuid);
	WriteMetadataFile(assetsRoot / "Textures" / "Legacy.ursatex.meta", oldTextureGuid);
	WriteMetadataFile(assetsRoot / "Materials" / "Legacy.ursamat.meta", oldMaterialGuid);
	WriteMetadataFile(assetsRoot / "Prefabs" / "Legacy.ursaprefab.meta", oldPrefabGuid);
	WriteMetadataFile(assetsRoot / "Scenes" / "Legacy.ursascene.meta", oldSceneGuid);

	auto opened = AssetDatabase::Open(filesystem);
	Expect(opened && static_cast<bool>(opened.value->Refresh()), "Cannot initialize migration AssetDatabase");
	const ResourceId generatedImageGuid = Metadata(*opened.value, "prj:Textures/Logo.png").guid;
	const ResourceId originalSceneGuid = Metadata(*opened.value, "prj:Scenes/Legacy.ursascene").guid;
	auto migrated = AssetProjectMigrator::Migrate(*opened.value);
	Expect(static_cast<bool>(migrated), "Legacy asset migration failed");
	Expect(migrated.value.seededGuids == 5 && migrated.value.convertedReferences == 5,
	       "Migration did not report the expected identities and external references");
	Expect(Metadata(*opened.value, "prj:Shaders/Legacy.ursashader").guid == shaderId &&
	           Metadata(*opened.value, "prj:Textures/Legacy.ursatex").guid == textureId &&
	           Metadata(*opened.value, "prj:Materials/Legacy.ursamat").guid == materialId &&
	           Metadata(*opened.value, "prj:Prefabs/Legacy.ursaprefab").guid == prefabId &&
	           Metadata(*opened.value, "prj:Scenes/Legacy.ursascene").guid == sceneId,
	       "Metadata GUIDs were not seeded from embedded resource IDs");
	Expect(Metadata(*opened.value, "prj:Textures/Logo.png").guid == generatedImageGuid,
	       "Migration changed the generated GUID of a source image");

	auto migratedSceneText = filesystem.ReadText(Path("prj:Scenes/Legacy.ursascene"));
	auto migratedMaterialText = filesystem.ReadText(Path("prj:Materials/Legacy.ursamat"));
	Expect(migratedSceneText && migratedMaterialText, "Cannot read migrated JSON assets");
	const Json migratedScene = Json::parse(ToStdString(migratedSceneText.value));
	const Json migratedMaterial = Json::parse(ToStdString(migratedMaterialText.value));
	auto materialReference =
	    AssetReferenceFromJson(migratedScene["gameObjects"][0]["components"][0]["data"]["material"], "scene.material");
	auto prefabReference = AssetReferenceFromJson(migratedScene["instances"][0]["prefab"], "scene.prefab");
	auto shaderReference = AssetReferenceFromJson(migratedMaterial["shaderId"], "material.shaderId");
	auto textureReference = AssetReferenceFromJson(migratedMaterial["textures"]["baseColor"], "material.texture");
	auto overrideReference = AssetReferenceFromJson(migratedMaterial["propertyOverrides"]["_BaseMap"]["textureId"],
	                                                "material.override.texture");
	Expect(materialReference && materialReference.value.guid == materialId &&
	           materialReference.value.localFileId == 0 && prefabReference && prefabReference.value.guid == prefabId &&
	           shaderReference && shaderReference.value.guid == shaderId && textureReference &&
	           textureReference.value.guid == textureId && overrideReference &&
	           overrideReference.value.guid == textureId,
	       "Migrated external references are not GUID/local-file-ID values");
	auto loadedMaterial = LoadMaterialResource(
	    filesystem, Path("prj:Materials/Legacy.ursamat"),
	    [textureId, textureValue = texture.value](ResourceId reference) -> Result<MaterialTexturePtr>
	    {
		    return reference == textureId ? Result<MaterialTexturePtr>::Success(textureValue)
		                                  : Result<MaterialTexturePtr>::Failure("Unexpected texture ID");
	    });
	Expect(loadedMaterial && loadedMaterial.value.id == materialId &&
	           loadedMaterial.value.material->ShaderId() == shaderId &&
	           loadedMaterial.value.material->TextureResourceId(MaterialTextureSlot::BaseColor) == textureId,
	       "Material loader did not resolve the migrated GUID/local-file-ID references");
	Expect(migratedScene["gameObjects"][0]["id"] == ToStdString(objectId) &&
	           migratedScene["gameObjects"][0]["components"][0]["id"] == ToStdString(componentId) &&
	           migratedScene["instances"][0]["id"] == ToStdString(instanceId),
	       "Migration modified object, component, or instance-local IDs");

	auto backupManifest = VirtualPath::Parse(migrated.value.backupRoot + "/Manifest.json");
	Expect(backupManifest && filesystem.Exists(backupManifest.value).value,
	       "Migration did not preserve a recovery manifest");
	Expect(static_cast<bool>(AssetProjectMigrator::Restore(filesystem, migrated.value.backupRoot)),
	       "Migration backup could not restore the original project files");
	Expect(static_cast<bool>(opened.value->Refresh()), "Restored project failed to refresh its asset index");
	Expect(Metadata(*opened.value, "prj:Scenes/Legacy.ursascene").guid == originalSceneGuid &&
	           originalSceneGuid == oldSceneGuid,
	       "Migration restore did not recover the previous sidecar GUID");
	auto restoredSceneText = filesystem.ReadText(Path("prj:Scenes/Legacy.ursascene"));
	Expect(restoredSceneText && Json::parse(ToStdString(restoredSceneText.value))["instances"][0]["prefab"] ==
	                                ToStdString(prefabId.ToString()),
	       "Migration restore did not recover the original serialized reference");
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("UrsaAssetMigrationTests-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root);
		TestMigrationAndRestore(root);
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
