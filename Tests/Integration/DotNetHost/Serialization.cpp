#include "TestSupport.hpp"
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/TextureResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <cmath>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
void TestManagedRenderingComponentSerialization(DotNetHost& host, VirtualFileSystem& filesystem,
                                                const VirtualPath& contentPath)
{
	if (!filesystem.IsMounted("texturefixture"))
	{
		Done(
		    filesystem.MountProvider("texturefixture", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	}
	const auto textureId = Take(ResourceId::Parse("34567890-0000-4000-8000-000000000001"));
	const auto texturePath = Take(VirtualPath::Parse("texturefixture:ManagedMeshTexture.ursatex"));
	auto texture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255}));
	Done(SaveTextureResource(filesystem, texturePath, textureId, *texture));
	const auto metadata = nlohmann::json{{"guid", ToStdString(textureId.ToString())}}.dump();
	Done(filesystem.WriteAtomic(Take(VirtualPath::Parse(texturePath.ToString() + ".meta")),
	                            Vector<UInt8>(metadata.data(), metadata.data() + metadata.size())));
	auto resources = Take(ResourceSession::Open(filesystem));
	Scene scene;
	auto sceneApi = std::make_unique<ManagedSceneApi>(scene, resources.get());
	const UInt64 session = Take(host.CreateScriptSession(false, sceneApi.get()));
	Done(host.LoadScriptContent(session, contentPath));
	Done(host.ValidateScriptSession(session));
	auto runtime = std::make_unique<ManagedScriptRuntime>(scene, host, *sceneApi, session);
	auto owner = Take(scene.CreateGameObject("Managed Render Components"));
	const auto scriptId =
	    Take(runtime->CreateAndAdd(owner, "Ursa.SessionFixtures.ManagedRenderComponentProbe, Ursa.SessionFixtures"));
	auto* probe = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(scriptId));
	Done(host.DeserializeBehaviour(
	    probe->InstanceToken(),
	    R"({"FormatVersion":1,"ScriptType":"test.managed-render-component-probe","Version":1,"Fields":{"verifyTextureBindings":true}})"));
	FrameContext frame;
	frame.deltaSeconds = 1.0f / 60.0f;
	frame.fixedDeltaSeconds = 1.0f / 50.0f;
	scene.BeginFrame();
	Done(scene.Update(frame));

	auto* camera = scene.FindComponent<CameraComponent>(owner);
	auto* box = scene.FindComponent<MeshRendererComponent>(owner);
	auto* filter = scene.FindComponent<MeshFilterComponent>(owner);
	Expect(camera && box && filter && filter->SharedMesh(),
	       "C# Awake did not add built-in Camera, Mesh Filter and Mesh Renderer components");
	Expect(std::abs(camera->verticalFovRadians - 1.1f) < 0.0001f && camera->nearPlane == 0.2f &&
	           camera->farPlane == 80.0f && box->SharedMaterial()->Parameters().baseColor.red == 0.25f &&
	           box->SharedMaterial()->Parameters().baseColor.green == 0.5f &&
	           box->SharedMaterial()->Parameters().baseColor.blue == 0.75f,
	       "C# built-in component properties did not update native Scene data");

	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	const MaterialIdResolver resolveMaterial = [&resources](const std::shared_ptr<MaterialInstance>& material)
	{ return resources->IdentifyMaterial(material); };
	const ResourceId sceneId = ResourceId::Generate();
	auto sceneDocument =
	    Take(EncodeSceneDocument("ManagedRenderComponents.ursascene", sceneId, scene, registry, resolveMaterial));
	ComponentLoadContext sceneLoadContext(*resources);
	auto loadedScene =
	    Take(DecodeSceneDocument("ManagedRenderComponents.ursascene", sceneDocument, registry, sceneLoadContext));
	auto loadedRoot = loadedScene.scene->RootGameObjects().front();
	const auto* loadedCamera = loadedScene.scene->FindComponent<CameraComponent>(loadedRoot);
	const auto* loadedBox = loadedScene.scene->FindComponent<MeshRendererComponent>(loadedRoot);
	Expect(loadedCamera && std::abs(loadedCamera->verticalFovRadians - 1.1f) < 0.0001f &&
	           loadedCamera->nearPlane == 0.2f && loadedCamera->farPlane == 80.0f && loadedBox &&
	           loadedBox->SharedMaterial()->Parameters().baseColor.red == 0.25f &&
	           loadedBox->SharedMaterial()->Parameters().baseColor.green == 0.5f &&
	           loadedBox->SharedMaterial()->Parameters().baseColor.blue == 0.75f,
	       "Managed built-in component properties did not survive Scene serialization");

	ComponentLoadContext prefabCaptureContext(*resources);
	const ResourceId prefabId = ResourceId::Generate();
	auto prefabRoot = GameObject::Create("Managed Render Components");
	auto* prefabCameraSource = owner->FindComponent<CameraComponent>();
	auto* prefabBoxSource = owner->FindComponent<MeshRendererComponent>();
	Expect(prefabCameraSource && prefabBoxSource,
	       "Managed rendering components were unavailable for Prefab serialization");
	auto* detachedCamera = Take(prefabRoot->AddComponent<CameraComponent>());
	detachedCamera->verticalFovRadians = prefabCameraSource->verticalFovRadians;
	detachedCamera->nearPlane = prefabCameraSource->nearPlane;
	detachedCamera->farPlane = prefabCameraSource->farPlane;
	auto* detachedBox = Take(prefabRoot->AddComponent<MeshRendererComponent>());
	Done(detachedBox->SetMaterials(prefabBoxSource->SharedMaterials()));
	auto* detachedFilter = Take(prefabRoot->AddComponent<MeshFilterComponent>());
	Done(detachedFilter->SetMesh(filter->SharedMesh()));
	PrefabAsset prefab(prefabId, std::move(prefabRoot));
	auto prefabDocument =
	    Take(EncodePrefabAssetDocument("ManagedRenderComponents.ursaprefab", prefab, registry, resolveMaterial));
	ComponentLoadContext prefabLoadContext(*resources);
	auto loadedPrefab =
	    Take(DecodePrefabAssetDocument("ManagedRenderComponents", prefabDocument, registry, prefabLoadContext));
	auto* prefabCamera = loadedPrefab.Root()->FindComponent<CameraComponent>();
	auto* prefabBox = loadedPrefab.Root()->FindComponent<MeshRendererComponent>();
	Expect(prefabCamera && std::abs(prefabCamera->verticalFovRadians - 1.1f) < 0.0001f &&
	           prefabCamera->nearPlane == 0.2f && prefabCamera->farPlane == 80.0f && prefabBox &&
	           prefabBox->SharedMaterial()->Parameters().baseColor.red == 0.25f &&
	           prefabBox->SharedMaterial()->Parameters().baseColor.green == 0.5f &&
	           prefabBox->SharedMaterial()->Parameters().baseColor.blue == 0.75f,
	       "Managed built-in component properties did not survive Prefab serialization");

	auto loadedApi = std::make_unique<ManagedSceneApi>(*loadedScene.scene, resources.get());
	const UInt64 loadedSession = Take(host.CreateScriptSession(false, loadedApi.get()));
	Done(host.LoadScriptContent(loadedSession, contentPath));
	Done(host.ValidateScriptSession(loadedSession));
	auto loadedRuntime = std::make_unique<ManagedScriptRuntime>(*loadedScene.scene, host, *loadedApi, loadedSession);
	Done(loadedRuntime->AttachSerializedComponents());
	loadedScene.scene->BeginFrame();
	Done(loadedScene.scene->Update(frame));
	Expect(loadedRoot->Name().value == "MeshFields:Restored",
	       "Actual C# Mesh/Material fields were not restored before Awake");
	loadedScene.scene->Clear();
	loadedRuntime.reset();
	Done(host.CloseScriptSession(loadedSession));
	loadedApi.reset();
	Done(host.SelectScriptSession(session));
	scene.Clear();
	runtime.reset();
	Done(host.CloseScriptSession(session));
	sceneApi.reset();
}

} // namespace Ursa::Test
