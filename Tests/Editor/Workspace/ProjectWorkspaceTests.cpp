#include <Workspace/ProjectWorkspace.hpp>
#include <Assets/ExternalProjectImport.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <ResourceFixtures.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Asset(const char* relative)
{
	return Take(VirtualPath::Parse(String("prj:") + relative));
}

void WriteHeader(const std::filesystem::path& path, ResourceId id)
{
	std::ofstream out(path);
	out << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << ToStdString(id.ToString()) << "\"}";
	if (!out)
	{
		throw std::runtime_error("Cannot write test resource header");
	}
}

Vector<ResourceMount> engineMounts(const std::filesystem::path& root)
{
	Vector<ResourceMount> mounts;
	mounts.push_back(ResourceMount{engineScheme, root, MountAccess::ReadOnly});
	return mounts;
}
} // namespace

int main()
{
	const auto base =
	    std::filesystem::current_path() / ("editor_workspace_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		const auto project = base / "project";
		const auto assets = project / "Assets";
		const auto engine = base / "engine";
		std::filesystem::create_directories(assets);
		std::filesystem::create_directories(engine);
		const auto id = ResourceId::Generate();
		const auto original = assets / "original.ursamat";
		const auto renamed = assets / "renamed.ursamat";
		WriteHeader(original, id);
		done(WriteAssetMetadata(original, id));
		std::ofstream(assets / "Logo.png", std::ios::binary) << "image-source";
		auto workspace = Take(Editor::ProjectWorkspace::Open(project, engineMounts(engine)));
		Expect(workspace->ProjectRoot() == project && workspace->AssetsRoot() == assets,
		       "Project directory and assets root were not separated");
		Expect(workspace->Resources().size() == 1 && workspace->Resources()[0].id == id &&
		           workspace->Resources()[0].location.path.ToString() == "prj:original.ursamat",
		       "Project resource enumeration lost the stable ID or virtual path");
		Expect(static_cast<bool>(workspace->CheckWritable(Asset("new.ursatex"))),
		       "Project write path resolution failed");
		Expect(!VirtualPath::Parse("prj:../Engine/builtin.ursamat"), "Parent escape parsed as a virtual path");
		Expect(!workspace->CheckWritable(Take(VirtualPath::Parse("ursa:builtin.ursamat"))),
		       "Engine asset was accepted as writable");
		std::filesystem::create_directories(assets / "builtin");
		Expect(!Editor::ProjectWorkspace::Open(project, engineMounts(assets / "builtin")),
		       "Engine root inside the writable assets folder was accepted");
		Expect(!Editor::ProjectWorkspace::Open(engine, engineMounts(engine)),
		       "A project without an assets directory was accepted");

		Expect(workspace->Database() != nullptr, "Project workspace did not create an AssetDatabase");
		done(workspace->Database()->Move(Asset("original.ursamat"), Asset("renamed.ursamat")));
		done(workspace->Refresh());
		Expect(workspace->Resources().size() == 1 && workspace->Resources()[0].id == id &&
		           workspace->Resources()[0].location.path.ToString() == "prj:renamed.ursamat",
		       "Resource ID changed after rename and refresh");
		const auto assetsAfterMove = workspace->Assets();
		const auto renamedAsset =
		    std::find_if(assetsAfterMove.begin(), assetsAfterMove.end(), [](const Editor::AssetRecord& asset)
		                 { return asset.path.ToString() == "prj:renamed.ursamat"; });
		const auto imageAsset =
		    std::find_if(assetsAfterMove.begin(), assetsAfterMove.end(),
		                 [](const Editor::AssetRecord& asset) { return asset.path.ToString() == "prj:Logo.png"; });
		Expect(assetsAfterMove.size() == 2 && renamedAsset != assetsAfterMove.end() &&
		           renamedAsset->metadata.guid == id && imageAsset != assetsAfterMove.end(),
		       "AssetDatabase identity changed after moving the source and its sidecar");

		const auto duplicate = engine / "duplicate.ursamat";
		WriteHeader(duplicate, id);
		done(WriteAssetMetadata(duplicate, id));
		const auto failed = workspace->Refresh();
		Expect(!failed && failed.error.find("Duplicate asset GUID") != String::npos,
		       "Duplicate ID did not report an error");
		Expect(failed.error.find("prj:renamed.ursamat") != String::npos &&
		           failed.error.find("ursa:duplicate.ursamat") != String::npos,
		       "Duplicate ID diagnostic did not use virtual paths");
		Expect(failed.error.find(ToUrsaString(renamed.u8string())) == String::npos,
		       "Duplicate ID diagnostic leaked a host path");
		Expect(workspace->Resources().size() == 1 && workspace->Resources()[0].id == id,
		       "Failed refresh replaced the previous resource index");
		std::filesystem::remove(duplicate);
		std::filesystem::remove(duplicate.string() + ".meta");

		const auto outside = base / "outside";
		std::filesystem::create_directory(outside);
#ifdef _WIN32
		const std::string command =
		    "cmd /c mklink /J \"" + (assets / "linked").string() + "\" \"" + outside.string() + "\" > nul 2>&1";
		const bool linked = std::system(command.c_str()) == 0;
#else
		std::error_code symlinkError;
		std::filesystem::create_directory_symlink(outside, assets / "linked", symlinkError);
		const bool linked = !symlinkError;
#endif
		if (linked)
		{
			Expect(!workspace->CheckWritable(Asset("linked/escape.ursatex")),
			       "Link path escaped the project assets root");
			const auto escaped =
			    Editor::CopyExternalProjectFiles(assets, {ToUrsaString(original.u8string())}, "linked/new-folder");
			Expect(!escaped && !std::filesystem::exists(outside / "new-folder"),
			       "External import created a directory outside the Assets root through a link");
		}

		const auto external = base / "external";
		std::filesystem::create_directories(external);
		std::ofstream(external / "loose.png") << "external image";
		std::ofstream(outside / "secret.png") << "outside";
		if (linked)
		{
			const auto topLink =
			    Editor::CopyExternalProjectFiles(assets, {ToUrsaString((assets / "linked").u8string())}, "");
			Expect(!topLink, "External import accepted a top-level directory link");
#ifdef _WIN32
			const std::string nestedLinkCommand = "cmd /c mklink /J \"" + (external / "nested-link").string() +
			                                      "\" \"" + outside.string() + "\" > nul 2>&1";
			const bool nestedLinked = std::system(nestedLinkCommand.c_str()) == 0;
#else
			std::error_code nestedError;
			std::filesystem::create_directory_symlink(outside, external / "nested-link", nestedError);
			const bool nestedLinked = !nestedError;
#endif
			if (nestedLinked)
			{
				done(Editor::CopyExternalProjectFiles(assets, {ToUrsaString(external.u8string())}, ""));
				Expect(!std::filesystem::exists(assets / "external" / "nested-link"),
				       "External import followed a link nested inside a directory");
			}
			std::cout << "Nested link test: " << (nestedLinked ? "executed" : "unavailable") << '\n';
		}
		std::cout << "Top-level link test: " << (linked ? "executed" : "unavailable") << '\n';
		std::ofstream(assets / "loose.png.meta") << "existing metadata";
		const auto externalFile = ToUrsaString((external / "loose.png").u8string());
		done(Editor::CopyExternalProjectFiles(assets, {externalFile}, ""));
		Expect(std::filesystem::exists(assets / "loose (1).png") && !std::filesystem::exists(assets / "loose.png"),
		       "External import reused a name reserved by an existing sidecar");
		done(Editor::CopyExternalProjectFiles(assets, {externalFile}, ""));
		Expect(std::filesystem::exists(assets / "loose (2).png"), "External import retry overwrote a prior copy");
		bool copiedAnything = true;
		const auto invalid = Editor::CopyExternalProjectFiles(
		    assets, {externalFile, ToUrsaString((external / "missing.png").u8string())}, "batch", &copiedAnything);
		Expect(!invalid && !copiedAnything && !std::filesystem::exists(assets / "batch"),
		       "Invalid later source was not rejected before the batch created output");
#ifdef _WIN32
		const auto lockedFile = external / "locked.png";
		std::ofstream(lockedFile) << "locked image";
		const HANDLE locked =
		    CreateFileW(lockedFile.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Expect(locked != INVALID_HANDLE_VALUE, "Could not lock the external test file");
		const auto partial = Editor::CopyExternalProjectFiles(
		    assets, {externalFile, ToUrsaString(lockedFile.u8string())}, "partial", &copiedAnything);
		CloseHandle(locked);
		Expect(!partial && copiedAnything && partial.error.find("locked.png") != String::npos &&
		           std::filesystem::exists(assets / "partial" / "loose.png") &&
		           !std::filesystem::exists(assets / "partial" / "locked.png"),
		       "Mid-batch copy failure did not report the path and preserve the earlier copy");
		done(Editor::CopyExternalProjectFiles(assets, {externalFile, ToUrsaString(lockedFile.u8string())}, "partial"));
		Expect(std::filesystem::exists(assets / "partial" / "loose (1).png") &&
		           std::filesystem::exists(assets / "partial" / "locked.png"),
		       "Retry after a partial batch did not preserve existing files");
#endif
		const auto recursive = Editor::CopyExternalProjectFiles(assets, {ToUrsaString(base.u8string())}, "nested");
		Expect(!recursive && !std::filesystem::exists(assets / "nested"),
		       "Import accepted a source folder containing its destination");
		const auto sameFolder = Editor::CopyExternalProjectFiles(assets, {ToUrsaString(assets.u8string())}, "");
		Expect(!sameFolder, "Import accepted its destination as a source folder");
		std::filesystem::remove(assets / "loose.png.meta");
		done(workspace->Refresh());
		const auto materialId = ResourceId::Generate();
		MaterialInstance material(MaterialType::Unlit);
		done(material.SetBaseColor({0.2f, 0.3f, 0.4f, 1.f}));
		done(SaveMaterialResource(workspace->Index().Filesystem(), Asset("usable.ursamat"), materialId, material, {}));
		MeshData triangle;
		triangle.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
		triangle.indices = {0, 1, 2};
		auto mesh = Take(Mesh::Create(std::move(triangle)));
		const auto meshId = ResourceId::Generate();
		done(SaveMeshResource(workspace->Index().Filesystem(), Asset("binary.ursamesh"), meshId, *mesh, true));
		const auto textureId = ResourceId::Generate();
		auto texture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 0, 0, 255}));
		done(SaveTextureResource(workspace->Index().Filesystem(), Asset("texture.ursatex"), textureId, *texture));
		std::ofstream(assets / "copy.ursashader")
		    << "Shader \"Tests/Copy\" { ID \"c6f1e600-b5c6-4c56-a5e1-5bba91570141\" Version 1 "
		       "Properties { _Tint(\"Tint\", Color)=(1,1,1,1) } SubShader { "
		       "Pass { Name \"Forward\" HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\nENDHLSL } } }";
		done(WriteAssetMetadata(assets / "copy.ursashader", ResourceId::Generate()));
		auto library = Take(Shader::ShaderSourceLibrary::Load(workspace->Index().Filesystem(), "prj"));
		const auto* source = library.Find("Tests/Copy");
		Expect(source != nullptr, "Cannot find Shader source fixture");
		auto dependencies = Take(library.ResolveDependencies(workspace->Index().Filesystem(), source->source.name));
		Shader::ShaderPassCompilation pass;
		pass.stages = {{Shader::ProgramStage::Vertex,
		                "VSMain",
		                {0x03, 0x02, 0x23, 0x07},
		                "{\"entryPoints\":[{\"name\":\"VSMain\",\"mode\":\"vert\"}]}",
		                "DXC test",
		                {}},
		               {Shader::ProgramStage::Fragment,
		                "PSMain",
		                {0x03, 0x02, 0x23, 0x07},
		                "{\"entryPoints\":[{\"name\":\"PSMain\",\"mode\":\"frag\"}]}",
		                "DXC test",
		                {}}};
		auto shader = Take(Shader::BuildShaderAsset(*source, library, dependencies, "fixture", "fixture", {pass}));
		std::filesystem::remove(assets / "copy.ursashader");
		std::filesystem::remove(assets / "copy.ursashader.meta");
		done(Shader::SaveShaderAssetResource(workspace->Index().Filesystem(), Asset("compiled.ursashaderasset"),
		                                     shader));
		done(workspace->Refresh());
		done(workspace->Database()->Copy(Asset("compiled.ursashaderasset"), Asset("compiled-copy.ursashaderasset")));
		const auto copiedShaderId = Take(workspace->Database()->Metadata(Asset("compiled-copy.ursashaderasset"))).guid;
		const auto copiedShader = Take(
		    Shader::LoadShaderAssetResource(workspace->Index().Filesystem(), Asset("compiled-copy.ursashaderasset")));
		Expect(copiedShaderId != shader.id && copiedShader.id == copiedShaderId &&
		           copiedShader.subShaders[0].passes[0].identity != shader.subShaders[0].passes[0].identity &&
		           copiedShader.subShaders[0].passes[0].variants[0].layoutIdentity ==
		               shader.subShaders[0].passes[0].variants[0].layoutIdentity &&
		           copiedShader.subShaders[0].passes[0].variants[0].stages[0].spirv ==
		               shader.subShaders[0].passes[0].variants[0].stages[0].spirv,
		       "Compiled Shader copy did not rebuild pass identity and preserve the bytecode and layout");
		done(workspace->Database()->CreateFolder(Asset("shader-folder")));
		done(workspace->Database()->Copy(Asset("compiled.ursashaderasset"),
		                                 Asset("shader-folder/item.ursashaderasset")));
		done(workspace->Database()->Copy(Asset("shader-folder"), Asset("shader-folder-copy")));
		const auto folderShaderId =
		    Take(workspace->Database()->Metadata(Asset("shader-folder-copy/item.ursashaderasset"))).guid;
		const auto folderShader = Take(Shader::LoadShaderAssetResource(
		    workspace->Index().Filesystem(), Asset("shader-folder-copy/item.ursashaderasset")));
		Expect(folderShader.id == folderShaderId && folderShaderId != shader.id &&
		           folderShader.subShaders[0].passes[0].identity != shader.subShaders[0].passes[0].identity,
		       "Folder copy did not rebuild compiled Shader identity");
		auto originalShaderBytes = Take(workspace->Index().Filesystem().Read(Asset("compiled.ursashaderasset")));
		auto damagedShaderBytes = originalShaderBytes;
		damagedShaderBytes.back() ^= 1;
		done(workspace->Index().Filesystem().WriteAtomic(Asset("compiled.ursashaderasset"), damagedShaderBytes));
		const auto damagedCopy =
		    workspace->Database()->Copy(Asset("compiled.ursashaderasset"), Asset("damaged-copy.ursashaderasset"));
		Expect(!damagedCopy && damagedCopy.error.find("checksum") != String::npos &&
		           !std::filesystem::exists(assets / "damaged-copy.ursashaderasset") &&
		           !std::filesystem::exists(assets / "damaged-copy.ursashaderasset.meta") &&
		           Take(workspace->Index().Filesystem().Read(Asset("compiled.ursashaderasset"))) == damagedShaderBytes,
		       "Damaged Shader copy did not fail without changing source or leaving a partial copy");
		done(workspace->Index().Filesystem().WriteAtomic(Asset("compiled.ursashaderasset"), originalShaderBytes));
		done(workspace->Database()->Copy(Asset("usable.ursamat"), Asset("usable-copy.ursamat")));
		done(workspace->Database()->Copy(Asset("binary.ursamesh"), Asset("binary-copy.ursamesh")));
		done(workspace->Database()->Copy(Asset("texture.ursatex"), Asset("texture-copy.ursatex")));
		done(workspace->Refresh());
		const auto materialCopyId = Take(workspace->Database()->Metadata(Asset("usable-copy.ursamat"))).guid;
		const auto meshCopyId = Take(workspace->Database()->Metadata(Asset("binary-copy.ursamesh"))).guid;
		const auto textureCopyId = Take(workspace->Database()->Metadata(Asset("texture-copy.ursatex"))).guid;
		auto resources = Take(ResourceSession::Open(workspace->Index().Filesystem()));
		const auto loadedMaterial = Take(resources->LoadMaterial(materialCopyId));
		const auto loadedMesh = Take(resources->LoadMesh(meshCopyId, 0));
		const auto loadedTexture = Take(resources->LoadTexture(textureCopyId));
		Expect(materialCopyId != materialId && loadedMaterial->Parameters().baseColor.red == 0.2f &&
		           meshCopyId != meshId && loadedMesh->Snapshot()->data.positions.size() == 3 &&
		           textureCopyId != textureId && loadedTexture->GetWidth() == 1,
		       "Copied native resources could not be loaded with their new identities");
		done(workspace->Database()->CreateFolder(Asset("reference-source")));
		const auto referenceTextureId = ResourceId::Generate();
		const auto referenceMaterialId = ResourceId::Generate();
		const auto referencePrefabId = ResourceId::Generate();
		done(SaveTextureResource(workspace->Index().Filesystem(), Asset("reference-source/Tex.ursatex"),
		                         referenceTextureId, *texture));
		MaterialInstance referenceMaterial(MaterialType::Unlit);
		done(referenceMaterial.SetTexture(MaterialTextureSlot::BaseColor, texture));
		done(SaveMaterialResource(workspace->Index().Filesystem(), Asset("reference-source/Mat.ursamat"),
		                          referenceMaterialId, referenceMaterial, [referenceTextureId](MaterialTexturePtr)
		                          { return Result<ResourceId>::Success(referenceTextureId); }));
		done(workspace->Refresh());
		auto sourceResources = Take(ResourceSession::Open(workspace->Index().Filesystem()));
		auto sourceMaterial = Take(sourceResources->LoadMaterial(referenceMaterialId));
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		Scene prefabScene;
		auto prefabRoot = Take(prefabScene.CreateGameObject("Reference"));
		auto* renderer = Take(prefabRoot->AddComponent<MeshRendererComponent>());
		done(renderer->SetSharedMaterial(sourceMaterial));
		done(SavePrefabResource(workspace->Index().Filesystem(), Asset("reference-source/Object.ursaprefab"),
		                        referencePrefabId, prefabScene, prefabRoot, registry));
		done(workspace->Refresh());
		done(workspace->Database()->Copy(Asset("reference-source"), Asset("reference-copy")));
		done(workspace->Refresh());
		const auto copiedReferenceTextureId =
		    Take(workspace->Database()->Metadata(Asset("reference-copy/Tex.ursatex"))).guid;
		const auto copiedReferenceMaterialId =
		    Take(workspace->Database()->Metadata(Asset("reference-copy/Mat.ursamat"))).guid;
		const auto copiedReferencePrefabId =
		    Take(workspace->Database()->Metadata(Asset("reference-copy/Object.ursaprefab"))).guid;
		auto copiedResources = Take(ResourceSession::Open(workspace->Index().Filesystem()));
		auto copiedReferenceMaterial = Take(copiedResources->LoadMaterial(copiedReferenceMaterialId));
		ComponentLoadContext copiedContext(*copiedResources);
		auto copiedPrefab = Take(LoadPrefabResource(
		    workspace->Index().Filesystem(), Asset("reference-copy/Object.ursaprefab"), registry, copiedContext));
		auto* copiedRenderer = copiedPrefab.scene->RootGameObjects()[0]->FindComponent<MeshRendererComponent>();
		Expect(copiedReferenceTextureId != referenceTextureId && copiedReferenceMaterialId != referenceMaterialId &&
		           copiedReferencePrefabId != referencePrefabId && copiedPrefab.id == copiedReferencePrefabId &&
		           copiedReferenceMaterial->TextureResourceId(MaterialTextureSlot::BaseColor) == referenceTextureId &&
		           copiedRenderer && !copiedRenderer->SharedMaterials().empty() &&
		           copiedRenderer->SharedMaterials()[0] &&
		           copiedRenderer->SharedMaterials()[0]->AssetIdentity().guid == referenceMaterialId,
		       "Folder copies did not preserve references to their original assets");
		const auto duplicateDir = external / "duplicate-folder";
		std::filesystem::create_directory(duplicateDir);
		WriteHeader(duplicateDir / "duplicate.ursamat", id);
		done(WriteAssetMetadata(duplicateDir / "duplicate.ursamat", id));
		std::filesystem::copy_file(assets / "usable.ursamat", duplicateDir / "real-material.ursamat");
		done(WriteAssetMetadata(duplicateDir / "real-material.ursamat", materialId));
		std::filesystem::copy_file(assets / "binary.ursamesh", duplicateDir / "real-mesh.ursamesh");
		done(WriteAssetMetadata(duplicateDir / "real-mesh.ursamesh", meshId));
		std::filesystem::copy_file(assets / "compiled.ursashaderasset", duplicateDir / "real-shader.ursashaderasset");
		done(WriteAssetMetadata(duplicateDir / "real-shader.ursashaderasset", shader.id));
		done(Editor::CopyExternalProjectFiles(assets, {ToUrsaString(duplicateDir.u8string())}, ""));
		done(workspace->Refresh());
		const auto copiedGuid = Take(workspace->Database()->Metadata(Asset("duplicate-folder/duplicate.ursamat"))).guid;
		Expect(copiedGuid != id && Take(workspace->Database()->Metadata(Asset("renamed.ursamat"))).guid == id &&
		           Take(workspace->Database()->SourcePath(id)).ToString() == "prj:renamed.ursamat",
		       "External duplicate GUID repair changed the existing asset identity");
		Expect(
		    Take(ReadResourceHeader(workspace->Index().Filesystem(), Asset("duplicate-folder/duplicate.ursamat"))).id ==
		        copiedGuid,
		    "External duplicate resource header does not match its repaired metadata");
		const auto externalMaterialId =
		    Take(workspace->Database()->Metadata(Asset("duplicate-folder/real-material.ursamat"))).guid;
		const auto externalMeshId =
		    Take(workspace->Database()->Metadata(Asset("duplicate-folder/real-mesh.ursamesh"))).guid;
		const auto externalShaderId =
		    Take(workspace->Database()->Metadata(Asset("duplicate-folder/real-shader.ursashaderasset"))).guid;
		auto externalResources = Take(ResourceSession::Open(workspace->Index().Filesystem()));
		Expect(externalMaterialId != materialId &&
		           Take(externalResources->LoadMaterial(externalMaterialId))->Parameters().baseColor.red == 0.2f &&
		           externalMeshId != meshId &&
		           Take(externalResources->LoadMesh(externalMeshId, 0))->Snapshot()->data.positions.size() == 3 &&
		           externalShaderId != shader.id &&
		           Take(externalResources->LoadShader(externalShaderId))->id == externalShaderId &&
		           Take(workspace->Database()->Metadata(Asset("compiled.ursashaderasset"))).guid == shader.id &&
		           Take(workspace->Database()->Metadata(Asset("usable.ursamat"))).guid == materialId,
		       "External duplicate material did not preserve the original and load under its repaired GUID");
		done(workspace->Database()->Copy(Asset("renamed.ursamat"), Asset("copied.ursamat")));
		const auto internalCopyGuid = Take(workspace->Database()->Metadata(Asset("copied.ursamat"))).guid;
		Expect(internalCopyGuid != id &&
		           Take(ReadResourceHeader(workspace->Index().Filesystem(), Asset("copied.ursamat"))).id ==
		               internalCopyGuid,
		       "Internal native asset copy has inconsistent file and metadata identities");
		done(workspace->Refresh());
		Expect(Take(workspace->Database()->Metadata(Asset("duplicate-folder/duplicate.ursamat"))).guid == copiedGuid,
		       "A repeated refresh changed the repaired external asset GUID");
		std::filesystem::remove_all(assets / "duplicate-folder");
		std::filesystem::remove(assets / "duplicate-folder.meta");
		done(workspace->Refresh());
		std::filesystem::remove_all(base);
		std::cout << "Editor project workspace test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(base);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
