#include "TestSupport.hpp"
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/RenderingComponents.hpp>
#include <MeshFixtures.hpp>
#include <RenderingFixtures.hpp>
#include <ComputeFixtures.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Rendering/Resources/RenderResources.hpp>
#include <Rendering/Scene/RenderScene.hpp>
#include <Rendering/Pipeline/NativeForwardPipeline.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
void TestClassicRenderingExample(DotNetHost& host, const VirtualPath& contentPath, UInt32 objectCount)
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
	String stableOutputIdentity;
	for (UInt64 frameIndex = 1; frameIndex <= 3; ++frameIndex)
	{
		FrameContext frame;
		frame.frameIndex = frameIndex;
		frame.deltaSeconds = 1.0f / 60.0f;
		scene.BeginFrame();
		Done(scene.Update(frame));
		const String outputIdentity = Take(camera->Name());
		Expect(
		    outputIdentity.find("ClassicRendering:") == 0 && outputIdentity.size() == 50,
		    ("The host camera did not expose a complete output identity and generation: " + ToStdString(outputIdentity))
		        .c_str());
		if (stableOutputIdentity.empty())
		{
			stableOutputIdentity = outputIdentity;
		}
		else
		{
			Expect(outputIdentity == stableOutputIdentity,
			       "Stable output identity or generation changed across frames");
		}
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
		const Size center = (32 * 64 + 32) * 4;
		Expect(pixels[center] < 15 && pixels[center + 1] > 240 && pixels[center + 2] < 15 && pixels[0] < 15 &&
		           pixels[1] < 15 && pixels[2] > 240,
		       "The C# documentation example did not render green geometry over its blue offscreen clear");
	}
	auto replacementOutput = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
	sceneApi.SetRenderOutput(replacementOutput);
	FrameContext replacementFrame;
	replacementFrame.frameIndex = 4;
	replacementFrame.deltaSeconds = 1.0f / 60.0f;
	scene.BeginFrame();
	Done(scene.Update(replacementFrame));
	Expect(Take(camera->Name()) != stableOutputIdentity,
	       "Replacing an output target did not change the camera's output identity");
	const auto replacementPixels = Take(Rendering::ReadRenderTexture(*device, *replacementOutput));
	const Size replacementCenter = (32 * 64 + 32) * 4;
	Expect(replacementPixels[replacementCenter + 1] > 240 && replacementPixels[0] < 15,
	       "A replacement output did not receive the rendered scene");
	sceneApi.SetRenderOutput(output);
	Expect(sceneApi.AcceptedRenderBatchCount() == 4, "The example did not execute one scoped target chain per frame");
	Done(scene.DestroyGameObject(camera));
	for (const auto& cube : cubes)
	{
		Done(scene.DestroyGameObject(cube));
	}
	Done(scene.CommitChanges());
	auto failureCamera = Take(scene.CreateGameObject("Failure Camera"));
	Take(failureCamera->AddComponent<CameraComponent>());
	Take(runtime.CreateAndAdd(failureCamera, "Ursa.SessionFixtures.RenderingFailureProbe, Ursa.SessionFixtures"));
	for (UInt64 frameIndex = 5; frameIndex <= 6; ++frameIndex)
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
	Expect(sceneApi.AcceptedRenderBatchCount() == 8, "Failed rendering did not exercise both command segments");
	Done(scene.DestroyGameObject(failureCamera));
	Done(scene.CommitChanges());
	auto lifetimeCamera = Take(scene.CreateGameObject("Lifetime Camera"));
	Take(lifetimeCamera->AddComponent<CameraComponent>());
	Take(runtime.CreateAndAdd(lifetimeCamera, "Ursa.SessionFixtures.PipelineLifetimeProbe, Ursa.SessionFixtures"));
	for (UInt64 frameIndex = 7; frameIndex <= 8; ++frameIndex)
	{
		FrameContext frame;
		frame.frameIndex = frameIndex;
		scene.BeginFrame();
		Done(scene.Update(frame));
		Expect(Take(lifetimeCamera->Name()) == "PipelineLifetime:" + ToUrsaString(std::to_string(frameIndex - 6)),
		       "The session-owned pipeline or context event did not remain valid across frames");
		const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
		Expect(pixels[0] < 15 && pixels[1] < 15 && pixels[2] > 240,
		       "The session-owned pipeline did not draw its current output");
	}
	Done(scene.DestroyGameObject(lifetimeCamera));
	Done(scene.CommitChanges());
	Done(host.CloseScriptSession(session));
	std::cout << "Compiled classic rendering example: 3 GPU frames passed\n";
	std::cout << "RendererList batch calls: " << objectCount << " objects, 3 submissions in 3 frames\n";
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

