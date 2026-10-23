#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetImportPipeline.hpp>
#include <Assets/ModelImporter.hpp>
#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/SceneDraft.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Core/StringInterop.hpp>
#include <Math/Camera.hpp>
#include <RenderingFixtures.hpp>
#include <Serialization/ResourceId.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SceneResource.hpp>
#include <Rhi/RHI.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/MeshResource.hpp>
#include <Scene/Scene.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/PakArchive.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

MeshRendererComponent* FindMeshRenderer(const GameObject::Ptr& object)
{
	if (auto* renderer = object->FindComponent<MeshRendererComponent>())
	{
		return renderer;
	}
	for (const GameObject::Ptr& child : object->Children())
	{
		if (auto* renderer = FindMeshRenderer(child))
		{
			return renderer;
		}
	}
	return nullptr;
}

SkinnedMeshRendererComponent* FindSkinnedMeshRenderer(const GameObject::Ptr& object)
{
	if (auto* renderer = object->FindComponent<SkinnedMeshRendererComponent>())
	{
		return renderer;
	}
	for (const GameObject::Ptr& child : object->Children())
	{
		if (auto* renderer = FindSkinnedMeshRenderer(child))
		{
			return renderer;
		}
	}
	return nullptr;
}

AnimatorComponent* FindAnimator(const GameObject::Ptr& object)
{
	if (auto* animator = object->FindComponent<AnimatorComponent>())
	{
		return animator;
	}
	for (const GameObject::Ptr& child : object->Children())
	{
		if (auto* animator = FindAnimator(child))
		{
			return animator;
		}
	}
	return nullptr;
}

Size CountModelNodes(const GameObject::Ptr& object)
{
	Size count = 1;
	for (const GameObject::Ptr& child : object->Children())
	{
		count += CountModelNodes(child);
	}
	return count;
}

Size CountMeshRenderers(const GameObject::Ptr& object)
{
	Size count = object->FindComponents<MeshRendererComponent>().size();
	for (const GameObject::Ptr& child : object->Children())
	{
		count += CountMeshRenderers(child);
	}
	return count;
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

void WriteText(const std::filesystem::path& path, const char* text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << text;
	if (!output)
	{
		throw std::runtime_error("Cannot create the model importer fixture");
	}
}

Vector<UInt8> ReadBytes(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
	{
		throw std::runtime_error("Cannot read the model import fixture");
	}
	Vector<UInt8> bytes(static_cast<Size>(input.tellg()));
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!input)
	{
		throw std::runtime_error("Cannot read all bytes from the model import fixture");
	}
	return bytes;
}

bool ContainsPath(const Vector<VirtualPath>& paths, const char* expected)
{
	for (const VirtualPath& path : paths)
	{
		if (path.ToString() == expected)
		{
			return true;
		}
	}
	return false;
}

int RunProcess(const std::string& command)
{
#ifdef _WIN32
	std::wstring commandLine = std::filesystem::u8path(command).wstring();
	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESTDHANDLES;
	startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION processInfo{};
	if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
	                    &startupInfo, &processInfo))
	{
		return -1;
	}
	WaitForSingleObject(processInfo.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(processInfo.hProcess, &exitCode);
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	return static_cast<int>(exitCode);
#else
	return std::system(command.c_str());
#endif
}

std::filesystem::path BuildPackage(const std::filesystem::path& root, const std::filesystem::path& assetsRoot,
                                   const std::filesystem::path& libraryRoot, const char* packageName)
{
	const auto outputDirectory = root / packageName;
	const auto packagePath = outputDirectory / "Content.pak";
	std::filesystem::create_directories(outputDirectory);
	(void)libraryRoot;
	const std::string command = std::string("\"") + URSA_PAK_EXECUTABLE + "\" --engine \"" +
	                            URSA_DEMO_ENGINE_ASSET_PATH + "\" --project-root \"" +
	                            assetsRoot.parent_path().string() + "\" --configuration " + URSA_TEST_CONFIGURATION +
	                            " --output \"" + packagePath.string() + "\"";
	const int exitCode = RunProcess(command);
	Expect(exitCode == 0, ("UrsaPak failed (" + std::to_string(exitCode) + "): " + command).c_str());
	return packagePath;
}

nlohmann::json ReadArtifact(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto bytes = filesystem.Read(path);
	Expect(static_cast<bool>(bytes), "Cannot read the imported model artifact");
	if (PathFromUrsaString(path.relative).extension() == ".ursamesh")
	{
		Expect(bytes.value.size() >= 48, "Truncated Mesh artifact");
		return nlohmann::json::from_cbor(bytes.value.begin() + 48, bytes.value.end()).at("data");
	}
	if (PathFromUrsaString(path.relative).extension() == ".ursamat")
	{
		return nlohmann::json::parse(bytes.value.begin(), bytes.value.end());
	}
	return nlohmann::json::from_cbor(bytes.value.begin(), bytes.value.end());
}

