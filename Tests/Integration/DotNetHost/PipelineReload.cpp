#include "TestSupport.hpp"
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/RenderingComponents.hpp>
#include <RenderingFixtures.hpp>
#include <Rendering/Resources/RenderResources.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
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

} // namespace Ursa::Test