void TestManagedForward(DotNetHost& host, const VirtualPath& content)
{
	auto device = Take(Rhi::CreateDevice({}));
	auto baseline = Take(Rendering::NativeForwardPipeline::Create(device));
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
			const auto managedUpdate = scene.Update(frame);
			if (!managedUpdate)
			{
				throw std::runtime_error("Managed Forward update failed in scenario " + std::to_string(scenario) +
				                         ": " + ToStdString(managedUpdate.error));
			}
			Expect(Take(camera->Name()) == "Forward:Rendered", "The C# default Forward callback failed");
			auto currentResult = Rendering::ReadRenderTexture(*device, *output);
			if (!currentResult)
			{
				throw std::runtime_error("Managed Forward output read failed in scenario " + std::to_string(scenario) +
				                         ": " + ToStdString(currentResult.error));
			}
			const auto& current = currentResult.value;
			auto reference = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
			const auto baselineRender = baseline->RenderOffscreen(scene, reference, Rhi::ResourceState::ShaderRead);
			if (!baselineRender)
			{
				throw std::runtime_error("Native Forward baseline failed in scenario " + std::to_string(scenario) +
				                         ": " + ToStdString(baselineRender.error));
			}
			auto expectedResult = Rendering::ReadRenderTexture(*device, *reference);
			if (!expectedResult)
			{
				throw std::runtime_error("Native Forward reference read failed in scenario " +
				                         std::to_string(scenario) + ": " + ToStdString(expectedResult.error));
			}
			const auto& expected = expectedResult.value;
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
		const bool regionsCorrect = pixels[leftPixel] == 0 && pixels[leftPixel + 1] == 188 &&
		                            pixels[leftPixel + 2] == 0 && pixels[rightPixel] == 0 &&
		                            pixels[rightPixel + 1] == 0 && pixels[rightPixel + 2] == 188;
		if (!regionsCorrect)
		{
			throw std::runtime_error(
			    "Camera clear regions failed in frame " + std::to_string(frameIndex) +
			    ": left=" + std::to_string(pixels[leftPixel]) + "," + std::to_string(pixels[leftPixel + 1]) + "," +
			    std::to_string(pixels[leftPixel + 2]) + " right=" + std::to_string(pixels[rightPixel]) + "," +
			    std::to_string(pixels[rightPixel + 1]) + "," + std::to_string(pixels[rightPixel + 2]));
		}
	}
	scene.Clear();
	Done(host.CloseScriptSession(session));
	std::cout << "Managed Forward camera viewport and clear isolation passed\n";
}