void TestProvidedFbx(const std::filesystem::path& root)
{
	const auto assetsRoot = root / "Fbx" / "Assets";
	const auto libraryRoot = root / "Fbx" / "Library";
	std::filesystem::create_directories(assetsRoot / "Models");
	std::filesystem::create_directories(libraryRoot);
	std::filesystem::copy_file(std::filesystem::u8path(URSA_DEMO_MODEL_PATH), assetsRoot / "Models" / "Cube.fbx");

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount the copied FBX fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount the FBX importer Library");
	auto database = AssetDatabase::Open(filesystem);
	if (!database)
	{
		throw std::runtime_error("Cannot open the FBX AssetDatabase: " + ToStdString(database.error));
	}
	auto refreshed = database.value->Refresh();
	if (!refreshed)
	{
		throw std::runtime_error("Cannot refresh the FBX AssetDatabase: " + ToStdString(refreshed.error));
	}
	const VirtualPath source = Path("prj:Models/Cube.fbx");
	auto imported = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(static_cast<bool>(imported), imported ? "" : ToStdString(imported.error).c_str());
	const auto model = ReadArtifact(filesystem, imported.value.artifactPath);
	Expect(model.value("format", std::string{}) == "UrsaModel" && model.value("version", 0) == 2,
	       "FBX importer did not write the versioned UrsaModel document");
	Expect(model.contains("nodes") && model["nodes"].is_array() && !model["nodes"].empty() &&
	           model.contains("meshLocalFileIds") && model["meshLocalFileIds"].is_array() &&
	           !model["meshLocalFileIds"].empty(),
	       "Cube.fbx did not preserve its node hierarchy and meshes");
	for (const auto& node : model["nodes"])
	{
		Expect(node.contains("name") && node.contains("transform") && node["transform"].size() == 16 &&
		           node.contains("meshLocalFileIds"),
		       "An imported FBX node is missing its transform or mesh assignments");
	}
	auto cached = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(cached && cached.value.cacheHit && cached.value.subAssets.size() == imported.value.subAssets.size() &&
	           cached.value.diagnostics == imported.value.diagnostics,
	       "FBX import did not reuse its stable imported sub-assets");
	for (const ImportedArtifactLocation& subAsset : imported.value.subAssets)
	{
		if (subAsset.localFileId == imported.value.mainObjectLocalFileId)
		{
			continue;
		}
		auto resolved = database.value->Resolve(imported.value.guid, subAsset.localFileId);
		Expect(resolved && resolved.value == subAsset.path, "An FBX sub-asset local ID did not resolve");
	}
	ModelImportSettings changedSettings;
	changedSettings.scaleFactor = 0.5f;
	changedSettings.flipUvs = true;
	Expect(static_cast<bool>(ModelImporter::SetSettings(*database.value, source, changedSettings)),
	       "Cannot persist FBX model importer settings");
	auto restoredSettings = ModelImporter::Settings(*database.value, source);
	Expect(restoredSettings && restoredSettings.value.scaleFactor == changedSettings.scaleFactor &&
	           restoredSettings.value.flipUvs,
	       "FBX Inspector settings were not restored from metadata");
	auto reimported = ModelImporter::Import(*database.value, source, restoredSettings.value);
	Expect(reimported && !reimported.value.cacheHit && reimported.value.guid == imported.value.guid,
	       "Changing FBX importer settings did not reimport while preserving asset identity");
	const auto reimportedModel = ReadArtifact(filesystem, reimported.value.artifactPath);
	AssetDatabase* assetDatabase = database.value.get();
	Vector<ResourceMount> runtimeMounts;
	for (const VirtualMount& mount : filesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto engineFilesystem = Take(OpenEngineFileSystem());
	for (const VirtualMount& mount : engineFilesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto session = Take(ResourceSession::Open(runtimeMounts, [assetDatabase](ResourceId assetGuid, UInt64 localFileId)
	                                          { return assetDatabase->Resolve(assetGuid, localFileId); }));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto draft = Take(SceneDraft::Create(ResourceKind::Scene, registry, *session));
	auto placedCube = Take(draft->InstantiateModel(reimported.value.guid, reimportedModel));
	Expect(CountModelNodes(placedCube) == reimportedModel["nodes"].size() &&
	           CountMeshRenderers(placedCube) == reimportedModel["meshLocalFileIds"].size(),
	       "The supplied Cube.fbx did not instantiate its ordered nodes and meshes into a Scene");
	const VirtualPath placedScene = Path("prj:CubeScene.ursascene");
	Done(draft->Save(filesystem, placedScene, ResourceId::Generate()));
	auto reloadedDraft = Take(SceneDraft::Load(filesystem, placedScene, ResourceKind::Scene, registry, *session));
	GameObject::Ptr reloadedCube;
	for (const GameObject::Ptr& root : reloadedDraft->GetScene().RootGameObjects())
	{
		if (CountMeshRenderers(root) != 0)
		{
			reloadedCube = root;
			break;
		}
	}
	Expect(reloadedCube && CountModelNodes(reloadedCube) == reimportedModel["nodes"].size() &&
	           CountMeshRenderers(reloadedCube) == reimportedModel["meshLocalFileIds"].size(),
	       "The supplied Cube.fbx Scene did not preserve its hierarchy after save and reopen");
}

void TestObjMaterialDependencies(const std::filesystem::path& root)
{
	const auto assetsRoot = root / "Obj" / "Assets";
	const auto libraryRoot = root / "Obj" / "Library";
	const auto textureSource = std::filesystem::u8path(URSA_DEMO_TEXTURE_PATH);
	std::filesystem::create_directories(assetsRoot / "Models" / "Textures");
	std::filesystem::create_directories(libraryRoot);
	WriteText(assetsRoot / "Models" / "Triangle.obj", "mtllib Triangle.mtl\n"
	                                                  "o TriangleNode\n"
	                                                  "v 0 0 0\n"
	                                                  "v 1 0 0\n"
	                                                  "v 0 1 0\n"
	                                                  "vt 0 0\n"
	                                                  "vt 1 0\n"
	                                                  "vt 0 1\n"
	                                                  "vn 0 0 1\n"
	                                                  "usemtl Painted\n"
	                                                  "f 1/1/1 2/2/1 3/3/1\n");
	WriteText(assetsRoot / "Models" / "Triangle.mtl", "newmtl Painted\n"
	                                                  "Kd 0.8 0.5 0.2\n"
	                                                  "Ka 0.1 0.1 0.1\n"
	                                                  "Pr 0.35\n"
	                                                  "Pm 0.1\n"
	                                                  "map_Kd Textures/UrsaEngine.png\n");
	std::filesystem::copy_file(textureSource, assetsRoot / "Models" / "Textures" / "UrsaEngine.png");

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount the OBJ fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount the OBJ importer Library");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "Cannot initialize the OBJ AssetDatabase");
	const VirtualPath source = Path("prj:Models/Triangle.obj");
	auto imported = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(static_cast<bool>(imported), imported ? "" : ToStdString(imported.error).c_str());
	Expect(ContainsPath(imported.value.dependencies, "prj:Models/Triangle.mtl") &&
	           ContainsPath(imported.value.dependencies, "prj:Models/Textures/UrsaEngine.png"),
	       "OBJ material or image dependency was not tracked through VFS");
	const auto model = ReadArtifact(filesystem, imported.value.artifactPath);
	Expect(model["nodes"].size() >= 2 && model["meshLocalFileIds"].size() == 1 &&
	           model["materialLocalFileIds"].size() >= 1 && model.contains("materials"),
	       "OBJ hierarchy, mesh, or MTL material was not imported");
	UInt64 materialId = 0;
	nlohmann::json materialRecord;
	for (const auto& importedMaterial : model["materials"])
	{
		if (importedMaterial.value("name", std::string{}) == "Painted")
		{
			materialId = importedMaterial["localFileId"].get<UInt64>();
			materialRecord = importedMaterial;
			break;
		}
	}
	Expect(materialId != 0, "OBJ importer did not preserve the named MTL material");
	auto materialPath = database.value->Resolve(imported.value.guid, materialId);
	Expect(static_cast<bool>(materialPath), "Imported OBJ material did not resolve as a sub-asset");
	const auto material = ReadArtifact(filesystem, materialPath.value);
	auto textureMetadata = database.value->Metadata(Path("prj:Models/Textures/UrsaEngine.png"));
	Expect(static_cast<bool>(textureMetadata), "OBJ texture source metadata is missing");
	Expect(materialRecord["name"] == "Painted" && material.contains("shaderId") &&
	           material["parameters"]["baseColor"][0] > 0.7f && material["textures"].contains("baseColor") &&
	           material["textures"]["baseColor"]["guid"] == ToStdString(textureMetadata.value.guid.ToString()),
	       "OBJ material texture was not converted to a stable GUID reference");
	bool hasUnsupportedPropertyDiagnostic = false;
	for (const String& diagnostic : imported.value.diagnostics)
	{
		hasUnsupportedPropertyDiagnostic |= diagnostic.find("unsupported source property") != String::npos;
	}
	Expect(hasUnsupportedPropertyDiagnostic,
	       "Unsupported OBJ material properties were not reported as import diagnostics");
	const auto meshId = model["meshLocalFileIds"][0].get<UInt64>();
	auto meshPath = database.value->Resolve(imported.value.guid, meshId);
	Expect(static_cast<bool>(meshPath), "Imported OBJ mesh did not resolve as a sub-asset");
	const auto mesh = ReadArtifact(filesystem, meshPath.value);
	Expect(model["meshMaterialBindings"][std::to_string(meshId)][0] == materialId && mesh["vertexCount"] >= 3 &&
	           mesh["indices"].get_binary().size() == 12,
	       "OBJ mesh did not preserve its material assignment and triangle geometry");
	AssetDatabase* assetDatabase = database.value.get();
	Vector<ResourceMount> runtimeMounts;
	for (const VirtualMount& mount : filesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto engineFilesystem = Take(OpenEngineFileSystem());
	for (const VirtualMount& mount : engineFilesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto session = Take(ResourceSession::Open(runtimeMounts, [assetDatabase](ResourceId guid, UInt64 localFileId)
	                                          { return assetDatabase->Resolve(guid, localFileId); }));
	auto runtimeMesh = Take(session->LoadMesh(imported.value.guid, meshId));
	Expect(runtimeMesh->Snapshot()->data.positions.size() == 3 && runtimeMesh->Snapshot()->data.indices.size() == 3 &&
	           runtimeMesh->AssetIdentity().localFileId == meshId,
	       "ResourceSession did not load the imported mesh and its shader-backed material sub-asset");
	auto missingMesh = session->LoadMesh(imported.value.guid, std::numeric_limits<UInt64>::max());
	Expect(!missingMesh && !missingMesh.error.empty(), "Unresolved model mesh did not return a diagnostic error");
	Rhi::DeviceDesc deviceDescription;
#ifndef NDEBUG
	deviceDescription.validation = true;
#endif
	auto device = Take(Rhi::CreateDevice(deviceDescription));
	auto renderer = Take(Test::SceneRenderer::Create(device));
	auto color = Take(device->CreateTexture(
	    {64, 64, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
	Scene scene;
	auto cameraObject = Take(scene.CreateGameObject("Camera"));
	LocalTransform cameraTransform;
	cameraTransform.position = {0.0f, 0.0f, 4.0f};
	Done(scene.SetLocalTransform(cameraObject, cameraTransform));
	const ComponentId cameraComponent = Take(scene.AddComponent<CameraComponent>(cameraObject));
	Done(scene.SetActiveCamera(cameraComponent));
	auto meshObject = Take(scene.CreateGameObject("Triangle"));
	LocalTransform meshTransform;
	meshTransform.position = {0.15f, 0.0f, 0.0f};
	Done(scene.SetLocalTransform(meshObject, meshTransform));
	const ComponentId meshComponent = Take(scene.AddComponent<MeshRendererComponent>(meshObject));
	auto* meshFilter = Take(meshObject->AddComponent<MeshFilterComponent>());
	Done(meshFilter->SetSharedMesh(runtimeMesh));
	Done(static_cast<MeshRendererComponent*>(scene.GetComponent(meshComponent))
	         ->SetSharedMaterial(Take(session->LoadMaterial(imported.value.guid, materialId))));
	ComponentRegistry componentRegistry;
	Done(RegisterBuiltinComponents(componentRegistry));
	const VirtualPath scenePath = Path("prj:RuntimeMesh.ursascene");
	const ResourceId sceneGuid = ResourceId::Generate();
	Done(SaveSceneResource(filesystem, scenePath, sceneGuid, scene, componentRegistry));
	ComponentLoadContext loadContext(*session);
	loadContext.Provide(componentRegistry);
	auto loadedScene = Take(LoadSceneResource(filesystem, scenePath, componentRegistry, loadContext));
	const auto renderData = Take(loadedScene.scene->CollectRenderData(1.0f));
	Expect(renderData.objects.size() == 1 && renderData.objects[0].mesh == runtimeMesh &&
	           renderData.objects[0].material == Take(session->LoadMaterial(imported.value.guid, materialId)) &&
	           std::abs(renderData.objects[0].world.At(0, 3) - meshTransform.position.x) < 0.001f,
	       "Reloaded MeshRenderer lost its sub-asset, material, or GameObject world transform");
	Vector<Rendering::RenderObject> gpuDraws = renderData.objects;
	gpuDraws[0].material.reset();
	auto rendered = Take(renderer->DrawOffscreen(color, renderData.viewProjection, gpuDraws, renderData.lighting,
	                                             renderData.cameraPosition));
	Done(rendered->Wait());
	auto readback =
	    Take(device->CreateBuffer({64 * 64 * 4, Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(color, readback));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	Vector<UInt8> pixels(64 * 64 * 4);
	Done(readback->Read(0, pixels.data(), pixels.size()));
	UInt32 renderedPixels = 0;
	UInt8 maximumChannel = 0;
	for (Size offset = 0; offset < pixels.size(); offset += 4)
	{
		maximumChannel =
		    (std::max)(maximumChannel, (std::max)(pixels[offset], (std::max)(pixels[offset + 1], pixels[offset + 2])));
		if (pixels[offset] > 30 || pixels[offset + 1] > 30 || pixels[offset + 2] > 30)
		{
			++renderedPixels;
		}
	}
	Expect(renderedPixels > 10, ("Imported OBJ mesh produced no visible pixels in the offscreen renderer (count=" +
	                             std::to_string(renderedPixels) + ", max=" + std::to_string(maximumChannel) + ")")
	                                .c_str());
	renderer.reset();
	Done(device->WaitIdle());
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	const VirtualPath placedScenePath = Path("prj:PlacedModel.ursascene");
	auto sceneDraft = Take(SceneDraft::Create(ResourceKind::Scene, registry, *session));
	auto placedSceneRoot = Take(sceneDraft->InstantiateModel(imported.value.guid, model));
	Expect(FindMeshRenderer(placedSceneRoot) != nullptr, "Model was not instantiated into a Scene hierarchy");
	Done(sceneDraft->Save(filesystem, placedScenePath, ResourceId::Generate()));
	auto reopenedScene = Take(SceneDraft::Load(filesystem, placedScenePath, ResourceKind::Scene, registry, *session));
	GameObject::Ptr reopenedSceneModel;
	for (const GameObject::Ptr& root : reopenedScene->GetScene().RootGameObjects())
	{
		if (FindMeshRenderer(root))
		{
			reopenedSceneModel = root;
			break;
		}
	}
	Expect(reopenedSceneModel &&
	           FindMeshRenderer(reopenedSceneModel)
	                   ->GetGameObject()
	                   ->FindComponent<MeshFilterComponent>()
	                   ->SharedMesh()
	                   ->AssetIdentity()
	                   .guid == imported.value.guid &&
	           FindMeshRenderer(reopenedSceneModel)
	                   ->GetGameObject()
	                   ->FindComponent<MeshFilterComponent>()
	                   ->SharedMesh()
	                   ->AssetIdentity()
	                   .localFileId == meshId,
	       "Scene save and reopen did not preserve model sub-asset references");
	const VirtualPath placedPrefabPath = Path("prj:PlacedModel.ursaprefab");
	auto prefabDraft = Take(SceneDraft::Create(ResourceKind::Prefab, registry, *session));
	auto placedPrefabRoot = Take(prefabDraft->InstantiateModel(imported.value.guid, model));
	Expect(FindMeshRenderer(placedPrefabRoot) != nullptr, "Model was not instantiated into a Prefab hierarchy");
	Done(prefabDraft->Save(filesystem, placedPrefabPath, ResourceId::Generate()));
	auto reopenedPrefab =
	    Take(SceneDraft::Load(filesystem, placedPrefabPath, ResourceKind::Prefab, registry, *session));
	Expect(reopenedPrefab->GetPrefabAsset() && FindMeshRenderer(reopenedPrefab->GetPrefabAsset()->Root()) &&
	           FindMeshRenderer(reopenedPrefab->GetPrefabAsset()->Root())
	                   ->GetGameObject()
	                   ->FindComponent<MeshFilterComponent>()
	                   ->SharedMesh()
	                   ->AssetIdentity()
	                   .localFileId == meshId,
	       "Prefab save and reopen did not preserve model sub-asset references");
	auto cached = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(cached && cached.value.cacheHit, "OBJ import did not reuse artifacts after registering its dependencies");
	Expect(cached.value.diagnostics == imported.value.diagnostics,
	       "OBJ import diagnostics were lost when reading the cached model artifact");

	auto previousArtifact = filesystem.Read(imported.value.artifactPath);
	auto previousTexture = filesystem.Read(Path("prj:Models/Textures/UrsaEngine.png"));
	Expect(previousArtifact && previousTexture, "Cannot prepare the missing-texture reimport check");
	Expect(static_cast<bool>(filesystem.Remove(Path("prj:Models/Textures/UrsaEngine.png"))),
	       "Cannot remove the OBJ texture dependency for the failure check");
	auto failedReimport = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(!failedReimport && failedReimport.error.find("Triangle.obj") != String::npos &&
	           failedReimport.error.find("UrsaEngine.png") != String::npos,
	       "Missing OBJ texture did not produce a model-specific import diagnostic");
	auto preservedArtifact = filesystem.Read(imported.value.artifactPath);
	Expect(preservedArtifact && preservedArtifact.value == previousArtifact.value,
	       "Failed OBJ reimport replaced its last-good model artifact");
	Expect(static_cast<bool>(filesystem.WriteAtomic(Path("prj:Models/Textures/UrsaEngine.png"), previousTexture.value)),
	       "Cannot restore the OBJ texture fixture after the failure check");
	auto restoredImport = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(restoredImport && restoredImport.value.cacheHit,
	       "Restoring identical source data did not recover the published OBJ import cache");
	Expect(static_cast<bool>(database.value->Refresh()),
	       "Cannot refresh imported Scene and Prefab metadata before packaging");
	auto currentArtifacts = database.value->CurrentImportedArtifacts();
	Expect(currentArtifacts && currentArtifacts.value.size() >= 3,
	       "AssetDatabase did not report current imported model and texture artifacts for packaging");
	const auto pakPath = BuildPackage(root, assetsRoot, libraryRoot, "ObjPackage");
	auto archive = Take(PakArchive::Open(pakPath));
	VirtualFileSystem packagedFilesystem;
	Done(archive->Mount(packagedFilesystem));
	Expect(!packagedFilesystem.IsMounted(editorLibraryScheme) &&
	           !Take(packagedFilesystem.Exists(Path("prj:Models/Triangle.obj"))) &&
	           !Take(packagedFilesystem.Exists(Path("prj:Models/Triangle.obj.meta"))) &&
	           Take(packagedFilesystem.Exists(Path("prj:__UrsaCooked/AssetCatalog.ursacatalog"))),
	       "Pak included Editor-only source/cache data or omitted its runtime GUID/local-file-ID catalog");
	auto packagedSession = Take(ResourceSession::Open(packagedFilesystem));
	auto packagedMesh = Take(packagedSession->LoadMesh(imported.value.guid, meshId));
	Expect(packagedMesh->Snapshot()->data.indices.size() == 3 &&
	           Take(packagedSession->LoadMaterial(imported.value.guid, materialId))->IsValid(),
	       "Packaged runtime catalog did not resolve the model mesh and its material sub-assets");
	auto packagedTexture = Take(packagedSession->LoadTexture(textureMetadata.value.guid));
	Expect(packagedTexture->GetWidth() == 1448 && packagedTexture->GetHeight() == 1086,
	       "Packaged runtime catalog did not resolve the imported texture without Editor Library data");
}

void TestGltfExternalDependencies(const std::filesystem::path& root)
{
	const auto assetsRoot = root / "Gltf" / "Assets";
	const auto libraryRoot = root / "Gltf" / "Library";
	const auto fixtureRoot = std::filesystem::u8path(URSA_ASSIMP_GLTF_EXTERNAL_DIR);
	const auto modelDirectory = assetsRoot / "Models" / "BoxTextured-glTF";
	std::filesystem::create_directories(assetsRoot / "Models");
	std::filesystem::create_directories(libraryRoot);
	std::filesystem::copy(fixtureRoot, modelDirectory, std::filesystem::copy_options::recursive);
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount the external glTF fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount the external glTF Library");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "Cannot initialize the glTF AssetDatabase");
	const VirtualPath source = Path("prj:Models/BoxTextured-glTF/BoxTextured.gltf");
	const VirtualPath bufferPath = Path("prj:Models/BoxTextured-glTF/BoxTextured0.bin");
	const VirtualPath imagePath = Path("prj:Models/BoxTextured-glTF/CesiumLogoFlat.png");
	auto imported = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(static_cast<bool>(imported), imported ? "" : ToStdString(imported.error).c_str());
	Expect(ContainsPath(imported.value.dependencies, "prj:Models/BoxTextured-glTF/BoxTextured0.bin") &&
	           ContainsPath(imported.value.dependencies, "prj:Models/BoxTextured-glTF/CesiumLogoFlat.png"),
	       "glTF buffer or image was omitted from the persisted import dependencies");
	auto cached = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(cached && cached.value.cacheHit, "Unchanged glTF external dependencies did not reuse the model cache");

	const auto replacementTexture = ReadBytes(std::filesystem::u8path(URSA_DEMO_TEXTURE_PATH));
	Done(filesystem.WriteAtomic(imagePath, replacementTexture));
	auto imageReimport = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(imageReimport && !imageReimport.value.cacheHit,
	       "Changing a glTF external image did not invalidate the model import cache");

	auto bufferBytes = Take(filesystem.Read(bufferPath));
	Expect(bufferBytes.size() > 292, "External glTF buffer fixture is unexpectedly short");
	auto setCoordinate = [](Vector<UInt8>& bytes, Float32 value, Size offset)
	{
		UInt32 bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		for (UInt32 shift = 0; shift < 32; shift += 8)
		{
			bytes[offset + shift / 8] = static_cast<UInt8>((bits >> shift) & 0xff);
		}
	};
	Vector<UInt8> changedBuffer = bufferBytes;
	setCoordinate(changedBuffer, 4.0f, 288);
	Done(filesystem.WriteAtomic(bufferPath, changedBuffer));
	auto bufferReimport = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(bufferReimport && !bufferReimport.value.cacheHit,
	       "Changing a glTF external buffer did not invalidate the model import cache");
	cached = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(cached && cached.value.cacheHit, "Reimported glTF dependencies did not become reusable cache inputs");

	const auto previousArtifact = Take(filesystem.Read(imported.value.artifactPath));
	const auto model = nlohmann::json::from_cbor(previousArtifact.begin(), previousArtifact.end());
	const auto meshId = model.at("meshLocalFileIds").at(0).get<UInt64>();
	AssetDatabase* assetDatabase = database.value.get();
	Vector<ResourceMount> runtimeMounts;
	for (const VirtualMount& mount : filesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto engineFilesystem = Take(OpenEngineFileSystem());
	for (const VirtualMount& mount : engineFilesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto session = Take(ResourceSession::Open(runtimeMounts, [assetDatabase](ResourceId guid, UInt64 localFileId)
	                                          { return assetDatabase->Resolve(guid, localFileId); }));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto sceneDraft = Take(SceneDraft::Create(ResourceKind::Scene, registry, *session));
	auto sceneModel = Take(sceneDraft->InstantiateModel(imported.value.guid, model));
	Expect(FindMeshRenderer(sceneModel) != nullptr, "Imported glTF was not instantiated into a Scene");
	const VirtualPath scenePath = Path("prj:GltfScene.ursascene");
	Done(sceneDraft->Save(filesystem, scenePath, ResourceId::Generate()));
	auto reopenedScene = Take(SceneDraft::Load(filesystem, scenePath, ResourceKind::Scene, registry, *session));
	GameObject::Ptr reopenedModel;
	for (const auto& sceneRoot : reopenedScene->GetScene().RootGameObjects())
	{
		if (FindMeshRenderer(sceneRoot))
		{
			reopenedModel = sceneRoot;
			break;
		}
	}
	Expect(reopenedModel && FindMeshRenderer(reopenedModel)
	                                ->GetGameObject()
	                                ->FindComponent<MeshFilterComponent>()
	                                ->SharedMesh()
	                                ->AssetIdentity()
	                                .localFileId == meshId,
	       "Scene save/reopen lost the imported glTF mesh reference");
	const auto editorStatus = Take(database.value->CurrentImport(source));
	const std::string cliImportCommand = std::string("\"") + URSA_ASSET_IMPORT_EXECUTABLE + "\" --project \"" +
	                                     assetsRoot.parent_path().string() + "\" --engine \"" +
	                                     URSA_DEMO_ENGINE_ASSET_PATH + "\" --configuration " + URSA_TEST_CONFIGURATION +
	                                     " --profile Both --no-scripts";
	Expect(RunProcess(cliImportCommand) == 0, "UrsaAssetImport CLI failed to prepare the isolated glTF project");
	Done(database.value->ReloadImports());
	const auto cliStatus = Take(database.value->CurrentImport(source));
	Expect(editorStatus.dependencies == cliStatus.dependencies &&
	           editorStatus.artifacts.size() == cliStatus.artifacts.size(),
	       "Editor and UrsaAssetImport produced different glTF dependency or artifact inventories");
	for (const auto& editorArtifact : editorStatus.artifacts)
	{
		const auto match = std::find_if(cliStatus.artifacts.begin(), cliStatus.artifacts.end(),
		                                [&](const ImportedArtifactLocation& candidate)
		                                {
			                                return candidate.localFileId == editorArtifact.localFileId &&
			                                       candidate.kind == editorArtifact.kind &&
			                                       candidate.outputKey == editorArtifact.outputKey &&
			                                       candidate.contentFingerprint == editorArtifact.contentFingerprint;
		                                });
		Expect(match != cliStatus.artifacts.end(),
		       "Editor and UrsaAssetImport changed a stable glTF sub-asset or artifact fingerprint");
	}
	const auto pakPath = BuildPackage(root, assetsRoot, libraryRoot, "GltfPackage");
	auto archive = Take(PakArchive::Open(pakPath));
	VirtualFileSystem packagedFilesystem;
	Done(archive->Mount(packagedFilesystem));
	Expect(!Take(packagedFilesystem.Exists(source)), "Runtime glTF pak retained its source document");
	Expect(!Take(packagedFilesystem.Exists(bufferPath)), "Runtime glTF pak retained its external buffer");
	Expect(!Take(packagedFilesystem.Exists(imagePath)), "Runtime glTF pak retained its source texture image");
	auto packagedSession = Take(ResourceSession::Open(packagedFilesystem));
	Expect(Take(packagedSession->LoadMesh(imported.value.guid, meshId))->Snapshot()->data.indices.size() >= 3 &&
	           Take(packagedSession->LoadMaterial(imported.value.guid,
	                                              model.at("materialLocalFileIds").at(0).get<UInt64>()))
	               ->IsValid(),
	       "Runtime glTF pak could not resolve its Mesh and PBR material without source files");

	const VirtualPath corruptBuffer = Path("prj:Models/BoxTextured-glTF/BoxTextured0.bin");
	Done(filesystem.WriteAtomic(corruptBuffer, Vector<UInt8>{0, 0, 0, 0}));
	auto failedReimport = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(!failedReimport && !failedReimport.error.empty(),
	       "Malformed glTF dependency unexpectedly published a new model import");
	Expect(Take(filesystem.Read(imported.value.artifactPath)) == previousArtifact,
	       "Failed glTF dependency import replaced the last successful model artifact");
	Done(filesystem.WriteAtomic(bufferPath, changedBuffer));

	ProjectAssetImporter projectImporter;
	bool changedDuringConstruction = false;
	Done(projectImporter.RegisterNativeProcessing(
	    "ursa.Model", {".gltf"},
	    [&](AssetDatabase& isolated, const VirtualPath& isolatedSource, bool force)
	    {
		    auto candidate = ModelImporter::Import(isolated, isolatedSource, ModelImportSettings{}, force);
		    if (!candidate)
		    {
			    return Result<AssetImportResult>::Failure(candidate.error);
		    }
		    if (!changedDuringConstruction)
		    {
			    changedDuringConstruction = true;
			    Vector<UInt8> concurrentChange = changedBuffer;
			    setCoordinate(concurrentChange, 4.25f, 288);
			    auto written = filesystem.WriteAtomic(bufferPath, concurrentChange);
			    if (!written)
			    {
				    return Result<AssetImportResult>::Failure(written.error);
			    }
		    }
		    return ProjectAssetImporter::LoadCurrent(isolated, isolatedSource);
	    }));
	auto changedSnapshot = projectImporter.Import(*database.value, source, true);
	Expect(changedDuringConstruction && !changedSnapshot &&
	           (changedSnapshot.error.find("inputs changed during import") != String::npos ||
	            changedSnapshot.error.find("inputs changed before candidate publication") != String::npos),
	       "ProjectAssetImporter published a candidate after its external glTF input changed");
	Expect(Take(filesystem.Read(imported.value.artifactPath)) == previousArtifact,
	       "Snapshot rejection changed the last successful glTF model artifact");
}

void TestGltfPlayerWithoutSources(const std::filesystem::path& root)
{
	const auto projectRoot = root / "GltfAnim";
	const auto assetsRoot = projectRoot / "Assets";
	const auto libraryRoot = projectRoot / "Library";
	const auto sourceDirectory = assetsRoot / "Models" / "SimpleSkin";
	std::filesystem::create_directories(sourceDirectory);
	std::filesystem::create_directories(libraryRoot);
	const auto fixture = std::filesystem::u8path(URSA_ASSIMP_GLTF_ANIMATED);
	std::filesystem::copy(fixture.parent_path(), sourceDirectory, std::filesystem::copy_options::recursive);
	const auto source = Path("prj:Models/SimpleSkin/simple_skin.gltf");
	VirtualFileSystem filesystem;
	Done(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite));
	Done(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite));
	const String buildMarker = "namespace GltfPlayer { public static class BuildMarker {} }";
	Done(filesystem.WriteAtomic(Path("prj:BuildMarker.cs"), Vector<UInt8>(buildMarker.begin(), buildMarker.end())));
	auto database = Take(AssetDatabase::Open(filesystem));
	Done(database->Refresh());
	auto imported = Take(ModelImporter::Import(*database, source, ModelImportSettings{}));
	const auto model = ReadArtifact(filesystem, imported.artifactPath);
	Expect(!model.at("animationClipLocalFileIds").empty() && !model.at("materialLocalFileIds").empty(),
	       "Animated glTF player fixture is missing its animation or material");
	Vector<ResourceMount> mounts;
	for (const auto& mount : filesystem.Providers())
	{
		mounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto engineFiles = Take(OpenEngineFileSystem());
	for (const auto& mount : engineFiles.Providers())
	{
		mounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto session = Take(ResourceSession::Open(mounts, [&database](ResourceId guid, UInt64 localFileId)
	                                          { return database->Resolve(guid, localFileId); }));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto scene = Take(SceneDraft::Create(ResourceKind::Scene, registry, *session));
	Take(scene->InstantiateModel(imported.guid, model));
	auto cameraObject = Take(scene->GetScene().CreateGameObject("Camera"));
	LocalTransform cameraTransform;
	cameraTransform.position = {0, 0, 4};
	Done(cameraObject->SetLocalTransform(cameraTransform));
	const auto camera = Take(cameraObject->AddComponent<CameraComponent>());
	Done(scene->GetScene().SetActiveCamera(camera->Id()));
	const auto scenePath = Path("prj:GltfPlayerScene.ursascene");
	Done(scene->Save(filesystem, scenePath, ResourceId::Generate()));

	const std::string cliImport = std::string("\"") + URSA_ASSET_IMPORT_EXECUTABLE + "\" --project \"" +
	                              projectRoot.string() + "\" --engine \"" + URSA_DEMO_ENGINE_ASSET_PATH +
	                              "\" --configuration " + URSA_TEST_CONFIGURATION + " --profile Both";
	Expect(RunProcess(cliImport) == 0, "UrsaAssetImport failed for the animated glTF Player project");
	auto workspace = Take(AssetImportPipeline::OpenProject(projectRoot, URSA_DEMO_ENGINE_ASSET_PATH));
	AssetImportOptions playerOptions;
	playerOptions.profile = "Player";
	playerOptions.configuration = URSA_TEST_CONFIGURATION;
	auto prepared = Take(AssetImportPipeline::Prepare(*workspace, playerOptions));
	Expect(!Take(prepared.runtimeFiles.Exists(source)) &&
	           !Take(prepared.runtimeFiles.Exists(Path("prj:Models/SimpleSkin/quad_skin.glb"))),
	       "Loose glTF Player content retained source model files");
	const auto looseRoot = root / "GltfAnimatedLooseContent";
	VirtualFileSystem loose;
	for (const auto& mapping : Vector<std::pair<String, String>>{{engineScheme, "Engine"}, {projectScheme, "Project"}})
	{
		const auto destination =
		    Take(DirectoryFileProvider::Open(looseRoot / std::filesystem::u8path(ToStdString(mapping.second)), true));
		Done(loose.MountProvider(mapping.first, destination, MountAccess::ReadWrite));
		for (const auto& entry : Take(prepared.runtimeFiles.List(mapping.first)))
		{
			Done(loose.WriteAtomic(entry.path, Take(prepared.runtimeFiles.Read(entry.path))));
		}
	}
	const auto package = BuildPackage(root, assetsRoot, libraryRoot, "GltfAnimatedPlayerPackage");
	auto packagedArchive = Take(PakArchive::Open(package));
	VirtualFileSystem packagedFiles;
	Done(packagedArchive->Mount(packagedFiles));
	Expect(!Take(packagedFiles.Exists(source)) &&
	           !Take(packagedFiles.Exists(Path("prj:Models/SimpleSkin/quad_skin.glb"))) &&
	           !Take(packagedFiles.Exists(Path("prj:Models/SimpleSkin/simple_skin.bin"))),
	       "Animated glTF Player package retained model source files");
	auto packagedSession = Take(ResourceSession::Open(packagedFiles));
	const auto clipId = model.at("animationClipLocalFileIds").at(0).get<UInt64>();
	const auto materialId = model.at("materialLocalFileIds").at(0).get<UInt64>();
	Expect(Take(packagedSession->LoadAnimationClip(imported.guid, clipId))->durationSeconds > 0 &&
	           Take(packagedSession->LoadMaterial(imported.guid, materialId))->IsValid(),
	       "Source-free glTF Player package lost its animation or material");
	std::filesystem::remove_all(sourceDirectory);
	const auto sceneArgument = " --scene GltfPlayerScene.ursascene --hidden --frames 4";
	Expect(RunProcess(std::string("\"") + URSA_SCRIPT_PLAYER_EXECUTABLE + "\" --content \"" + looseRoot.string() +
	                  "\"" + sceneArgument) == 0,
	       "Player failed to run loose cooked glTF content after source removal");
	Expect(RunProcess(std::string("\"") + URSA_SCRIPT_PLAYER_EXECUTABLE + "\" --pak \"" + package.string() + "\"" +
	                  sceneArgument) == 0,
	       "Player failed to run packaged glTF content after source removal");
}

void TestEmbeddedTexture(const std::filesystem::path& root)
{
	const auto assetsRoot = root / "Embedded" / "Assets";
	const auto libraryRoot = root / "Embedded" / "Library";
	std::filesystem::create_directories(assetsRoot);
	std::filesystem::create_directories(libraryRoot);
	std::filesystem::copy_file(std::filesystem::u8path(URSA_ASSIMP_EMBEDDED_FBX_PATH), assetsRoot / "Embedded.fbx");

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount the embedded texture FBX fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount the embedded texture Library");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "Cannot initialize embedded texture metadata");
	auto imported = ModelImporter::Import(*database.value, Path("prj:Embedded.fbx"), ModelImportSettings{});
	Expect(static_cast<bool>(imported), imported ? "" : ToStdString(imported.error).c_str());
	const auto model = ReadArtifact(filesystem, imported.value.artifactPath);
	bool foundEmbeddedReference = false;
	for (const auto& record : model["materials"])
	{
		const auto& textures = record["material"]["textures"];
		for (auto texture = textures.begin(); texture != textures.end(); ++texture)
		{
			const UInt64 localFileId = texture.value()["localFileId"].get<UInt64>();
			if (localFileId != 0 && texture.value()["guid"] == ToStdString(imported.value.guid.ToString()))
			{
				foundEmbeddedReference = true;
				Expect(static_cast<bool>(database.value->Resolve(imported.value.guid, localFileId)),
				       "Embedded texture sub-asset did not resolve by local file ID");
			}
		}
	}
	Expect(foundEmbeddedReference, "FBX embedded texture was not published as a stable model sub-asset");
}

void TestSkeletalAnimationImport(const std::filesystem::path& root)
{
	Vector<RawSkinInfluence> rawWeights{{0, 0, 0.1f}, {0, 1, 0.2f}, {0, 2, 0.3f}, {0, 3, 0.4f}, {0, 4, 0.5f}};
	auto normalizedWeights = ModelImporter::NormalizeSkinInfluences(1, rawWeights, 4);
	Expect(normalizedWeights && normalizedWeights.value[0].jointIndices[0] == 4 &&
	           std::abs(normalizedWeights.value[0].weights[0] - 0.5f / 1.4f) < 0.0001f,
	       "Skin weight normalization did not keep the four strongest influences");
	Vector<RawSkinInfluence> malformedWeights{{1, 0, 1.0f}};
	Expect(!ModelImporter::NormalizeSkinInfluences(1, malformedWeights, 4),
	       "Malformed out-of-range skin vertex data was accepted");
	malformedWeights = {{0, 0, std::numeric_limits<Ursa::Float32>::quiet_NaN()}};
	Expect(!ModelImporter::NormalizeSkinInfluences(1, malformedWeights, 4),
	       "Malformed non-finite skin weight data was accepted");

	const auto assetsRoot = root / "Animated" / "Assets";
	const auto libraryRoot = root / "Animated" / "Library";
	std::filesystem::create_directories(assetsRoot);
	std::filesystem::create_directories(libraryRoot);
	std::filesystem::copy_file(std::filesystem::u8path(URSA_ASSIMP_ANIMATED_FBX_PATH), assetsRoot / "Animated.fbx");
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount the animated FBX fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount the animated FBX Library");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "Cannot initialize animated FBX metadata");
	const VirtualPath source = Path("prj:Animated.fbx");
	auto imported = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(static_cast<bool>(imported), imported ? "" : ToStdString(imported.error).c_str());
	const auto model = ReadArtifact(filesystem, imported.value.artifactPath);
	Expect(model.contains("skeletonLocalFileIds") && model["skeletonLocalFileIds"].is_array() &&
	           !model["skeletonLocalFileIds"].empty() && model.contains("animationClipLocalFileIds") &&
	           model["animationClipLocalFileIds"].is_array() && !model["animationClipLocalFileIds"].empty(),
	       "Animated FBX did not produce stable skeleton and animation clip sub-assets");
	const UInt64 skeletonId = model["skeletonLocalFileIds"][0].get<UInt64>();
	const auto skeletonPath = Take(database.value->Resolve(imported.value.guid, skeletonId));
	const auto skeleton = ReadArtifact(filesystem, skeletonPath);
	Expect(skeleton.value("format", std::string{}) == "UrsaSkeleton" && skeleton["joints"].size() > 1,
	       "Animated FBX skeleton did not preserve its joint hierarchy and bind poses");
	bool foundSkinnedMesh = false;
	for (const auto& meshIdValue : model["meshLocalFileIds"])
	{
		const UInt64 meshId = meshIdValue.get<UInt64>();
		const auto meshPath = Take(database.value->Resolve(imported.value.guid, meshId));
		const auto mesh = ReadArtifact(filesystem, meshPath);
		if (mesh["skinInfluences"].get_binary().empty())
		{
			continue;
		}
		foundSkinnedMesh = true;
		const auto& influenceBytes = mesh["skinInfluences"].get_binary();
		const Ursa::UInt64 vertexCount = mesh["vertexCount"].get<Ursa::UInt64>();
		Expect(mesh["skeleton"]["localFileId"] == skeletonId && influenceBytes.size() == vertexCount * 24,
		       "Skinned mesh influence records do not match the bounded vertex layout");
		bool foundWeight = false;
		for (Ursa::Size offset = 0; offset < influenceBytes.size(); offset += 24)
		{
			Ursa::Float32 totalWeight = 0.0f;
			for (Ursa::Size influence = 0; influence < 4; ++influence)
			{
				Ursa::Float32 weight = 0.0f;
				std::memcpy(&weight, influenceBytes.data() + offset + 8 + influence * sizeof(weight), sizeof(weight));
				Expect(std::isfinite(weight) && weight >= 0.0f, "Imported skin influence weight is malformed");
				totalWeight += weight;
			}
			Expect(totalWeight == 0.0f || std::abs(totalWeight - 1.0f) < 0.001f,
			       "Retained skin weights were not normalized per vertex");
			foundWeight = foundWeight || totalWeight > 0.0f;
		}
		Expect(foundWeight, "Animated FBX contains no retained vertex joint weights");
		break;
	}
	Expect(foundSkinnedMesh, "Animated FBX did not attach joint influences to a mesh sub-asset");
	const UInt64 clipId = model["animationClipLocalFileIds"][0].get<UInt64>();
	const auto clipPath = Take(database.value->Resolve(imported.value.guid, clipId));
	const auto clip = ReadArtifact(filesystem, clipPath);
	Expect(clip.value("format", std::string{}) == "UrsaAnimationClip" && clip.value("durationSeconds", 0.0) > 0.0 &&
	           !clip["channels"].empty(),
	       "Animated FBX clip has no duration or node keyframe channels");
	auto cached = ModelImporter::Import(*database.value, source, ModelImportSettings{});
	Expect(cached && cached.value.cacheHit && cached.value.subAssets.size() == imported.value.subAssets.size(),
	       "Skeletal FBX import did not reuse its stable local sub-assets");
	AssetDatabase* assetDatabase = database.value.get();
	Vector<ResourceMount> runtimeMounts;
	for (const VirtualMount& mount : filesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto engineFilesystem = Take(OpenEngineFileSystem());
	for (const VirtualMount& mount : engineFilesystem.Providers())
	{
		runtimeMounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto session = Take(ResourceSession::Open(runtimeMounts, [assetDatabase](ResourceId assetGuid, UInt64 localFileId)
	                                          { return assetDatabase->Resolve(assetGuid, localFileId); }));
	auto skeletonAsset = Take(session->LoadSkeleton(imported.value.guid, skeletonId));
	auto clipAsset = Take(session->LoadAnimationClip(imported.value.guid, clipId));
	Expect(skeletonAsset->joints.size() == skeleton["joints"].size() &&
	           clipAsset->durationSeconds == clip["durationSeconds"].get<Ursa::Float64>(),
	       "Runtime skeleton or animation clip loading changed imported data");
	AnimatorComponent animator;
	Done(animator.SetAnimation(imported.value.guid, skeletonId, clipId, *session));
	const Vector<Mat4> startPose = animator.JointPalette();
	Done(animator.SetTime(clipAsset->durationSeconds * 0.5));
	bool poseChanged = false;
	for (Ursa::Size joint = 0; joint < startPose.size() && joint < animator.JointPalette().size(); ++joint)
	{
		for (Ursa::Size element = 0; element < startPose[joint].values.size(); ++element)
		{
			poseChanged = poseChanged || std::abs(startPose[joint].values[element] -
			                                      animator.JointPalette()[joint].values[element]) > 0.0001f;
		}
	}
	Expect(poseChanged, "Animator did not evaluate imported node keyframes into a joint pose");
	animator.SetLoop(true);
	Done(animator.SetSpeed(1.0f));
	Done(animator.SetTime(clipAsset->durationSeconds - 0.01));
	Done(animator.Play());
	FrameContext loopFrame;
	loopFrame.deltaSeconds = 0.02f;
	Done(animator.Update(loopFrame));
	Expect(animator.IsPlaying() && animator.Time() < 0.02, "Animator did not wrap clip playback at the loop boundary");
	animator.Stop();
	const Ursa::Float64 stoppedTime = animator.Time();
	loopFrame.deltaSeconds = 1.0f;
	Done(animator.Update(loopFrame));
	Expect(!animator.IsPlaying() && animator.Time() == stoppedTime,
	       "Animator Stop did not retain its current pose time");
	animator.SetLoop(false);
	Done(animator.SetTime(clipAsset->durationSeconds - 0.01));
	Done(animator.Play());
	loopFrame.deltaSeconds = 0.02f;
	Done(animator.Update(loopFrame));
	Expect(!animator.IsPlaying() && std::abs(animator.Time() - clipAsset->durationSeconds) < 0.0001,
	       "Non-looping animation did not stop at the clip duration");
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto draft = Take(SceneDraft::Create(ResourceKind::Scene, registry, *session));
	auto modelRoot = Take(draft->InstantiateModel(imported.value.guid, model));
	Expect(FindSkinnedMeshRenderer(modelRoot) != nullptr && FindAnimator(modelRoot) != nullptr,
	       "Animated model instantiation did not add skinned rendering and Animator components");
	AnimatorComponent* authoredAnimator = FindAnimator(modelRoot);
	Expect(FindSkinnedMeshRenderer(modelRoot)->SharedMesh()->Snapshot()->data.skinInfluences.size() > 0,
	       "Instantiated skinned mesh has no vertex joint influences");
	auto clonedSkin = Take(FindSkinnedMeshRenderer(modelRoot)->SharedMesh()->Clone());
	const auto skinSource = clonedSkin->Snapshot()->data.skeletonReference;
	const auto skinJointIndices = clonedSkin->Snapshot()->data.skinJointIndices;
	const auto skinInverseBindMatrices = clonedSkin->Snapshot()->data.skinInverseBindMatrices;
	const auto standaloneSkinId = ResourceId::Generate();
	const auto standaloneSkinPath = Path("prj:Meshes/IndependentSkin.ursamesh");
	Done(SaveMeshResource(filesystem, standaloneSkinPath, standaloneSkinId, *clonedSkin));
	const auto sidecarText = nlohmann::json{{"guid", ToStdString(standaloneSkinId.ToString())},
	                                        {"version", 1u},
	                                        {"folder", false},
	                                        {"importer", ""},
	                                        {"importerVersion", 0u},
	                                        {"settings", nlohmann::json::object()}}
	                             .dump();
	Done(filesystem.WriteAtomic(Take(VirtualPath::Parse(standaloneSkinPath.ToString() + ".meta")),
	                            Vector<UInt8>(sidecarText.data(), sidecarText.data() + sidecarText.size())));
	SerializationContext skinContext(session.get());
	auto savedSkin = Take(LoadMeshResource(filesystem, standaloneSkinPath, skinContext));
	Expect(savedSkin.mesh->AssetIdentity().guid == standaloneSkinId &&
	           savedSkin.mesh->Snapshot()->data.skeletonReference.guid == skinSource.guid &&
	           savedSkin.mesh->Snapshot()->data.skeletonReference.localFileId == skinSource.localFileId &&
	           savedSkin.mesh->Snapshot()->data.skinJointIndices == skinJointIndices &&
	           savedSkin.mesh->Snapshot()->data.skinInverseBindMatrices.size() == skinInverseBindMatrices.size(),
	       "Standalone skinned Mesh lost its identity, skeleton reference or versioned per-skin binding");
	Done(database.value->Refresh());
	Done(session->Index().Rebuild());
	Done(FindSkinnedMeshRenderer(modelRoot)->SetSharedMesh(savedSkin.mesh));
	Rhi::DeviceDesc deviceDescription;
#ifndef NDEBUG
	deviceDescription.validation = true;
#endif
	auto device = Take(Rhi::CreateDevice(deviceDescription));
	auto renderer = Take(Test::SceneRenderer::Create(device));
	Camera camera;
	camera.position = {0.0f, 0.0f, 10.0f};
	camera.target = {};
	Done(draft->GetScene().SetAmbientLight({1, 1, 1, 1}, 1.0f));
	auto renderPose = [&](Ursa::Float64 time)
	{
		Done(authoredAnimator->SetTime(time));
		const auto frame = Take(draft->GetScene().CollectPreviewRenderData(camera, 1.0f));
		const auto color = Take(device->CreateTexture(
		    {96, 96, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
		Done(Take(renderer->DrawOffscreen(color, frame.viewProjection, frame.objects, frame.lighting,
		                                  frame.cameraPosition))
		         ->Wait());
		const auto readback =
		    Take(device->CreateBuffer({96 * 96 * 4, Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		const auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(color, readback));
		Done(command->End());
		Done(Take(device->Submit(command))->Wait());
		Vector<UInt8> pixels(96 * 96 * 4);
		Done(readback->Read(0, pixels.data(), pixels.size()));
		return pixels;
	};
	const Vector<UInt8> bindPosePixels = renderPose(0.0);
	const Vector<UInt8> animatedPosePixels = renderPose(clipAsset->durationSeconds * 0.5);
	Size changedPosePixels = 0;
	for (Size offset = 0; offset < bindPosePixels.size(); offset += 4)
	{
		if (std::abs(static_cast<int>(bindPosePixels[offset]) - animatedPosePixels[offset]) > 2 ||
		    std::abs(static_cast<int>(bindPosePixels[offset + 1]) - animatedPosePixels[offset + 1]) > 2 ||
		    std::abs(static_cast<int>(bindPosePixels[offset + 2]) - animatedPosePixels[offset + 2]) > 2)
		{
			++changedPosePixels;
		}
	}
	Expect(changedPosePixels > 5, "Vulkan GPU skinning produced identical bind and animated pose pixels");
	renderer.reset();
	Done(device->WaitIdle());
	Done(authoredAnimator->SetSpeed(1.75f));
	authoredAnimator->SetLoop(false);
	Done(authoredAnimator->SetTime(clipAsset->durationSeconds * 0.25));
	Done(authoredAnimator->Play());
	const VirtualPath animatedScenePath = Path("prj:AnimatedScene.ursascene");
	Done(draft->Save(filesystem, animatedScenePath, ResourceId::Generate()));
	auto reopened = Take(SceneDraft::Load(filesystem, animatedScenePath, ResourceKind::Scene, registry, *session));
	AnimatorComponent* reopenedAnimator = nullptr;
	for (const GameObject::Ptr& sceneRoot : reopened->GetScene().RootGameObjects())
	{
		reopenedAnimator = FindAnimator(sceneRoot);
		if (reopenedAnimator)
		{
			break;
		}
	}
	Expect(reopenedAnimator && reopenedAnimator->IsPlaying() && !reopenedAnimator->Loop() &&
	           std::abs(reopenedAnimator->Speed() - 1.75f) < 0.0001f &&
	           std::abs(reopenedAnimator->Time() - clipAsset->durationSeconds * 0.25) < 0.0001,
	       "Scene save and reload did not preserve Animator playback state");
	Done(database.value->Refresh());
	const auto packagePath = BuildPackage(root, assetsRoot, libraryRoot, "AnimatedPackage");
	auto archive = Take(PakArchive::Open(packagePath));
	VirtualFileSystem packagedFilesystem;
	Done(archive->Mount(packagedFilesystem));
	Expect(!packagedFilesystem.IsMounted(editorLibraryScheme) &&
	           !Take(packagedFilesystem.Exists(Path("prj:Animated.fbx"))) &&
	           Take(packagedFilesystem.Exists(Path("prj:__UrsaCooked/AssetCatalog.ursacatalog"))),
	       "Animated model pak retained editor inputs or omitted the runtime catalog");
	auto packagedSession = Take(ResourceSession::Open(packagedFilesystem));
	auto packagedSkeleton = Take(packagedSession->LoadSkeleton(imported.value.guid, skeletonId));
	auto packagedClip = Take(packagedSession->LoadAnimationClip(imported.value.guid, clipId));
	Expect(packagedSkeleton->joints.size() == skeletonAsset->joints.size() &&
	           packagedClip->channels.size() == clipAsset->channels.size(),
	       "Packaged runtime catalog did not resolve skeleton and animation sub-assets without Library data");
	bool loadedPackagedSkin = false;
	for (const auto& meshIdValue : model["meshLocalFileIds"])
	{
		auto mesh = packagedSession->LoadMesh(imported.value.guid, meshIdValue.get<UInt64>());
		if (mesh && mesh.value->Snapshot()->data.skeleton)
		{
			loadedPackagedSkin = true;
			break;
		}
	}
	Expect(loadedPackagedSkin, "Packaged runtime catalog did not resolve a skinned mesh sub-asset");
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::current_path() / "temp" /
		                  ("UrsaDemoModelImportTests-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root);
		TestProvidedFbx(root);
		TestObjMaterialDependencies(root);
		TestGltfExternalDependencies(root);
		TestGltfPlayerWithoutSources(root);
		TestEmbeddedTexture(root);
		TestSkeletalAnimationImport(root);
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
