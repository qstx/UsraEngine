#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/Bindings/BindingRequirements.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Mesh/Primitive.hpp>
#include <MeshFixtures.hpp>
#include <RenderingFixtures.hpp>
#include <ComputeFixtures.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Rendering/Resources/RenderResources.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/TextureResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#if defined(URSA_DOTNET_TEST_EDITOR)
#include <Scripting/ManagedEditorBridge.hpp>
#include <Assets/SceneDraft.hpp>
#include <imgui.h>
#endif
#include <Vfs/PakArchive.hpp>
#include <Vfs/PakBuilder.hpp>
#include <algorithm>
#include <optional>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

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

void TestClassicRenderingExample(DotNetHost& host, const VirtualPath& contentPath, UInt32 objectCount = 1)
{
	Rhi::DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = Take(Rhi::CreateDevice(description));
	auto output = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
	Scene scene;
	ManagedSceneApi sceneApi(scene);
	sceneApi.SetRenderDevice(device);
	sceneApi.SetRenderOutput(output);
	const UInt64 session = Take(host.CreateScriptSession(false, &sceneApi));
	Done(host.LoadScriptContent(session, contentPath));
	Done(host.ValidateScriptSession(session));
	ManagedScriptRuntime runtime(scene, host, sceneApi, session);
	auto camera = Take(scene.CreateGameObject("Classic Camera"));
	Take(camera->AddComponent<CameraComponent>());
	Take(runtime.CreateAndAdd(camera, "Ursa.SessionFixtures.ClassicRenderingProbe, Ursa.SessionFixtures"));
	Vector<GameObject::Ptr> cubes;
	for (UInt32 index = 0; index < objectCount; ++index)
	{
		auto cube = Take(scene.CreateGameObject("Classic Cube"));
		LocalTransform transform;
		transform.position = {0, 0, -3};
		Done(cube->SetLocalTransform(transform));
		Take(Test::AddRenderer(cube, {0, 1, 0, 1}));
		cubes.push_back(cube);
	}
	for (UInt64 frameIndex = 1; frameIndex <= 3; ++frameIndex)
	{
		FrameContext frame;
		frame.frameIndex = frameIndex;
		frame.deltaSeconds = 1.0f / 60.0f;
		scene.BeginFrame();
		Done(scene.Update(frame));
		Expect(Take(camera->Name()) == "ClassicRendering:Submitted", "The compiled documentation example failed");
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
		const Size center = (32 * 64 + 32) * 4;
		Expect(pixels[center] < 15 && pixels[center + 1] > 240 && pixels[center + 2] < 15 && pixels[0] < 15 &&
		           pixels[1] < 15 && pixels[2] > 240,
		       "The C# documentation example did not render green geometry over its blue offscreen clear");
	}
	Expect(sceneApi.AcceptedRenderBatchCount() == 6, "The example did not execute two logical segments per frame");
	Done(scene.DestroyGameObject(camera));
	for (const auto& cube : cubes)
	{
		Done(scene.DestroyGameObject(cube));
	}
	Done(scene.CommitChanges());
	auto failureCamera = Take(scene.CreateGameObject("Failure Camera"));
	Take(failureCamera->AddComponent<CameraComponent>());
	Take(runtime.CreateAndAdd(failureCamera, "Ursa.SessionFixtures.RenderingFailureProbe, Ursa.SessionFixtures"));
	for (UInt64 frameIndex = 4; frameIndex <= 5; ++frameIndex)
	{
		FrameContext frame;
		frame.frameIndex = frameIndex;
		scene.BeginFrame();
		Done(scene.Update(frame));
		Expect(Take(failureCamera->Name()) == "RenderingFailure:Aborted",
		       "Render and end-event failures did not preserve both diagnostics and complete cleanup");
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
		Expect(pixels[0] < 15 && pixels[1] > 240 && pixels[2] < 15,
		       "A render exception discarded the submitted green segment or executed the unsubmitted red segment");
	}
	Expect(sceneApi.AcceptedRenderBatchCount() == 10, "Failed rendering did not exercise both command segments");
	Done(scene.DestroyGameObject(failureCamera));
	Done(scene.CommitChanges());
	auto lifetimeCamera = Take(scene.CreateGameObject("Lifetime Camera"));
	Take(lifetimeCamera->AddComponent<CameraComponent>());
	Take(runtime.CreateAndAdd(lifetimeCamera, "Ursa.SessionFixtures.PipelineLifetimeProbe, Ursa.SessionFixtures"));
	for (UInt64 frameIndex = 6; frameIndex <= 7; ++frameIndex)
	{
		FrameContext frame;
		frame.frameIndex = frameIndex;
		scene.BeginFrame();
		Done(scene.Update(frame));
		Expect(Take(lifetimeCamera->Name()) == "PipelineLifetime:" + ToUrsaString(std::to_string(frameIndex - 5)),
		       "The session-owned pipeline or context event did not remain valid across frames");
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
		Expect(pixels[0] < 15 && pixels[1] < 15 && pixels[2] > 240,
		       "The session-owned pipeline did not draw its current output");
	}
	Done(scene.DestroyGameObject(lifetimeCamera));
	Done(scene.CommitChanges());
	Done(host.CloseScriptSession(session));
	std::cout << "Compiled classic rendering example: 3 GPU frames passed\n";
	std::cout << "RendererList batch calls: " << objectCount << " objects, 6 submissions in 3 frames\n";
	std::cout << "Managed rendering exception cleanup: 2 GPU frames passed\n";
}

