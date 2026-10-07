#include <Material/Material.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
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

		auto session = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(session), "Cannot open migration resource session");
		auto loaded = session.value->LoadMaterial(materialId);
		Expect(static_cast<bool>(loaded), "Legacy material did not load");
		Expect(loaded.value->IsShaderBacked() && loaded.value->ShaderId() == Shader::BuiltinPbrShaderId() &&
		           loaded.value->Type() == MaterialType::PbrMetallicRoughness,
		       "Legacy PBR material did not map to the built-in PBR Shader identity");
		Expect(loaded.value->Parameters().metallic == 0.75f && loaded.value->Parameters().roughness == 0.3f &&
		           loaded.value->Parameters().alphaMode == AlphaMode::Mask,
		       "Legacy PBR parameters changed during in-memory migration");
		Expect(loaded.value->TextureResourceId(MaterialTextureSlot::BaseColor) == textureId &&
		           loaded.value->PropertyOverrides().find("_Metallic") != loaded.value->PropertyOverrides().end(),
		       "Migration did not preserve stable texture identity and typed property overrides");
		auto convenienceCopy = loaded.value->Clone();
		Expect(static_cast<bool>(convenienceCopy->SetRoughness(0.55f)) &&
		           convenienceCopy->PropertyOverrides().at("_Roughness").floatValue == 0.55f,
		       "Built-in PBR convenience setter did not update its Shader property");
		auto loadedUnlit = session.value->LoadMaterial(unlitId);
		Expect(loadedUnlit && loadedUnlit.value->IsShaderBacked() &&
		           loadedUnlit.value->ShaderId() == Shader::BuiltinUnlitShaderId() &&
		           loadedUnlit.value->Type() == MaterialType::Unlit &&
		           loadedUnlit.value->Parameters().baseColor.red == 0.7f,
		       "Legacy Unlit material did not migrate to the built-in Unlit Shader");
		auto beforeSave = ReadJsonResource(filesystem, Path("prj:Material.ursamat"), ResourceKind::Material);
		Expect(beforeSave && beforeSave.value["version"] == resourceFormatVersion,
		       "Loading a legacy material rewrote it before explicit save");

		Expect(
		    static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:Material.ursamat"), materialId, *loaded.value,
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
		           saved.value["shaderId"] == ToStdString(Shader::BuiltinPbrShaderId().ToString()),
		       "Explicit save did not write the new versioned Shader-backed material format");

		saved.value["propertyOverrides"]["_RemovedProperty"] = {{"type", "Int"}, {"int", 77}};
		Expect(static_cast<bool>(WriteJsonResource(filesystem, Path("prj:Material.ursamat"), saved.value)),
		       "Cannot write unknown-property fixture");
		auto reopenedSession = ResourceSession::Open(filesystem);
		Expect(static_cast<bool>(reopenedSession), "Cannot reopen migrated material session");
		auto reopened = reopenedSession.value->LoadMaterial(materialId);
		Expect(reopened && reopened.value->IsShaderBacked() &&
		           reopened.value->ShaderId() == Shader::BuiltinPbrShaderId() &&
		           reopened.value->PropertyOverrides().find("_RemovedProperty") !=
		               reopened.value->PropertyOverrides().end(),
		       "New material format did not preserve its Shader identity and unknown property override");
		Expect(static_cast<bool>(SaveMaterialResource(filesystem, Path("prj:Material.ursamat"), materialId,
		                                              *reopened.value,
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
		    SaveMaterialResource(readonlyFilesystem, Path("prj:Material.ursamat"), materialId, *reopened.value,
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