void TestUniversalPostProcessingBacking(DotNetHost& host, const VirtualPath& content)
{
	auto device = Take(Rhi::CreateDevice({}));
	Scene scene;
	ManagedSceneApi api(scene);
	api.SetRenderDevice(device);
	auto output = Take(Rendering::CreateRenderTexture(*device, {32, 32, 0, 32}));
	api.SetRenderOutput(output);
	const auto session = Take(host.CreateScriptSession(false, &api));
	Done(host.LoadScriptContent(session, content));
	ManagedScriptRuntime runtime(scene, host, api, session);
	auto colorObject = Take(scene.CreateGameObject("HDR Color Camera"));
	auto* colorCamera = Take(colorObject->AddComponent<CameraComponent>());
	Done(scene.SetActiveCamera(colorCamera->Id()));
	colorCamera->orthographic = true;
	colorCamera->orthographicSize = 5;
	colorCamera->nearPlane = 0.25f;
	colorCamera->farPlane = 42;
	colorCamera->rendering.viewport = {0, 0, 0.5f, 1};
	colorCamera->rendering.background = {4, 0, 0, 1};
	auto directionalObject = Take(scene.CreateGameObject("Snapshot Directional Light"));
	auto* directional = Take(directionalObject->AddComponent<DirectionalLightComponent>());
	Done(directional->SetIntensity(3.5f));
	Done(directional->SetCastsShadows(true));
	Done(directional->SetShadowStrength(0.6f));
	Done(directional->SetShadowBias(0.015f));
	Done(directional->SetShadowNormalBias(0.08f));
	Done(directional->SetSoftShadows(true));
	auto pointObject = Take(scene.CreateGameObject("Snapshot Point Light"));
	auto* point = Take(pointObject->AddComponent<PointLightComponent>());
	Done(point->SetIntensity(2.25f));
	Done(point->SetRange(4));
	LocalTransform pointTransform;
	pointTransform.position = {0, 0, -5};
	Done(pointObject->SetLocalTransform(pointTransform));
	Rendering::RenderScene nativeSnapshot;
	Done(nativeSnapshot.Capture(scene, 1));
	Expect(nativeSnapshot.Lighting().directional.size() == 1 &&
	           nativeSnapshot.Lighting().directional[0].identity == directional->PersistentId() &&
	           nativeSnapshot.Lighting().directional[0].shadowStrength == 0.6f &&
	           nativeSnapshot.Lighting().point.size() == 1 &&
	           nativeSnapshot.Lighting().point[0].identity == point->PersistentId(),
	       "Native scene snapshot is missing the light values expected from the managed query");
	const auto nativeCamera =
	    std::find_if(nativeSnapshot.Cameras().begin(), nativeSnapshot.Cameras().end(),
	                 [&](const Rendering::CameraData& value) { return value.identity == colorCamera->PersistentId(); });
	Expect(nativeCamera != nativeSnapshot.Cameras().end() && nativeCamera->orthographic &&
	           nativeCamera->nearPlane == 0.25f && nativeCamera->farPlane == 42,
	       "Native camera snapshot is missing the projection values expected from the managed query");
	auto depthObject = Take(scene.CreateGameObject("Depth Clear Camera"));
	auto* depthCamera = Take(depthObject->AddComponent<CameraComponent>());
	depthCamera->rendering.depth = 1;
	depthCamera->rendering.viewport = {0, 0, 0.5f, 1};
	depthCamera->rendering.background = {0, 4, 0, 1};
	depthCamera->rendering.clearFlags = Rendering::CameraClearFlags::Depth;
	auto nothingObject = Take(scene.CreateGameObject("Nothing Clear Camera"));
	auto* nothingCamera = Take(nothingObject->AddComponent<CameraComponent>());
	nothingCamera->rendering.depth = 2;
	nothingCamera->rendering.viewport = {0, 0, 0.5f, 1};
	nothingCamera->rendering.background = {0, 0, 4, 1};
	nothingCamera->rendering.clearFlags = Rendering::CameraClearFlags::Nothing;
	// The empty UniversalRendererData above selects the pipeline's generic minimal
	// configuration: EV=0, Reinhard, no shadows, bloom, or FXAA. Compare its complete
	// display output with the unmanaged implementation before the managed callback
	// installs the Universal pipeline asset.
	auto nativeBaseline = Take(Rendering::NativeForwardPipeline::Create(device));
	auto nativeOutput = Take(Rendering::CreateRenderTexture(*device, {32, 32, 0, 32}));
	Done(nativeBaseline->RenderOffscreen(scene, nativeOutput, Rhi::ResourceState::ShaderRead));
	const auto nativePixels = Take(Rendering::ReadRenderTexture(*device, *nativeOutput));
	const auto component =
	    Take(runtime.CreateAndAdd(colorObject, "Ursa.SessionFixtures.UniversalPipelineProbe, Ursa.SessionFixtures"));
	auto* probe = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(component));
	nlohmann::json probeFields = {
	    {"depthCamera",
	     {{"$ursaReference", {{"Kind", "Component"}, {"Id", ToStdString(depthCamera->PersistentId().ToString())}}}}},
	    {"nothingCamera",
	     {{"$ursaReference", {{"Kind", "Component"}, {"Id", ToStdString(nothingCamera->PersistentId().ToString())}}}}},
	    {"expectedDirectionalIdentity", ToStdString(directional->PersistentId().ToString())},
	    {"expectedPointIdentity", ToStdString(point->PersistentId().ToString())}};
	nlohmann::json probeState = {
	    {"FormatVersion", 1u},
	    {"ScriptType", "auto:11111111-1111-4111-8111-111111111111:Ursa.SessionFixtures.UniversalPipelineProbe"},
	    {"Version", 1u},
	    {"Fields", std::move(probeFields)}};
	Done(host.DeserializeBehaviour(probe->InstanceToken(), ToUrsaString(probeState.dump())));
	FrameContext frame;
	frame.frameIndex = 1;
	scene.BeginFrame();
	Done(scene.Update(frame));
	Expect(Take(colorObject->Name()) == "Universal:Rendered", "Universal renderer probe did not finish its frame");
	const auto pixels = Take(Rendering::ReadRenderTexture(*device, *output));
	Size differingChannels = 0;
	for (Size index = 0; index < pixels.size(); ++index)
	{
		if (std::abs(Int32(pixels[index]) - Int32(nativePixels[index])) > 2)
		{
			++differingChannels;
		}
	}
	Expect(differingChannels < 16,
	       "Generic minimal Universal configuration differs from the unmanaged Forward baseline");
	const Size preserved = (16 * 32 + 8) * 4;
	const Size outsideViewport = (16 * 32 + 24) * 4;
	Expect(pixels[preserved] >= 229 && pixels[preserved] <= 232 && pixels[preserved + 1] <= 2 &&
	           pixels[preserved + 2] <= 2,
	       "Depth/Nothing cameras reprocessed display output instead of the preserved linear HDR backing");
	Expect(pixels[outsideViewport] == 0 && pixels[outsideViewport + 1] == 0 && pixels[outsideViewport + 2] == 0,
	       "Universal split-screen processing changed pixels outside the camera viewport");
	scene.Clear();
	Done(host.CloseScriptSession(session));
	std::cout << "Universal postprocessing retained raw linear output through Depth/Nothing cameras\n";
}

} // namespace Ursa::Test