void TestManagedMatrixBindings(DotNetHost& host, VirtualFileSystem& filesystem, const VirtualPath& content)
{
	const ResourceId identity = ResourceId::Generate();
	Done(filesystem.MountProvider("matrixfixture", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto shader = Take(Test::CreateComputeFixture({URSA_TEST_DXC_EXECUTABLE, URSA_TEST_SPIRV_VAL_EXECUTABLE,
	                                                     URSA_TEST_SPIRV_CROSS_EXECUTABLE, URSA_SHADER_STAGING_ROOT},
	                                                    identity, true));
	const auto path = Take(VirtualPath::Parse("matrixfixture:MatrixBindings.ursashaderasset"));
	Done(Shader::SaveShaderAssetResource(filesystem, path, *shader));
	const auto resources =
	    Take(ResourceSession::Open(filesystem,
	                               [&](ResourceId guid, UInt64 localId)
	                               {
		                               return guid == identity && localId == 0
		                                          ? Result<VirtualPath>::Success(path)
		                                          : Result<VirtualPath>::Failure("Unknown matrix test shader");
	                               }));
	Scene scene;
	ManagedSceneApi api(scene, resources.get());
	api.SetRenderDevice(Take(Rhi::CreateDevice({})));
	const UInt64 session = Take(host.CreateScriptSession(false, &api));
	Done(host.LoadScriptContent(session, content));
	Done(host.ValidateScriptSession(session));
	ManagedScriptRuntime runtime(scene, host, api, session);
	auto object = Take(scene.CreateGameObject("Matrix bindings"));
	const auto component =
	    Take(runtime.CreateAndAdd(object, "Ursa.SessionFixtures.RenderingBindingsProbe, Ursa.SessionFixtures"));
	const auto* behaviour = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(component));
	const auto fields = nlohmann::json{{"FormatVersion", 1},
	                                   {"ScriptType", "test.rendering-bindings-probe"},
	                                   {"Version", 1},
	                                   {"Fields", {{"shaderGuid", ToStdString(identity.ToString())}}}};
	Done(host.DeserializeBehaviour(behaviour->InstanceToken(), ToUrsaString(fields.dump())));
	Done(host.InvokeBehaviour(behaviour->InstanceToken(), ManagedBehaviourEvent::Update));
	Expect(Take(object->Name()) == "MatrixBindingsPassed",
	       "C# non-symmetric Matrix4x4 did not match both HLSL major orders");
	scene.Clear();
	Done(host.CloseScriptSession(session));
}

void TestPipelineAssetReload(DotNetHost& host, VirtualFileSystem& filesystem, const VirtualPath& content)
{
	const auto rootId = Take(ResourceId::Parse("a5137701-cc10-40f1-8420-8e9769daa201"));
	const auto sharedId = Take(ResourceId::Parse("a5137701-cc10-40f1-8420-8e9769daa202"));
	const auto failingId = Take(ResourceId::Parse("a5137701-cc10-40f1-8420-8e9769daa203"));
	Done(filesystem.MountProvider("reloadfixture", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto rootPath = Take(VirtualPath::Parse("reloadfixture:Pipeline.ursaasset"));
	const auto sharedPath = Take(VirtualPath::Parse("reloadfixture:Shared.ursaasset"));
	const auto failingPath = Take(VirtualPath::Parse("reloadfixture:FailingPipeline.ursaasset"));
	const auto reference = [](ResourceId id)
	{
		return nlohmann::json{{"$ursaReference",
		                       {{"Kind", "ScriptableObject"},
		                        {"Id", ""},
		                        {"Resource", {{"guid", ToStdString(id.ToString())}, {"localFileId", 1u}}}}}};
	};
	ScriptableAsset pipelineAsset;
	Done(pipelineAsset.SetDescription(rootId, 1, "test.reload-pipeline", 1,
	                                  {{"FormatVersion", 1u},
	                                   {"ScriptType", "test.reload-pipeline"},
	                                   {"Version", 1u},
	                                   {"SourceGuid", "11111111-1111-4111-8111-111111111111"},
	                                   {"LocalFileId", 3u},
	                                   {"Fields",
	                                    {{"tint", 23},
	                                     {"failFactory", false},
	                                     {"shared", reference(sharedId)},
	                                     {"future", nlohmann::json::array({1, 2, 3})}}}}));
	ScriptableAsset sharedAsset;
	Done(sharedAsset.SetDescription(sharedId, 1, "test.reload-dependency", 1,
	                                {{"FormatVersion", 1u},
	                                 {"ScriptType", "test.reload-dependency"},
	                                 {"Version", 1u},
	                                 {"SourceGuid", "11111111-1111-4111-8111-111111111111"},
	                                 {"LocalFileId", 4u},
	                                 {"Fields", {{"value", 7}, {"self", reference(sharedId)}}}}));
	ScriptableAsset failingAsset;
	Done(failingAsset.SetDescription(failingId, 1, "test.reload-pipeline", 1,
	                                 {{"FormatVersion", 1u},
	                                  {"ScriptType", "test.reload-pipeline"},
	                                  {"Version", 1u},
	                                  {"SourceGuid", "11111111-1111-4111-8111-111111111111"},
	                                  {"LocalFileId", 3u},
	                                  {"Fields", {{"tint", 99}, {"failFactory", true}}}}));
	Done(filesystem.WriteAtomic(rootPath, Take(EncodeScriptableAssetArtifact(pipelineAsset))));
	Done(filesystem.WriteAtomic(sharedPath, Take(EncodeScriptableAssetArtifact(sharedAsset))));
	Done(filesystem.WriteAtomic(failingPath, Take(EncodeScriptableAssetArtifact(failingAsset))));
	auto resources =
	    Take(ResourceSession::Open(filesystem,
	                               [&](ResourceId id, UInt64 localId)
	                               {
		                               return localId == 1 && (id == rootId || id == sharedId || id == failingId)
		                                          ? Result<VirtualPath>::Success(id == rootId     ? rootPath
		                                                                         : id == sharedId ? sharedPath
		                                                                                          : failingPath)
		                                          : Result<VirtualPath>::Failure("Unknown reload asset");
	                               }));
	auto device = Take(Rhi::CreateDevice({}));

	struct Scope
	{
		Scene scene;
		std::unique_ptr<ManagedSceneApi> api;
		std::unique_ptr<ManagedScriptRuntime> runtime;
		std::shared_ptr<Rendering::RenderTextureResource> output;
		GameObject::Ptr camera;
		ScriptBehaviourComponent* probe = nullptr;
		UInt64 session = 0;
	};

	const auto open = [&]()
	{
		auto scope = std::make_unique<Scope>();
		scope->api = std::make_unique<ManagedSceneApi>(scope->scene, resources.get());
		scope->api->SetRenderDevice(device);
		scope->output = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
		scope->api->SetRenderOutput(scope->output);
		scope->session = Take(host.CreateScriptSession(true, scope->api.get()));
		Done(host.LoadScriptContent(scope->session, content));
		scope->runtime = std::make_unique<ManagedScriptRuntime>(scope->scene, host, *scope->api, scope->session);
		scope->camera = Take(scope->scene.CreateGameObject("Reload Camera"));
		Take(scope->camera->AddComponent<CameraComponent>());
		const auto component = Take(scope->runtime->CreateAndAdd(
		    scope->camera, "Ursa.SessionFixtures.PipelineReloadProbe, Ursa.SessionFixtures"));
		scope->probe = static_cast<ScriptBehaviourComponent*>(scope->scene.GetComponent(component));
		return scope;
	};
	const auto invoke = [&](Scope& scope, Int32 action)
	{
		Done(host.SelectScriptSession(scope.session));
		Done(host.DeserializeBehaviour(scope.probe->InstanceToken(),
		                               ToUrsaString(nlohmann::json{{"FormatVersion", 1u},
		                                                           {"ScriptType", "test.pipeline-reload-probe"},
		                                                           {"Version", 1u},
		                                                           {"Fields", {{"action", action}}}}
		                                                .dump())));
		Done(host.InvokeBehaviour(scope.probe->InstanceToken(), ManagedBehaviourEvent::Update));
	};
	const auto close = [&](Scope& scope)
	{
		scope.scene.Clear();
		scope.runtime.reset();
		Done(host.CloseScriptSession(scope.session));
	};
	auto source = open();
	invoke(*source, 0);
	const auto oldToken = source->probe->InstanceToken();
	for (Int32 failure : {2, 3})
	{
		invoke(*source, failure);
		auto rejected = open();
		Expect(!host.TransferScriptableAssets(source->session, rejected->session),
		       "A failing factory or validation replaced the active asset environment");
		close(*rejected);
		invoke(*source, 4);
		Expect(Take(source->camera->Name()) == "PipelineReload:41:19", "A failed candidate damaged the source fields");
	}
	auto candidate = open();
	Done(host.TransferScriptableAssets(source->session, candidate->session));
	invoke(*candidate, 1);
	Done(filesystem.MountProvider(projectSettingsScheme, std::make_shared<MemoryFileProvider>(),
	                              MountAccess::ReadWrite));
	const Rendering::GraphicsSettings graphics{{rootId, 1}};
	Done(Rendering::SaveGraphicsSettings(filesystem, graphics,
	                                     [&](AssetReference selected)
	                                     {
		                                     return selected.guid == rootId && selected.localFileId == 1
		                                                ? Result<void>{}
		                                                : Result<void>::Failure(
		                                                      "Unexpected Graphics pipeline identity");
	                                     }));
	const auto reopenedSettings = Take(Rendering::ReadGraphicsSettings(filesystem));
	Expect(reopenedSettings.renderPipeline.guid == rootId && reopenedSettings.renderPipeline.localFileId == 1,
	       "Project Graphics settings did not persist the selected pipeline identity");
	Done(host.ConfigureRenderPipeline(candidate->session, reopenedSettings.renderPipeline));
	Done(host.RenderScene(candidate->session, 1));
	const auto pixels = Take(Rendering::ReadRenderTexture(*device, *candidate->output));
	Expect(pixels[0] == 41 && pixels[1] == 0 && pixels[2] == 0,
	       "The reloaded pipeline did not render its unsaved configuration");
	const auto renderViewport =
	    [&](Scene& viewportScene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	        Rendering::CameraType cameraType, std::optional<Rendering::CameraData> cameraOverride = {})
	{
		ManagedSceneApi viewportApi(viewportScene, resources.get());
		viewportApi.SetRenderDevice(device);
		viewportApi.SetRenderOutput(target);
		viewportApi.SetRenderCameraSelection(cameraType, std::move(cameraOverride));
		Done(host.BindScriptSession(candidate->session, viewportApi));
		auto rendered = host.RenderScene(candidate->session,
		                                 Float32(target->description.width) / Float32(target->description.height));
		auto restored = host.BindScriptSession(candidate->session, *candidate->api);
		Done(std::move(restored));
		Done(std::move(rendered));
	};
	auto sceneTarget = Take(Rendering::CreateRenderTexture(*device, {32, 48, 0, 32}));
	Rendering::CameraData sceneViewCamera;
	sceneViewCamera.identity = ResourceId::Generate();
	sceneViewCamera.viewProjection = Mat4::Identity();
	sceneViewCamera.settings.type = Rendering::CameraType::SceneView;
	sceneViewCamera.settings.targetWidth = 32;
	sceneViewCamera.settings.targetHeight = 48;
	renderViewport(candidate->scene, sceneTarget, Rendering::CameraType::SceneView, sceneViewCamera);
	Scene previewScene;
	auto previewCameraObject = Take(previewScene.CreateGameObject("Preview Camera"));
	auto previewCamera = Take(previewCameraObject->AddComponent<CameraComponent>());
	previewCamera->rendering.type = Rendering::CameraType::Preview;
	previewCamera->rendering.targetWidth = 48;
	previewCamera->rendering.targetHeight = 32;
	Done(previewScene.SetActiveCamera(previewCamera->Id()));
	Done(previewScene.CommitChanges());
	auto previewTarget = Take(Rendering::CreateRenderTexture(*device, {48, 32, 0, 32}));
	renderViewport(previewScene, previewTarget, Rendering::CameraType::Preview);
	const auto scenePixels = Take(Rendering::ReadRenderTexture(*device, *sceneTarget));
	const auto previewPixels = Take(Rendering::ReadRenderTexture(*device, *previewTarget));
	Expect(scenePixels[0] == 41 && scenePixels[1] == 0 && scenePixels[2] == 0 && previewPixels[0] == 41 &&
	           previewPixels[1] == 0 && previewPixels[2] == 0 && sceneTarget->description.width == 32 &&
	           sceneTarget->description.height == 48 && previewTarget->description.width == 48 &&
	           previewTarget->description.height == 32,
	       "SceneView and Preview cameras did not use independent render targets with the active pipeline");
	Expect(!host.ConfigureRenderPipeline(candidate->session, {failingId, 1}),
	       "A failing Graphics pipeline candidate was applied successfully");
	Done(host.RenderScene(candidate->session, 1));
	const auto retainedPixels = Take(Rendering::ReadRenderTexture(*device, *candidate->output));
	Expect(retainedPixels[0] == 41 && retainedPixels[1] == 0 && retainedPixels[2] == 0,
	       "A failing Graphics pipeline candidate replaced the active render pipeline");
	close(*source);
	Done(host.SelectScriptSession(candidate->session));
	Expect(!host.InvokeBehaviour(oldToken, ManagedBehaviourEvent::Update), "Reload retained an old source token");
	invoke(*candidate, 1);
	Expect(Take(candidate->camera->Name()) == "PipelineReload:41:19",
	       "Source unload invalidated the candidate asset graph");
	close(*candidate);
	auto reopened = open();
	Done(host.ConfigureRenderPipeline(reopened->session,
	                                  Take(Rendering::ReadGraphicsSettings(filesystem)).renderPipeline));
	Done(host.RenderScene(reopened->session, 1));
	const auto reopenedPixels = Take(Rendering::ReadRenderTexture(*device, *reopened->output));
	Expect(reopenedPixels[0] == 23 && reopenedPixels[1] == 0 && reopenedPixels[2] == 0,
	       "Reopened editor session did not restore the selected Graphics pipeline");
	close(*reopened);
	Expect(Take(DecodeScriptableAssetArtifact(Take(filesystem.Read(rootPath)))).Payload()["Fields"]["tint"] == 23,
	       "Runtime reload fields were written into the persistent source");
	std::cout << "Pipeline asset reload, shared cycles, failed candidates and ALC cleanup passed\n";
}

void TestManagedForward(DotNetHost& host, const VirtualPath& content)
{
	auto device = Take(Rhi::CreateDevice({}));
	auto baseline = Take(Test::SceneRenderer::Create(device));
	for (UInt32 scenario = 0; scenario < 6; ++scenario)
	{
		Scene scene;
		ManagedSceneApi api(scene);
		api.SetRenderDevice(device);
		auto output = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
		api.SetRenderOutput(output);
		const auto session = Take(host.CreateScriptSession(false, &api));
		Done(host.LoadScriptContent(session, content));
		ManagedScriptRuntime runtime(scene, host, api, session);
		auto camera = Take(scene.CreateGameObject("Forward Camera"));
		auto* cameraComponent = Take(camera->AddComponent<CameraComponent>());
		cameraComponent->rendering.background = {0.035f, 0.055f, 0.09f, 1};
		Done(scene.SetActiveCamera(cameraComponent->Id()));
		Take(runtime.CreateAndAdd(camera, "Ursa.SessionFixtures.ForwardPipelineProbe, Ursa.SessionFixtures"));
		auto object = Take(scene.CreateGameObject("Forward Geometry"));
		LocalTransform transform;
		transform.position = {0, 0, -4};
		Done(object->SetLocalTransform(transform));
		auto material = std::make_shared<MaterialInstance>(scenario == 1 ? MaterialType::PbrMetallicRoughness
		                                                                 : MaterialType::Unlit);
		Done(material->SetBaseColor({0.8f, 0.15f, 0.05f, 1}));
		MeshPtr mesh = Take(Take(Test::Cube())->Clone());
		if (scenario == 2)
		{
			Done(material->SetBaseColor({1, 1, 1, 1}));
			Done(material->SetSampler({false, false}));
			Done(material->SetTexture(
			    MaterialTextureSlot::BaseColor,
			    Take(MaterialTexture::Create(2, 1, TextureColorSpace::Srgb, {255, 0, 0, 255, 0, 0, 255, 255}))));
		}
		if (scenario == 4)
		{
			MeshData data;
			data.positions = {{-1.8f, -0.8f, 0}, {-0.2f, -0.8f, 0}, {-0.2f, 0.8f, 0}, {-1.8f, 0.8f, 0},
			                  {0.2f, -0.8f, 0},  {1.8f, -0.8f, 0},  {1.8f, 0.8f, 0},  {0.2f, 0.8f, 0}};
			data.normals.assign(8, {0, 0, 1});
			data.tangents.assign(8, {1, 0, 0, 1});
			data.indices = {0, 2, 1, 0, 3, 2, 12, 14, 13, 12, 15, 14};
			data.subMeshes = {{0, 6, 0}, {6, 6, -8}};
			mesh = Take(Mesh::Create(std::move(data)));
			Done(mesh->RecalculateBounds());
		}
		std::shared_ptr<ModelSkeleton> skeleton;
		RendererComponent* renderer = nullptr;
		if (scenario == 5)
		{
			auto data = Take(mesh->GetData());
			for (auto& position : data.positions)
			{
				position.x += 100;
			}
			skeleton = std::make_shared<ModelSkeleton>();
			skeleton->modelGuid = ResourceId::Generate();
			skeleton->localFileId = 1;
			SkeletonJoint joint;
			joint.nodeLocalFileId = 1;
			joint.bindLocalTransform.position = {-100, 0, -4};
			joint.inverseBindMatrix = Mat4::Identity();
			skeleton->joints = {joint};
			data.skeleton = skeleton;
			data.skeletonReference = {skeleton->modelGuid, 1};
			data.skinInfluences.resize(data.positions.size());
			for (auto& influence : data.skinInfluences)
			{
				influence.weights[0] = 1;
			}
			Done(mesh->SetData(std::move(data)));
			Done(mesh->RecalculateBounds());
			auto* skin = Take(object->AddComponent<SkinnedMeshRendererComponent>());
			Done(skin->SetSharedMesh(mesh));
			renderer = skin;
		}
		else
		{
			Done(Take(object->AddComponent<MeshFilterComponent>())->SetSharedMesh(mesh));
			renderer = Take(object->AddComponent<MeshRendererComponent>());
		}
		Done(renderer->SetMaterial(material));
		if (scenario == 4)
		{
			auto green = std::make_shared<MaterialInstance>(MaterialType::Unlit);
			Done(green->SetBaseColor({0, 1, 0, 1}));
			Done(renderer->SetSharedMaterials({material, green}));
		}
		if (scenario == 1)
		{
			auto light = Take(scene.CreateGameObject("Forward Light"));
			Done(Take(light->AddComponent<DirectionalLightComponent>())->SetIntensity(4));
		}
		if (scenario == 3)
		{
			auto overlay = Take(scene.CreateGameObject("Transparent Overlay"));
			transform.position.z = -3;
			Done(overlay->SetLocalTransform(transform));
			auto transparent = Take(Test::AddRenderer(overlay, {0, 0, 1, 0.5f}));
			Done(transparent->SharedMaterial()->SetAlphaMode(AlphaMode::Blend));
		}
		for (UInt64 frameIndex = 1; frameIndex <= 3; ++frameIndex)
		{
			if (skeleton)
			{
				skeleton->joints[0].bindLocalTransform.position.x = -100 + Float32(frameIndex - 1) * 0.2f;
			}
			FrameContext frame;
			frame.frameIndex = frameIndex;
			scene.BeginFrame();
			Done(scene.Update(frame));
			Expect(Take(camera->Name()) == "Forward:Rendered", "The C# default Forward callback failed");
			const auto current = Take(Rendering::ReadRenderTexture(*device, *output));
			const auto legacy = Take(scene.CollectRenderData(1));
			auto reference = Take(Rendering::CreateRenderTexture(*device, {64, 64}));
			Done(Take(baseline->DrawOffscreen(reference->color, legacy.viewProjection, legacy.objects, legacy.lighting,
			                                  legacy.cameraPosition))
			         ->Wait());
			reference->colorState = Rhi::ResourceState::TransferSource;
			const auto expected = Take(Rendering::ReadRenderTexture(*device, *reference));
			UInt32 differing = 0;
			for (Size index = 0; index < current.size(); ++index)
			{
				if (std::abs(Int32(current[index]) - Int32(expected[index])) > 2)
				{
					++differing;
				}
			}
			if (differing >= 16)
			{
				const Size center = (32 * 64 + 32) * 4;
				throw std::runtime_error(
				    "C# Forward differs in scenario " + std::to_string(scenario) +
				    ", channels=" + std::to_string(differing) + ", center=" + std::to_string(current[center]) + "," +
				    std::to_string(current[center + 1]) + ", baseline=" + std::to_string(expected[center]) + "," +
				    std::to_string(expected[center + 1]));
			}
		}
		scene.Clear();
		Done(host.CloseScriptSession(session));
	}
	std::cout << "Managed Forward: Unlit/PBR/texture/transparent/submesh/moving skin baseline passed\n";
}

void TestDocumentedCustomPipeline(DotNetHost& host, const VirtualPath& content)
{
	auto device = Take(Rhi::CreateDevice({}));
	Scene scene;
	ManagedSceneApi api(scene);
	api.SetRenderDevice(device);
	auto output = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
	api.SetRenderOutput(output);
	const auto session = Take(host.CreateScriptSession(false, &api));
	Done(host.LoadScriptContent(session, content));
	ManagedScriptRuntime runtime(scene, host, api, session);
	auto camera = Take(scene.CreateGameObject("Custom Pipeline Camera"));
	Take(camera->AddComponent<CameraComponent>());
	const auto component =
	    Take(runtime.CreateAndAdd(camera, "Ursa.SessionFixtures.PipelineReloadProbe, Ursa.SessionFixtures"));
	auto* probe = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(component));
	Done(host.DeserializeBehaviour(probe->InstanceToken(),
	                               ToUrsaString(nlohmann::json{{"FormatVersion", 1u},
	                                                           {"ScriptType", "test.pipeline-reload-probe"},
	                                                           {"Version", 1u},
	                                                           {"Fields", {{"action", 5}}}}
	                                                .dump())));
	FrameContext frame;
	frame.frameIndex = 1;
	scene.BeginFrame();
	Done(scene.Update(frame));
	Expect(Take(camera->Name()) == "PipelineReload:Presentation", "The documented custom pipeline factory did not run");
	const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
	Expect(std::abs(int(pixels[0]) - 26) <= 1 && std::abs(int(pixels[1]) - 77) <= 1 &&
	           std::abs(int(pixels[2]) - 179) <= 1,
	       "The documented public C# pipeline did not produce its declared clear color");
	Done(scene.DestroyGameObject(camera));
	Done(scene.CommitChanges());
	Done(host.CloseScriptSession(session));
	std::cout << "Documented asset factory and custom Pipeline GPU output passed\n";
}

void TestForwardCameraRegions(DotNetHost& host, const VirtualPath& content)
{
	auto device = Take(Rhi::CreateDevice({}));
	Scene scene;
	ManagedSceneApi api(scene);
	api.SetRenderDevice(device);
	auto output = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
	api.SetRenderOutput(output);
	const auto session = Take(host.CreateScriptSession(false, &api));
	Done(host.LoadScriptContent(session, content));
	ManagedScriptRuntime runtime(scene, host, api, session);
	auto left = Take(scene.CreateGameObject("Left Camera"));
	auto* leftCamera = Take(left->AddComponent<CameraComponent>());
	leftCamera->rendering.viewport = {0, 0, 0.5f, 1};
	leftCamera->rendering.background = {0, 1, 0, 1};
	auto right = Take(scene.CreateGameObject("Right Camera"));
	auto* rightCamera = Take(right->AddComponent<CameraComponent>());
	rightCamera->rendering.viewport = {0.5f, 0, 0.5f, 1};
	rightCamera->rendering.background = {0, 0, 1, 1};
	rightCamera->rendering.depth = 1;
	const auto component =
	    Take(runtime.CreateAndAdd(left, "Ursa.SessionFixtures.ForwardPipelineProbe, Ursa.SessionFixtures"));
	auto* probe = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(component));
	Done(host.DeserializeBehaviour(
	    probe->InstanceToken(),
	    ToUrsaString(nlohmann::json{
	        {"FormatVersion", 1u},
	        {"ScriptType", "test.forward-pipeline"},
	        {"Version", 1u},
	        {"Fields",
	         {{"other",
	           {{"$ursaReference",
	             {{"Kind", "Component"}, {"Id", ToStdString(rightCamera->PersistentId().ToString())}}}}}}}}
	                     .dump())));
	for (UInt64 frameIndex = 1; frameIndex <= 3; ++frameIndex)
	{
		if (frameIndex == 2)
		{
			rightCamera->rendering.clearFlags = Rendering::CameraClearFlags::Depth;
		}
		if (frameIndex == 3)
		{
			rightCamera->rendering.clearFlags = Rendering::CameraClearFlags::Nothing;
		}
		FrameContext frame;
		frame.frameIndex = frameIndex;
		scene.BeginFrame();
		Done(scene.Update(frame));
		Expect(Take(left->Name()) == "Forward:Rendered", "Multi-camera Forward did not complete");
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
		const Size leftPixel = (32 * 64 + 16) * 4, rightPixel = (32 * 64 + 48) * 4;
		Expect(pixels[leftPixel] == 0 && pixels[leftPixel + 1] == 255 && pixels[leftPixel + 2] == 0 &&
		           pixels[rightPixel] == 0 && pixels[rightPixel + 1] == 0 && pixels[rightPixel + 2] == 255,
		       "Camera clear regions or Depth/Nothing clear flags overwrote another viewport");
	}
	scene.Clear();
	Done(host.CloseScriptSession(session));
	std::cout << "Managed Forward camera viewport and clear isolation passed\n";
}

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

VirtualPath Path(const String& value)
{
	auto path = VirtualPath::Parse(value);
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	return path.value;
}

String Fingerprint(const Vector<UInt8>& bytes)
{
	UInt64 hash = 14695981039346656037ull;
	for (UInt8 value : bytes)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	std::ostringstream result;
	result << std::hex << std::setfill('0') << std::setw(16) << hash;
	return ToUrsaString(result.str());
}

Vector<UInt8> JsonBytes(const nlohmann::json& value)
{
	const std::string text = value.dump();
	const auto* bytes = reinterpret_cast<const UInt8*>(text.data());
	return Vector<UInt8>(bytes, bytes + text.size());
}

bool HasExpectedBytes(const Vector<UInt8>& actual, const Vector<UInt8>& expected)
{
	return actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
}
#if defined(URSA_DOTNET_TEST_EDITOR)
void TestEditorExtensions(DotNetHost& host, VirtualFileSystem& source, const std::filesystem::path& root)
{
	using namespace Ursa::Editor;
	VirtualFileSystem editorFiles;
	Done(editorFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	// Reuse the package's providers through a VFS copy, adding the writable project overlay first.
	auto archive = Take(PakArchive::Open(root / "package" / "Managed.pak"));
	Done(archive->Mount(editorFiles));
	Done(editorFiles.Mount(userDataScheme, root / "user", MountAccess::ReadWrite));
	auto workspace = Take(ProjectWorkspace::FromFilesystem(root / "EditorProject", editorFiles));
	auto documents = Take(EditorDocuments::Create(*workspace));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto document = Take(
	    NewSceneDocument(Path("prj:EditorFixture.ursascene"), ResourceKind::Scene, registry, documents->Session()));
	auto draft = document->Payload<SceneDraft>();
	auto owner = Take(draft->GetScene().CreateGameObject("Editor Probe"));
	const ComponentId scriptId = Take(draft->GetScene().AddComponent<ScriptBehaviourComponent>(
	    owner, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
	auto* script = static_cast<ScriptBehaviourComponent*>(draft->GetScene().GetComponent(scriptId));
	script->SetSerializedPayload(
	    R"({"FormatVersion":1,"ScriptType":"test.session-probe","Version":1,"Fields":{"value":7,"future":{"preserved":"unknown data"}},"FieldTypes":{"value":{"Kind":"SignedInteger","ElementKind":"Unsupported","TypeName":"System.Int32"}}})");
	const ComponentId cameraId = Take(draft->GetScene().AddComponent<CameraComponent>(owner));
	auto* camera = static_cast<CameraComponent*>(draft->GetScene().GetComponent(cameraId));
	Done(documents->RequestOpen(std::move(document)));
	host.SetFilesystem(workspace->Index().Filesystem());
	Done(host.LoadEditorApi(Path("ursa:Ursa.Editor.dll")));
	const UInt64 domain = Take(host.CreateScriptSession(true));
	Done(host.LoadScriptContent(domain, Path("prj:Managed/Game.json")));
	Done(host.LoadScriptAssembly(Take(source.Read(Path("ursa:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host.ValidateScriptSession(domain));
	auto descriptors = Take(host.PrepareEditorExtensions(domain));
	Expect(descriptors.size() == 8,
	       "Managed editor descriptors did not contain the expected menus, window and Inspectors");
	Expect(!host.PrepareEditorExtensions(domain), "The same session registered editor callbacks twice");
	ComponentLoadContext loadContext(documents->Session());
	ManagedEditorContext context;
	context.document = documents->Current();
	context.documents = documents.get();
	context.workspace = workspace.get();
	context.registry = &registry;
	context.selection.object = owner;
	context.selection.roots = draft->GetScene().RootGameObjects();
	context.selection.scene = &draft->GetScene();
	context.selection.loadContext = &loadContext;
	const auto invoke = [&](const String& id) { return InvokeManagedEditorExtension(host, domain, id, context); };
	Done(invoke("menu:Tools/Test/Mark Dirty") ? Result<void>{} : Result<void>::Failure("Managed Dirty menu failed"));
	Expect(documents->Current()->Dirty(), "Managed menu did not mark the editing document Dirty");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Save")), "Managed Save menu failed");
	Expect(!documents->Current()->Dirty() &&
	           Take(workspace->Index().Filesystem().Exists(Path("prj:EditorFixture.ursascene"))),
	       "Managed Save did not persist the document through VFS");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Thread Guard")), "Managed background GUI guard failed");
	Expect(!invoke("menu:Tools/Test/Throw"), "Throwing managed menu was not isolated");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Mark Dirty")),
	       "A throwing menu disabled another editor extension");
	ImGui::CreateContext();
	ImGui::GetIO().DisplaySize = {800, 600};
	ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	const auto draw = [&](const String& id)
	{
		ImGui::NewFrame();
		ImGui::Begin("Managed editor fixture");
		auto result = invoke(id);
		ImGui::End();
		ImGui::Render();
		return result;
	};
	context.target = script;
	Expect(Take(draw("inspector:test.session-probe")), "Managed Inspector did not report its property Apply");
	auto payload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
	Expect(payload["Fields"]["value"] == 8 && documents->Current()->Dirty(),
	       "Managed Inspector did not use the unified serialized property transaction");
	Expect(!draw("inspector:test.session-probe"),
	       "A retained SerializedProperty was accepted in a later callback scope");
	Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["value"] == 8,
	       "A stale managed property modified the editing document");
	context.target = camera;
	Expect(Take(draw("inspector:ursa.Camera")) && camera->nearPlane == 0.25f,
	       "Managed Inspector could not edit an exposed native component");
	Done(documents->SaveCurrent());
	auto reopenedScene = Take(
	    OpenSceneDocument(Path("prj:EditorFixture.ursascene"), ResourceKind::Scene, registry, documents->Session()));
	auto reopenedDraft = reopenedScene->Payload<SceneDraft>();
	auto reopenedOwner = Take(reopenedDraft->GetScene().GameObjectById(owner->Id()));
	auto* reopenedScript = reopenedOwner->FindComponent<ScriptBehaviourComponent>();
	Expect(reopenedScript && reopenedScript->PersistentId() == script->PersistentId() &&
	           nlohmann::json::parse(ToStdString(reopenedScript->SerializedPayload()))["Fields"]["value"] == 8 &&
	           nlohmann::json::parse(ToStdString(
	               reopenedScript->SerializedPayload()))["Fields"]["future"]["preserved"] == "unknown data" &&
	           reopenedOwner->FindComponent<CameraComponent>()->nearPlane == 0.25f,
	       "Editor Scene save/reopen lost typed changes, stable IDs or unknown fields");
	context.target = nullptr;
	Expect(static_cast<bool>(draw("window:Ursa.EditorFixtures.ProbeWindow")),
	       "Managed tool window could not query selection and draw controls");
	Expect(!draw("window:Ursa.EditorFixtures.ThrowWindow"), "Throwing tool window was not isolated");
	Expect(static_cast<bool>(draw("window:Ursa.EditorFixtures.ProbeWindow")),
	       "A throwing tool window disabled an unrelated window");
	// Edit a detached Prefab graph through the same custom Inspector, in a fresh context.
	Done(host.CloseScriptSession(domain));
	const UInt64 replacement = Take(host.CreateScriptSession(true));
	Done(host.LoadScriptContent(replacement, Path("prj:Managed/Game.json")));
	Done(host.LoadScriptAssembly(Take(source.Read(Path("ursa:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host.ValidateScriptSession(replacement));
	auto registrations = Take(host.PrepareEditorExtensions(replacement));
	Expect(registrations.size() == descriptors.size(), "Reload duplicated extension registrations");
	auto prefabDocument = Take(
	    NewSceneDocument(Path("prj:EditorFixture.ursaprefab"), ResourceKind::Prefab, registry, documents->Session()));
	auto prefabDraft = prefabDocument->Payload<SceneDraft>();
	auto prefabRoot = prefabDraft->GetPrefabAsset()->Root();
	auto prefabScript = Take(prefabRoot->AddComponent<ScriptBehaviourComponent>(script->TypeName()));
	prefabScript->SetSerializedPayload(script->SerializedPayload());
	Done(documents->RequestOpen(std::move(prefabDocument)));
	context.document = documents->Current();
	context.target = prefabScript;
	context.selection.object = prefabRoot;
	context.selection.roots = {prefabRoot};
	context.selection.scene = nullptr;
	ImGui::NewFrame();
	ImGui::Begin("Detached Prefab fixture");
	auto prefabEdited = InvokeManagedEditorExtension(host, replacement, "inspector:test.session-probe", context);
	ImGui::End();
	ImGui::Render();
	Expect(prefabEdited && prefabEdited.value &&
	           nlohmann::json::parse(ToStdString(prefabScript->SerializedPayload()))["Fields"]["value"] == 9,
	       "Reloaded Inspector retained old state or failed to edit a detached Prefab");
	Expect(documents->Current()->Dirty(), "Prefab property Apply did not mark the current Prefab document Dirty");
	Done(documents->SaveCurrent());
	auto reopenedPrefab = Take(
	    OpenSceneDocument(Path("prj:EditorFixture.ursaprefab"), ResourceKind::Prefab, registry, documents->Session()));
	auto reopenedPrefabRoot = reopenedPrefab->Payload<SceneDraft>()->GetPrefabAsset()->Root();
	auto* reopenedPrefabScript = reopenedPrefabRoot->FindComponent<ScriptBehaviourComponent>();
	Expect(!documents->Current()->Dirty() && reopenedPrefabRoot->Id() == prefabRoot->Id() && reopenedPrefabScript &&
	           reopenedPrefabScript->PersistentId() == prefabScript->PersistentId() &&
	           nlohmann::json::parse(ToStdString(reopenedPrefabScript->SerializedPayload()))["Fields"]["value"] == 9 &&
	           nlohmann::json::parse(ToStdString(
	               reopenedPrefabScript->SerializedPayload()))["Fields"]["future"]["preserved"] == "unknown data",
	       "Editor Prefab save/reopen lost property changes, unknown fields or stable IDs");
	Done(host.CloseScriptSession(replacement));
	ImGui::DestroyContext();
	Expect(!invoke("menu:Tools/Test/Mark Dirty"), "Closed editing context retained a menu delegate");
	host.SetFilesystem(source);
}
#endif

} // namespace

int main(int argumentCount, char** arguments)
{
	const auto root = std::filesystem::u8path(argumentCount > 2 ? arguments[2] : "dotnet_host_smoke");
	try
	{
		Expect(argumentCount == 3, "Managed host smoke test requires managed output and writable cache directories");
		const auto managedOutput = std::filesystem::u8path(arguments[1]);
		const auto packageRoot = root / "package";
		const auto userRoot = root / "user";
		std::filesystem::create_directories(packageRoot);
		std::filesystem::create_directories(userRoot);

		const Vector<String> fileNames{"Ursa.ManagedHost.runtimeconfig.json",
		                               "Ursa.ManagedHost.deps.json",
		                               "Ursa.ManagedHost.dll",
		                               "Ursa.Bindings.dll",
		                               "Ursa.Runtime.dll",
		                               "Ursa.Rendering.dll",
		                               "Ursa.Editor.dll"};
		VirtualFileSystem looseSource;
		Done(looseSource.Mount(engineScheme, managedOutput, MountAccess::ReadOnly));
		Vector<PakEntryData> entries;
		for (const String& fileName : fileNames)
		{
			const VirtualPath source = Path(String(engineScheme) + ":" + fileName);
			auto bytes = Take(looseSource.Read(source));
			entries.push_back({source, std::move(bytes)});
		}
		const VirtualPath scriptAssemblyPath = Path("ursa:TestScripts/Ursa.ManagedHost.ProtocolTests.dll");
		entries.push_back({scriptAssemblyPath, Take(looseSource.Read(scriptAssemblyPath))});
		const VirtualPath sessionFixturePath = Path("ursa:TestScripts/Ursa.SessionFixtures.dll");
		const auto sessionFixtureBytes = Take(looseSource.Read(sessionFixturePath));
		entries.push_back({sessionFixturePath, sessionFixtureBytes});
		const VirtualPath dependencyPath = Path("ursa:TestScripts/Ursa.SessionDependency.dll");
		const auto dependencyBytes = Take(looseSource.Read(dependencyPath));
		entries.push_back({dependencyPath, dependencyBytes});
#if defined(URSA_DOTNET_TEST_EDITOR)
		const VirtualPath editorFixturePath = Path("ursa:TestScripts/Ursa.EditorFixtures.dll");
		entries.push_back({editorFixturePath, Take(looseSource.Read(editorFixturePath))});
#endif
		const VirtualPath sessionCatalogPath = Path("prj:Managed/ScriptTypeCatalog.json");
		const VirtualPath sessionContentPath = Path("prj:Managed/Game.json");
		nlohmann::json catalog{{"Version", 1u}};
		catalog["Scripts"] = nlohmann::json::array();
		UInt64 fixtureTypeId = 0;
		for (const auto& type : Vector<std::pair<String, String>>{{"SessionProbe", "test.session-probe"},
		                                                          {"LeakingProbe", "test.leaking-probe"},
		                                                          {"ReloadPipelineAsset", "test.reload-pipeline"},
		                                                          {"ReloadDependency", "test.reload-dependency"}})
		{
			catalog["Scripts"].push_back(
			    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
			     {"LocalFileId", ++fixtureTypeId},
			     {"Kind", fixtureTypeId >= 3 ? "ScriptableObject" : "Behaviour"},
			     {"ScriptType", ToStdString(type.second)},
			     {"Version", 1u},
			     {"Group", "Game"},
			     {"ClrTypeName", ToStdString("Ursa.SessionFixtures." + type.first + ", Ursa.SessionFixtures")},
			     {"LegacyIdentity", fixtureTypeId == 1}});
		}
		catalog["Scripts"].push_back(
		    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
		     {"LocalFileId", 5u},
		     {"Kind", "Behaviour"},
		     {"ScriptType", "auto:11111111-1111-4111-8111-111111111111:Ursa.SessionFixtures.AutoIdentityProbe"},
		     {"Version", 1u},
		     {"Group", "Game"},
		     {"ClrTypeName", "Ursa.SessionFixtures.AutoIdentityProbe, Ursa.SessionFixtures"}});
		catalog["Scripts"].push_back(
		    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
		     {"LocalFileId", 6u},
		     {"Kind", "Behaviour"},
		     {"ScriptType", "test.serialized-probe"},
		     {"Version", 1u},
		     {"Group", "Game"},
		     {"ClrTypeName", "Ursa.SessionFixtures.LegacySerializedProbe, Ursa.SessionFixtures"},
		     {"LegacyIdentity", true}});
		catalog["Scripts"].push_back(
		    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
		     {"LocalFileId", 7u},
		     {"Kind", "Behaviour"},
		     {"ScriptType", "test.managed-render-component-probe"},
		     {"Version", 1u},
		     {"Group", "Game"},
		     {"ClrTypeName", "Ursa.SessionFixtures.ManagedRenderComponentProbe, Ursa.SessionFixtures"},
		     {"LegacyIdentity", true}});
		const auto catalogBytes = JsonBytes(catalog);
		nlohmann::json content{{"Version", 2u},
		                       {"RequiredBindings", Bindings::ScriptBindingRequirements(false)},
		                       {"Group", "Game"},
		                       {"TypeCatalog", ToStdString(sessionCatalogPath.ToString())},
		                       {"CatalogFingerprint", ToStdString(Fingerprint(catalogBytes))}};
		content["Assemblies"] =
		    nlohmann::json::array({{{"Name", "Ursa.SessionFixtures"},
		                            {"Path", ToStdString(sessionFixturePath.ToString())},
		                            {"Fingerprint", ToStdString(Fingerprint(sessionFixtureBytes))}}});
		auto missingDependency = content;
		entries.push_back({Path("prj:Managed/MissingDependency.json"), JsonBytes(missingDependency)});
		content["Assemblies"].push_back({{"Name", "Ursa.SessionDependency"},
		                                 {"Path", ToStdString(dependencyPath.ToString())},
		                                 {"Fingerprint", ToStdString(Fingerprint(dependencyBytes))}});
		entries.push_back({sessionCatalogPath, catalogBytes});
		entries.push_back({sessionContentPath, JsonBytes(content)});
		auto incompatibleBindings = content;
		incompatibleBindings["RequiredBindings"][0]["Hash"] = std::string(64, '0');
		entries.push_back({Path("prj:Managed/IncompatibleBindings.json"), JsonBytes(incompatibleBindings)});
		incompatibleBindings = content;
		incompatibleBindings["RequiredBindings"].erase(incompatibleBindings["RequiredBindings"].begin());
		entries.push_back({Path("prj:Managed/MissingBindings.json"), JsonBytes(incompatibleBindings)});
		auto badContent = content;
		badContent["Assemblies"][0]["Fingerprint"] = "0000000000000000";
		entries.push_back({Path("prj:Managed/Invalid.json"), JsonBytes(badContent)});
		badContent = content;
		badContent["Group"] = "Editor";
		entries.push_back({Path("prj:Managed/Editor.json"), JsonBytes(badContent)});
		auto packageBytes = Take(PakBuilder::Encode(std::move(entries)));
		{
			std::ofstream packageOutput(packageRoot / "Managed.pak", std::ios::binary);
			packageOutput.write(reinterpret_cast<const char*>(packageBytes.data()),
			                    static_cast<std::streamsize>(packageBytes.size()));
			Expect(static_cast<bool>(packageOutput), "Cannot create the managed bootstrap package fixture");
		}

		auto archive = Take(PakArchive::Open(packageRoot / "Managed.pak"));
		VirtualFileSystem filesystem;
		Done(archive->Mount(filesystem));
		Done(filesystem.Mount(userDataScheme, userRoot, MountAccess::ReadWrite));
		ManagedHostFiles files;
		files.runtimeConfig = Path("ursa:Ursa.ManagedHost.runtimeconfig.json");
		files.entryAssembly = Path("ursa:Ursa.ManagedHost.dll");
		files.dependencies = {Path("ursa:Ursa.ManagedHost.deps.json"), Path("ursa:Ursa.Runtime.dll"),
		                      Path("ursa:Ursa.Rendering.dll"), Path("ursa:Ursa.Bindings.dll")};
		files.scriptAssemblies = {scriptAssemblyPath};

		auto staged = Take(MaterializeManagedHostFiles(filesystem, files));
		Expect(staged.CacheDirectory().scheme == userDataScheme &&
		           staged.CacheDirectory().relative.find("ManagedBootstrap/") == 0,
		       "Managed bootstrap was not staged in the writable user VFS mount");
		const VirtualPath stagedRuntimeConfig =
		    Path(staged.CacheDirectory().ToString() + "/Ursa.ManagedHost.runtimeconfig.json");
		const auto stagedRuntimeConfigBytes = Take(filesystem.Read(stagedRuntimeConfig));
		const auto sourceRuntimeConfigBytes = Take(filesystem.Read(files.runtimeConfig));
		Expect(HasExpectedBytes(stagedRuntimeConfigBytes, sourceRuntimeConfigBytes),
		       "VFS bootstrap staging changed runtime configuration bytes");
		auto runtimePath = Take(filesystem.ResolveForNativeHostInterop(stagedRuntimeConfig));
		std::error_code pathError;
		const auto relativeBootstrap = std::filesystem::relative(
		    std::filesystem::weakly_canonical(runtimePath), std::filesystem::weakly_canonical(userRoot), pathError);
		Expect(!pathError && std::filesystem::is_regular_file(runtimePath) && !relativeBootstrap.empty() &&
		           !relativeBootstrap.is_absolute() && *relativeBootstrap.begin() != "..",
		       "Native host path did not resolve inside the writable user cache");
		Expect(!filesystem.ResolveForNativeHostInterop(files.runtimeConfig),
		       "Native host path resolution accepted a package source file");

		ManagedHostFiles reordered = files;
		std::reverse(reordered.dependencies.begin(), reordered.dependencies.end());
		auto stagedAgain = Take(MaterializeManagedHostFiles(filesystem, reordered));
		Expect(stagedAgain.CacheDirectory() == staged.CacheDirectory(),
		       "Managed bootstrap fingerprint changed when dependencies were reordered");
		ManagedHostFiles missing = files;
		missing.entryAssembly = Path("ursa:MissingManagedHost.dll");
		auto missingResult = MaterializeManagedHostFiles(filesystem, missing);
		Expect(!missingResult && !missingResult.error.empty(), "Missing VFS bootstrap source was accepted");

		VirtualFileSystem readOnlyCache;
		Done(archive->Mount(readOnlyCache));
		auto memoryProvider = std::make_shared<MemoryFileProvider>();
		Done(readOnlyCache.MountProvider(userDataScheme, memoryProvider, MountAccess::ReadOnly));
		auto readOnlyResult = MaterializeManagedHostFiles(readOnlyCache, files);
		Expect(!readOnlyResult && readOnlyResult.error.find("read-only") != String::npos,
		       "Managed bootstrap wrote to a read-only user mount");

		Scene scene;
		ManagedSceneApi sceneApi(scene);
		auto host = Take(DotNetHost::Create());
		std::vector<std::string> messages;
		std::vector<UInt32> logLevels;
		const VirtualPath invalidRuntimeConfig = Path("user:ManagedBootstrap/invalid.runtimeconfig.json");
		const Vector<UInt8> invalidRuntimeConfigBytes{'{', 'n', 'o', 't', '-', 'j', 's', 'o', 'n', '}'};
		Done(filesystem.WriteAtomic(invalidRuntimeConfig, invalidRuntimeConfigBytes));
		ManagedHostFiles invalidRuntime = files;
		invalidRuntime.runtimeConfig = invalidRuntimeConfig;
		auto failedStart = host->Start(filesystem, invalidRuntime, [](UInt32, const String&) {});
		Expect(!failedStart && !host->IsInitialized(),
		       "Invalid runtime configuration unexpectedly initialized the managed host");

		const VirtualPath missingRuntimeConfig = Path("user:ManagedBootstrap/missing.runtimeconfig.json");
		Done(filesystem.WriteAtomic(
		    missingRuntimeConfig,
		    JsonBytes({{"runtimeOptions",
		                {{"tfm", "net99.0"},
		                 {"rollForward", "Disable"},
		                 {"framework", {{"name", "Microsoft.NETCore.App"}, {"version", "99.0.0"}}}}}})));
		ManagedHostFiles unavailableRuntime = files;
		unavailableRuntime.runtimeConfig = missingRuntimeConfig;
		auto unavailable = host->Start(filesystem, unavailableRuntime, [](UInt32, const String&) {});
		Expect(!unavailable && unavailable.error.find("Cannot initialize CoreCLR") != String::npos &&
		           !host->IsInitialized(),
		       "Unavailable framework produced no diagnostic or prevented a corrected startup");

		auto started = host->Start(
		    filesystem, files,
		    [&messages, &logLevels](UInt32 level, const String& message)
		    {
			    logLevels.push_back(level);
			    messages.emplace_back(message.c_str(), message.size());
		    },
		    &sceneApi);
		Expect(static_cast<bool>(started), ToStdString(started.error).c_str());
		Expect(host->IsInitialized() && logLevels == std::vector<UInt32>({1, 2}) && messages.size() == 2 &&
		           messages.front() == u8"Ursa 托管运行时已初始化" && messages.back().empty(),
		       "Native-to-managed initialization or UTF-8/empty-data callback failed");
		ManagedScriptRuntime scriptRuntime(scene, *host, sceneApi);
		const String probeType = "ProtocolBehaviour, Ursa.ManagedHost.ProtocolTests";
		auto scriptedObject = Take(scene.CreateGameObject("Mixed"));
		auto secondScriptedObject = Take(scene.CreateGameObject("Second"));
		Take(scriptRuntime.CreateAndAdd(scriptedObject, probeType));
		Take(scriptRuntime.CreateAndAdd(secondScriptedObject, probeType));
		const auto addFailureProbe = [&scene, &scriptRuntime](const String& objectName, const String& typeName)
		{
			auto object = Take(scene.CreateGameObject(objectName));
			const ComponentId component = Take(scriptRuntime.CreateAndAdd(object, typeName));
			return std::make_pair(std::move(object), component);
		};
		auto awakeFailure = addFailureProbe("AwakeFailure", "ThrowOnAwakeBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto fixedFailure =
		    addFailureProbe("FixedFailure", "ThrowOnFixedUpdateBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto updateFailure = addFailureProbe("UpdateFailure", "ThrowOnUpdateBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto serializedOwner = Take(scene.CreateGameObject("Serialized"));
		const ComponentId serializedComponent = Take(scene.AddComponent<ScriptBehaviourComponent>(
		    serializedOwner, "SerializedProbeBehaviour, Ursa.ManagedHost.ProtocolTests"));
		auto* serializedScript = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(serializedComponent));
		serializedScript->SetSerializedPayload(
		    R"({"FormatVersion":1,"ScriptType":"test.serialized-probe","Version":1,"Fields":{"savedValue":17}})");
		Done(scriptRuntime.AttachSerializedComponents());
		auto disableFailure =
		    addFailureProbe("DisableFailure", "ThrowOnDisableBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto destroyFailure =
		    addFailureProbe("DestroyFailure", "ThrowOnDestroyBehaviour, Ursa.ManagedHost.ProtocolTests");
		FrameContext frame;
		frame.deltaSeconds = 1.0f / 60.0f;
		frame.fixedDeltaSeconds = 1.0f / 50.0f;
		frame.frameIndex = 1;
		scene.BeginFrame();
		Done(scene.FixedUpdate(frame));
		Done(scene.Update(frame));
		Done(scene.LateUpdate(frame));
		const auto awakeEnabled = scene.IsComponentEnabled(awakeFailure.second);
		const auto fixedEnabled = scene.IsComponentEnabled(fixedFailure.second);
		const auto updateEnabled = scene.IsComponentEnabled(updateFailure.second);
		Expect(awakeEnabled && !awakeEnabled.value && fixedEnabled && !fixedEnabled.value && updateEnabled &&
		           !updateEnabled.value,
		       "Managed Awake/FixedUpdate/Update exceptions were not isolated by disabling their Behaviours");
		Expect(Take(scriptedObject->Name()) == "Mixed:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate",
		       "Mixed native/managed Scene did not dispatch callbacks with the GameObject handle bound");
		Expect(Take(secondScriptedObject->Name()) == "Second:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate",
		       "Two managed Behaviours did not retain independent instances and GameObject references");
		Expect(Take(serializedOwner->Name()) == "Serialized:Awake:17",
		       "Serialized script fields were not restored before managed Awake");
		Done(scene.SetActive(scriptedObject, false));
		Done(scene.SetActive(disableFailure.first, false));
		scene.BeginFrame();
		Done(scene.Update(frame));
		const std::string disabledName = ToStdString(Take(scriptedObject->Name()));
		Expect(disabledName.find(":OnDisable") != std::string::npos,
		       "Managed Behaviour did not receive OnDisable when its GameObject became inactive");
		Expect(ToStdString(Take(secondScriptedObject->Name())) ==
		           "Second:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate:Update",
		       "A disabled Behaviour stopped an independent script instance");
		Expect(static_cast<bool>(scene.DestroyGameObject(disableFailure.first)),
		       "OnDisable exception prevented owner destruction");
		Expect(static_cast<bool>(scene.DestroyGameObject(destroyFailure.first)),
		       "OnDestroy exception prevented owner destruction");
		Done(scene.DestroyGameObject(scriptedObject));
		auto firstDestroyMarker = Take(filesystem.Read(Path("user:ManagedBehaviourOnDestroy.txt")));
		Expect(
		    ToStdString(String(reinterpret_cast<const char*>(firstDestroyMarker.data()), firstDestroyMarker.size())) ==
		        "Mixed:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate:OnDisable:OnDestroy",
		    "Managed OnDestroy could not access its owner before native cleanup");
		Done(scene.DestroyGameObject(secondScriptedObject));
		auto secondDestroyMarker = Take(filesystem.Read(Path("user:ManagedBehaviourOnDestroy.txt")));
		const std::string secondDestroyName =
		    ToStdString(String(reinterpret_cast<const char*>(secondDestroyMarker.data()), secondDestroyMarker.size()));
		Expect(secondDestroyName ==
		           "Second:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate:Update:OnDisable:OnDestroy",
		       ("Second managed Behaviour did not retain independent OnDestroy state: " + secondDestroyName).c_str());
		Done(scene.DestroyGameObject(awakeFailure.first));
		Done(scene.DestroyGameObject(fixedFailure.first));
		Done(scene.DestroyGameObject(updateFailure.first));
		Done(scene.DestroyGameObject(serializedOwner));
		auto mover = Take(scene.CreateGameObject("Mover"));
		Take(scriptRuntime.CreateAndAdd(mover, "MoveWithInputBehaviour, Ursa.ManagedHost.ProtocolTests"));
		FrameContext moveFrame;
		moveFrame.deltaSeconds = 1.0f / 60.0f;
		moveFrame.fixedDeltaSeconds = 1.0f / 50.0f;
		moveFrame.frameIndex = 2;
		const Size rightIndex = static_cast<Size>(Key::Right);
		moveFrame.input.keys[rightIndex] = true;
		moveFrame.input.keyPressed[rightIndex] = true;
		scene.BeginFrame();
		Done(scene.Update(moveFrame));
		const auto movedTransform = Take(scene.GetLocalTransform(mover));
		Expect(movedTransform.position.x == 1.0f,
		       "Managed input did not move the native Transform through the backend-neutral API");
		Done(scene.DestroyGameObject(mover));
		Expect(messages.size() >= 2, "Managed lifecycle dispatch damaged the host logging callback");
		TestManagedRenderingComponentSerialization(*host, filesystem, sessionContentPath);
		TestClassicRenderingExample(*host, sessionContentPath);
		TestClassicRenderingExample(*host, sessionContentPath, 64);
		TestManagedMatrixBindings(*host, filesystem, sessionContentPath);
		TestPipelineAssetReload(*host, filesystem, sessionContentPath);
		TestManagedForward(*host, sessionContentPath);
		TestDocumentedCustomPipeline(*host, sessionContentPath);
		TestForwardCameraRegions(*host, sessionContentPath);

#if defined(URSA_DOTNET_TEST_EDITOR)
		TestEditorExtensions(*host, filesystem, root);
#endif
		Scene missingDependencyScene;
		ManagedSceneApi missingDependencyApi(missingDependencyScene);
		const UInt64 missingDependencyDomain = Take(host->CreateScriptSession(false, &missingDependencyApi));
		Expect(!host->LoadScriptContent(missingDependencyDomain, Path("prj:Managed/MissingDependency.json")),
		       "A project dependency missing from the manifest fell back to another session or the host");
		Done(host->CloseScriptSession(missingDependencyDomain));
		// Separate contexts retain independent statics, even while the edit session stays loaded.
		Scene editScene;
		ManagedSceneApi editApi(editScene);
		const UInt64 editDomain = Take(host->CreateScriptSession(true, &editApi));
		Done(host->LoadScriptContent(editDomain, sessionContentPath));
		Done(host->ValidateScriptSession(editDomain));
		nlohmann::json reloadSnapshots =
		    nlohmann::json::array({{{"Id", "22222222-2222-4222-8222-222222222222"},
		                            {"Payload",
		                             {{"FormatVersion", 1u},
		                              {"ScriptType", "test.session-probe"},
		                              {"Version", 1u},
		                              {"Fields", {{"value", 11}, {"unknownSavedField", 42}}}}}}});
		auto restoredSnapshots = Take(host->PrepareScriptReload(editDomain, ToUrsaString(reloadSnapshots.dump())));
		auto candidateFields = nlohmann::json::parse(ToStdString(restoredSnapshots))[0]["Payload"]["Fields"];
		Expect(candidateFields["value"] == 11 && candidateFields["position"] == nlohmann::json({1, 2, 3}) &&
		           candidateFields["unknownSavedField"] == 42,
		       "Editing reload lost saved fields, constructor defaults or unknown data");
		reloadSnapshots[0]["Payload"]["Version"] = 2u;
		Expect(!host->PrepareScriptReload(editDomain, ToUrsaString(reloadSnapshots.dump())),
		       "Candidate reload accepted an incompatible field version");
		Done(host->SelectScriptSession(editDomain));
		ManagedScriptRuntime editRuntime(editScene, *host, editApi, editDomain);
		auto editObject = Take(editScene.CreateGameObject("Edit"));
		Take(editRuntime.CreateAndAdd(editObject, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
		Expect(Take(editObject->Name()) == "Edit", "Editing a script invoked gameplay callbacks");
		for (UInt32 iteration = 0; iteration < 4; ++iteration)
		{
			Scene runScene;
			auto runApi = std::make_unique<ManagedSceneApi>(runScene);
			const UInt64 runDomain = Take(host->CreateScriptSession(false, runApi.get()));
			Expect(!host->LoadScriptContent(runDomain, Path("prj:Managed/Invalid.json")),
			       "Content fingerprint mismatch was accepted");
			Expect(!host->LoadScriptContent(runDomain, Path("prj:Managed/Editor.json")),
			       "Player session accepted Editor content");
			Expect(!host->LoadScriptContent(runDomain, Path("prj:Managed/IncompatibleBindings.json")) &&
			           !host->LoadScriptContent(runDomain, Path("prj:Managed/MissingBindings.json")),
			       "Player session accepted incompatible or missing binding module identities");
			Done(host->LoadScriptContent(runDomain, sessionContentPath));
			Done(host->ValidateScriptSession(runDomain));
			Expect(
			    Take(host->ResolveBehaviourType(runDomain, R"({"ScriptType":"test.session-probe"})",
			                                    "Old.Namespace.RenamedProbe")) ==
			            "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures" &&
			        Take(host->ResolveBehaviourType(
			            runDomain,
			            R"({"ScriptType":"auto:11111111-1111-4111-8111-111111111111:Ursa.SessionFixtures.AutoIdentityProbe","SourceGuid":"11111111-1111-4111-8111-111111111111","LocalFileId":5})",
			            "Previous.Namespace.AutoIdentityProbe")) ==
			            "Ursa.SessionFixtures.AutoIdentityProbe, Ursa.SessionFixtures" &&
			        !host->ResolveBehaviourType(
			            runDomain,
			            R"({"ScriptType":"auto:11111111-1111-4111-8111-Ursa.SessionFixtures.AutoIdentityProbe","SourceGuid":"11111111-1111-4111-8111-111111111111","LocalFileId":5})",
			            "Old.Namespace.AutoIdentityProbe") &&
			        Take(host->ResolveBehaviourType(
			            runDomain,
			            R"({"ScriptType":"test.session-probe","SourceGuid":"99999999-9999-4999-8999-999999999999","LocalFileId":1})",
			            "Old.Namespace.RenamedProbe")) == "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures" &&
			        !host->ResolveBehaviourType(runDomain, R"({"ScriptType":"test.session-probe","LocalFileId":999})",
			                                    "") &&
			        !host->ResolveBehaviourType(runDomain, R"({"ScriptType":"missing.type"})", ""),
			    "Serialized script identity did not resolve renamed types or reject mismatched asset identities");
			ManagedScriptRuntime runRuntime(runScene, *host, *runApi, runDomain);
			auto runObject = Take(runScene.CreateGameObject("Run"));
			auto scriptId =
			    Take(runRuntime.CreateAndAdd(runObject, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
			auto automaticIdentityOwner = Take(runScene.CreateGameObject("Auto Identity"));
			auto automaticIdentityComponent = Take(runRuntime.CreateAndAdd(
			    automaticIdentityOwner, "Ursa.SessionFixtures.AutoIdentityProbe, Ursa.SessionFixtures"));
			auto* automaticIdentityScript =
			    static_cast<ScriptBehaviourComponent*>(runScene.GetComponent(automaticIdentityComponent));
			const auto automaticIdentityPayload = nlohmann::json::parse(
			    ToStdString(Take(host->SerializeBehaviour(automaticIdentityScript->InstanceToken()))));
			Expect(automaticIdentityScript->TypeName() ==
			               "Ursa.SessionFixtures.AutoIdentityProbe, Ursa.SessionFixtures" &&
			           automaticIdentityPayload["ScriptType"] ==
			               "auto:11111111-1111-4111-8111-111111111111:Ursa.SessionFixtures.AutoIdentityProbe",
			       "A script without UrsaScriptType did not resolve and serialize through its persisted type identity");
			auto* script = static_cast<ScriptBehaviourComponent*>(runScene.GetComponent(scriptId));
			const UInt64 previousToken = script->InstanceToken();
			runScene.BeginFrame();
			Done(runScene.Update(frame));
			Expect(Take(runObject->Name()) == "Session:1:7", "Run reused previous session static state");
			auto fieldPayload = Take(host->SerializeBehaviour(previousToken));
			Expect(fieldPayload.find(R"("position":[1,2,3])") != String::npos,
			       "Managed vector fields disagree with the native Inspector array representation");
			Done(host->SelectScriptSession(editDomain));
			Expect(!host->InvokeBehaviour(previousToken, ManagedBehaviourEvent::Update),
			       "A Behaviour token was accepted in a different script session");
			Done(host->SelectScriptSession(runDomain));
			auto spawningOwner = Take(runScene.CreateGameObject("Spawner"));
			Take(runRuntime.CreateAndAdd(spawningOwner, "Ursa.SessionFixtures.SpawnerProbe, Ursa.SessionFixtures"));
			Done(runScene.Update(frame));
			Expect(Take(spawningOwner->Name()) == "Spawned:Bound:Awake",
			       "Runtime AddComponent did not bind identity before immediate callbacks");
			auto importedOwner = GameObject::Create("Imported");
			auto importedScript = Take(importedOwner->AddComponent<ScriptBehaviourComponent>(
			    "Previous.Namespace.OldProbeName, Ursa.SessionFixtures"));
			importedScript->SetSerializedPayload(
			    R"({"FormatVersion":1,"ScriptType":"test.session-probe","SourceGuid":"99999999-9999-4999-8999-999999999999","LocalFileId":1,"Version":1,"Fields":{"value":13}})");
			Done(runScene.AttachGameObject(importedOwner));
			Expect(Take(importedOwner->Name()) == "Session:2:13" &&
			           importedScript->TypeName() == "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures",
			       "Runtime graph attachment did not restore script fields before immediate Awake");
			auto renderOwner = Take(runScene.CreateGameObject("Managed Render Components"));
			Take(runRuntime.CreateAndAdd(renderOwner,
			                             "Ursa.SessionFixtures.ManagedRenderComponentProbe, Ursa.SessionFixtures"));
			auto* managedCamera = runScene.FindComponent<CameraComponent>(renderOwner);
			auto* managedBox = runScene.FindComponent<MeshRendererComponent>(renderOwner);
			const std::string renderComponentFailure =
			    "C# built-in component wrappers did not mutate native render components: " +
			    ToStdString(Take(renderOwner->Name())) + " (" + std::to_string(renderOwner->Components().size()) +
			    " native components)";
			Expect(managedCamera && std::abs(managedCamera->verticalFovRadians - 1.1f) < 0.0001f &&
			           managedCamera->nearPlane == 0.2f && managedCamera->farPlane == 80.0f && managedBox &&
			           managedBox->SharedMaterial()->Parameters().baseColor.red == 0.25f &&
			           managedBox->SharedMaterial()->Parameters().baseColor.green == 0.5f &&
			           managedBox->SharedMaterial()->Parameters().baseColor.blue == 0.75f,
			       renderComponentFailure.c_str());
			auto destroyer = Take(runScene.CreateGameObject("Destroyer"));
			Take(runRuntime.CreateAndAdd(destroyer, "Ursa.SessionFixtures.DestroyerProbe, Ursa.SessionFixtures"));
			Done(runScene.Update(frame));
			Expect(destroyer->Children().empty(), "Managed Destroy did not complete at the frame boundary");
			runScene.Clear();
			Done(host->CloseScriptSession(runDomain));
			Expect(!host->SelectScriptSession(runDomain), "A closed project context could be selected");
			Done(host->SelectScriptSession(editDomain));
			Expect(!host->InvokeBehaviour(previousToken, ManagedBehaviourEvent::Update),
			       "Stop retained an old managed Behaviour token");
			Expect(Take(editObject->Name()) == "Edit", "Run altered the editing session");
		}
		editScene.Clear();
		Done(host->CloseScriptSession(editDomain));

		Scene abandonedScene;
		ManagedSceneApi abandonedApi(abandonedScene);
		const UInt64 abandonedDomain = Take(host->CreateScriptSession(false, &abandonedApi));
		Done(host->LoadScriptContent(abandonedDomain, sessionContentPath));
		{
			ManagedScriptRuntime abandonedRuntime(abandonedScene, *host, abandonedApi, abandonedDomain);
			auto abandonedOwner = Take(abandonedScene.CreateGameObject("Abandoned"));
			Take(abandonedRuntime.CreateAndAdd(abandonedOwner,
			                                   "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
			abandonedScene.BeginFrame();
			Done(abandonedScene.Update(frame));
		}
		Expect(abandonedScene.RootGameObjects().empty(),
		       "Runtime destruction left Scene components pointing to a destroyed dispatcher");
		Done(host->CloseScriptSession(abandonedDomain));

		Scene leakingScene;
		auto leakingApi = std::make_unique<ManagedSceneApi>(leakingScene);
		const UInt64 leakingDomain = Take(host->CreateScriptSession(false, leakingApi.get()));
		Done(host->LoadScriptContent(leakingDomain, sessionContentPath));
		Done(host->ValidateScriptSession(leakingDomain));
		ManagedScriptRuntime leakingRuntime(leakingScene, *host, *leakingApi, leakingDomain);
		auto leakingObject = Take(leakingScene.CreateGameObject("Leak"));
		Take(leakingRuntime.CreateAndAdd(leakingObject, "Ursa.SessionFixtures.LeakingProbe, Ursa.SessionFixtures"));
		leakingScene.BeginFrame();
		Done(leakingScene.Update(frame));
		leakingScene.Clear();
		Expect(!host->CloseScriptSession(leakingDomain) && host->IsSessionFaulted(),
		       "A strong reference retained by a process event did not fault the host after bounded checks");
		Expect(!host->CreateScriptSession(false), "Unload failure did not prevent another project session");

		auto startedAgain = host->Start(filesystem, files, {});
		Expect(!startedAgain, "The process initialized CoreCLR more than once");
		auto secondHost = Take(DotNetHost::Create());
		auto secondStart = secondHost->Start(filesystem, files, {});
		Expect(!secondStart, "A second DotNetHost instance initialized CoreCLR in the same process");
		// CoreCLR retains the bootstrap assembly for the process lifetime. Keep this fixture in build/temp
		// instead of trying to delete assembly files that are still loaded by the current process.
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
