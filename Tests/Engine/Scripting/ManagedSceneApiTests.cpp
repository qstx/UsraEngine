#include <MeshFixtures.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SceneResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		throw std::runtime_error(message);
	}
}

nlohmann::json ResourceRequest(NativeApiV5& api, const nlohmann::json& request)
{
	const std::string payload = request.dump();
	UInt64 required = 0;
	const Int32 sizing = api.resourceRequest(api.sceneContext, reinterpret_cast<const UInt8*>(payload.data()),
	                                         payload.size(), nullptr, 0, &required);
	Expect(sizing == 4 && required, "Managed resource request did not return a response size");
	Vector<UInt8> response;
	response.resize(Size(required));
	Expect(api.resourceRequest(api.sceneContext, nullptr, 0, response.data(), response.size(), &required) == 0,
	       "Managed resource request response could not be read");
	auto parsed = nlohmann::json::parse(response.begin(), response.end());
	if (parsed.contains("error"))
	{
		const std::string error =
		    request.value("operation", std::string("resource request")) + ": " + parsed["error"].get<std::string>();
		throw std::runtime_error(error);
	}
	return parsed.at("value");
}

ManagedHandle Create(NativeApiV5& api, const char* name)
{
	ManagedHandle handle{};
	const auto* bytes = reinterpret_cast<const UInt8*>(name);
	const auto result = api.createGameObject(api.sceneContext, bytes, std::strlen(name), &handle);
	Expect(result == 0, "Managed API failed to create a GameObject");
	return handle;
}

bool SameHandle(const ManagedHandle& left, const ManagedHandle& right)
{
	return left.sessionId == right.sessionId && left.index == right.index && left.generation == right.generation &&
	       left.kind == right.kind && left.reserved == right.reserved;
}

ManagedHandle Handle(const nlohmann::json& value)
{
	return {value.at("sessionId").get<UInt64>(), value.at("index").get<UInt32>(), value.at("generation").get<UInt32>(),
	        ManagedHandleKind(value.at("kind").get<UInt32>()), value.value("reserved", UInt32{0})};
}
} // namespace

int main()
{
	try
	{
		using namespace Ursa;
		using namespace Ursa::Scripting;
		VirtualFileSystem resourceFiles;
		Done(
		    resourceFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const ResourceId materialGuid = ResourceId::Generate();
		const VirtualPath materialPath = Take(VirtualPath::Parse("prj:managed-renderer.ursamat"));
		MaterialInstance material(MaterialType::Unlit);
		Done(SaveMaterialResource(resourceFiles, materialPath, materialGuid, material, {}));
		auto resources = Take(ResourceSession::Open(
		    resourceFiles,
		    [materialGuid, materialPath](ResourceId guid, UInt64 localFileId) -> Result<VirtualPath>
		    {
			    return guid == materialGuid && localFileId == 0
			               ? Result<VirtualPath>::Success(materialPath)
			               : Result<VirtualPath>::Failure("Managed renderer fixture asset is missing");
		    }));
		String windowTitle;
		Scene scene;
		ManagedSceneApi sceneApi(scene, resources.get(), [&windowTitle](const String& value) { windowTitle = value; });
		NativeApiV5 api = sceneApi.Bind({});
		Rhi::DeviceDesc renderDeviceDescription;
		auto renderDevice = Take(Rhi::CreateDevice(renderDeviceDescription));
		sceneApi.SetRenderDevice(renderDevice);
		const auto renderTextureJson = ResourceRequest(api, {{"operation", "renderTexture.create"},
		                                                     {"width", 32u},
		                                                     {"height", 16u},
		                                                     {"format", 0u},
		                                                     {"depthBufferBits", 32u},
		                                                     {"enableRandomWrite", false},
		                                                     {"msaaSamples", 1u}});
		const ManagedHandle renderTextureHandle = Handle(renderTextureJson);
		Expect(renderTextureHandle.kind == ManagedHandleKind::RenderTexture &&
		           ResourceRequest(api, {{"operation", "resource.valid"}, {"handle", renderTextureJson}}).get<bool>(),
		       "Managed RenderTexture did not allocate valid color/depth RHI attachments");
		const auto graphicsBufferJson = ResourceRequest(
		    api, {{"operation", "graphicsBuffer.create"}, {"target", 4u}, {"count", 32u}, {"stride", 16u}});
		const ManagedHandle graphicsBufferHandle = Handle(graphicsBufferJson);
		Expect(graphicsBufferHandle.kind == ManagedHandleKind::GraphicsBuffer &&
		           ResourceRequest(api, {{"operation", "resource.valid"}, {"handle", graphicsBufferJson}}).get<bool>(),
		       "Managed GraphicsBuffer did not allocate valid structured storage");
		ResourceRequest(api, {{"operation", "renderTexture.release"}, {"handle", renderTextureJson}});
		ResourceRequest(api, {{"operation", "graphicsBuffer.release"}, {"handle", graphicsBufferJson}});
		Expect(!ResourceRequest(api, {{"operation", "resource.valid"}, {"handle", renderTextureJson}}).get<bool>() &&
		           !ResourceRequest(api, {{"operation", "resource.valid"}, {"handle", graphicsBufferJson}}).get<bool>(),
		       "Released managed RHI resources remained valid");
		bool overCapacityRejected = false;
		try
		{
			ResourceRequest(api, {{"operation", "renderTexture.create"},
			                      {"width", renderDevice->Capabilities().maxTextureSize + 1u},
			                      {"height", 16u},
			                      {"format", 0u},
			                      {"depthBufferBits", 0u},
			                      {"enableRandomWrite", false},
			                      {"msaaSamples", 1u}});
		}
		catch (const std::runtime_error&)
		{
			overCapacityRejected = true;
		}
		Expect(overCapacityRejected, "Managed RenderTexture accepted dimensions above the RHI capability");
		bool bufferCapacityRejected = false;
		try
		{
			const UInt32 excessiveCount =
			    static_cast<UInt32>(renderDevice->Capabilities().maxStorageBufferRange / 16 + 1);
			ResourceRequest(
			    api,
			    {{"operation", "graphicsBuffer.create"}, {"target", 4u}, {"count", excessiveCount}, {"stride", 16u}});
		}
		catch (const std::runtime_error&)
		{
			bufferCapacityRejected = true;
		}
		Expect(bufferCapacityRejected, "Managed GraphicsBuffer exceeded the RHI storage buffer capability");
		Rendering::RenderContextToken renderToken{77, 9, 3, 4};
		sceneApi.SetRenderContextToken(renderToken);
		Rendering::RenderCommandBatchHeader renderBatchHeader;
		Vector<UInt8> renderBatch(sizeof(renderBatchHeader));
		std::memcpy(renderBatch.data(), &renderBatchHeader, sizeof(renderBatchHeader));
		const auto submitRenderBatch = [&](const Rendering::RenderContextToken& token, const Vector<UInt8>& batch)
		{
			return api.submitRenderBatch(api.sceneContext, reinterpret_cast<const UInt8*>(&token), sizeof(token),
			                             batch.data(), batch.size());
		};
		Expect(api.submitRenderBatch && submitRenderBatch(renderToken, renderBatch) == 0 &&
		           sceneApi.AcceptedRenderBatchCount() == 1,
		       "Typed render batch was not accepted by its active context");
		auto staleRenderToken = renderToken;
		++staleRenderToken.generation;
		Expect(submitRenderBatch(staleRenderToken, renderBatch) != 0 && sceneApi.AcceptedRenderBatchCount() == 1,
		       "Stale render generation was accepted");
		auto foreignRenderSession = renderToken;
		++foreignRenderSession.sessionId;
		Expect(submitRenderBatch(foreignRenderSession, renderBatch) != 0 && sceneApi.AcceptedRenderBatchCount() == 1,
		       "Render batch from a different session was accepted");
		auto invalidRenderBatch = renderBatch;
		invalidRenderBatch[0] = 0xff;
		Expect(submitRenderBatch(renderToken, invalidRenderBatch) != 0 && sceneApi.AcceptedRenderBatchCount() == 1,
		       "Malformed typed render batch was accepted");
		Int32 workerBatchStatus = 0;
		std::thread worker([&] { workerBatchStatus = submitRenderBatch(renderToken, renderBatch); });
		worker.join();
		Expect(workerBatchStatus != 0 && sceneApi.AcceptedRenderBatchCount() == 1,
		       "Render batch was accepted from a non-owner thread");
		sceneApi.ClearRenderContextToken();
		const ManagedHandle parent = Create(api, "父节点");
		const ManagedHandle child = Create(api, "Child");

		Expect(api.setGameObjectParent(api.sceneContext, child, parent) == 0, "Managed API failed to set a parent");
		ManagedHandle actualParent{};
		Expect(api.getGameObjectParent(api.sceneContext, child, &actualParent) == 0 && SameHandle(parent, actualParent),
		       "Managed API returned the wrong GameObject parent");

		const char renamed[] = "子节点-雪";
		Expect(api.setGameObjectName(api.sceneContext, child, reinterpret_cast<const UInt8*>(renamed),
		                             sizeof(renamed) - 1) == 0,
		       "Managed API failed to set a UTF-8 name");
		UInt64 required = 0;
		Expect(api.getGameObjectName(api.sceneContext, child, nullptr, 0, &required) == 4 &&
		           required == sizeof(renamed) - 1,
		       "Managed API did not report the UTF-8 name buffer size");
		Vector<UInt8> name(required);
		Expect(api.getGameObjectName(api.sceneContext, child, name.data(), name.size(), &required) == 0 &&
		           std::equal(name.begin(), name.end(), reinterpret_cast<const UInt8*>(renamed)),
		       "Managed API changed the UTF-8 GameObject name");
		UInt64 idRequired = 0;
		Expect(api.getGameObjectPersistentId(api.sceneContext, child, nullptr, 0, &idRequired) == 4 && idRequired == 36,
		       "Managed API did not report the GameObject persistent ID buffer size");
		Vector<UInt8> gameObjectId(idRequired);
		ManagedHandle resolvedObject{};
		Expect(api.getGameObjectPersistentId(api.sceneContext, child, gameObjectId.data(), gameObjectId.size(),
		                                     &idRequired) == 0 &&
		           api.resolveGameObjectPersistentId(api.sceneContext, gameObjectId.data(), gameObjectId.size(),
		                                             &resolvedObject) == 0 &&
		           SameHandle(child, resolvedObject),
		       "Managed API did not resolve a GameObject by persistent ID");

		Float32 transform[10]{};
		Expect(api.getLocalTransform(api.sceneContext, child, transform, 10) == 0 && transform[6] == 1.0f &&
		           transform[7] == 1.0f && transform[8] == 1.0f && transform[9] == 1.0f,
		       "Managed API returned an invalid default Transform");
		const Float32 updatedTransform[10]{1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.7071067f, 0.7071067f, 2.0f, 3.0f, 4.0f};
		Expect(api.setLocalTransform(api.sceneContext, child, updatedTransform, 10) == 0 &&
		           api.getLocalTransform(api.sceneContext, child, transform, 10) == 0 &&
		           std::equal(transform, transform + 10, updatedTransform),
		       "Managed API failed to round-trip a local Transform");

		const Float32 parentTransform[10]{10.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 2.f, 3.f, 4.f};
		Float32 worldPosition[3]{};
		Expect(api.setLocalTransform(api.sceneContext, parent, parentTransform, 10) == 0 &&
		           api.getWorldPosition(api.sceneContext, child, worldPosition, 3) == 0 && worldPosition[0] == 12.f &&
		           worldPosition[1] == 6.f && worldPosition[2] == 12.f,
		       "Managed world position did not include the parent transform");
		const Float32 desiredPosition[3]{20.f, 15.f, 24.f};
		Expect(api.setWorldPosition(api.sceneContext, child, desiredPosition, 3) == 0 &&
		           api.getLocalTransform(api.sceneContext, child, transform, 10) == 0 && transform[0] == 5.f &&
		           transform[1] == 5.f && transform[2] == 6.f,
		       "Managed world position assignment did not convert through the parent inverse");
		const ManagedHandle disposable = Create(api, "Disposable");
		Expect(api.destroyGameObject(api.sceneContext, disposable) == 0 &&
		           api.getWorldPosition(api.sceneContext, disposable, worldPosition, 3) != 0 &&
		           api.destroyGameObject(api.sceneContext, disposable) != 0,
		       "Managed destroy did not invalidate object access");

		UInt32 active = 1;
		Expect(api.setGameObjectActive(api.sceneContext, child, 0) == 0 &&
		           api.getGameObjectActive(api.sceneContext, child, &active) == 0 && active == 0,
		       "Managed API failed to update activeSelf");

		Scene otherScene;
		ManagedSceneApi otherApi(otherScene);
		NativeApiV5 otherNativeApi = otherApi.Bind({});
		const ManagedHandle foreign = Create(otherNativeApi, "Foreign");
		Expect(api.getGameObjectActive(api.sceneContext, foreign, &active) != 0,
		       "Managed API accepted a handle from another Scene session");
		const char foreignCameraType[] = "ursa.Camera";
		ManagedHandle foreignCamera{};
		Float32 foreignProjection[3]{};
		Expect(otherNativeApi.addComponent(otherNativeApi.sceneContext, foreign,
		                                   reinterpret_cast<const UInt8*>(foreignCameraType),
		                                   sizeof(foreignCameraType) - 1, &foreignCamera) == 0 &&
		           api.getCameraProjection(api.sceneContext, foreignCamera, foreignProjection, 3) != 0,
		       "Managed API accepted a Component handle from another Scene session");

		const char cameraType[] = "ursa.Camera";
		ManagedHandle camera{};
		Expect(api.addComponent(api.sceneContext, parent, reinterpret_cast<const UInt8*>(cameraType),
		                        sizeof(cameraType) - 1, &camera) == 0,
		       "Managed API failed to add a native Component");
		Float32 projection[3]{};
		Expect(api.getCameraProjection(api.sceneContext, camera, projection, 3) == 0 &&
		           std::abs(projection[0] - 0.78539816f) < 0.0001f && projection[1] == 0.1f && projection[2] == 100.0f,
		       "Managed Camera wrapper returned incorrect default projection data");
		const Float32 newProjection[3]{1.1f, 0.2f, 80.0f};
		Expect(api.setCameraProjection(api.sceneContext, camera, newProjection, 3) == 0 &&
		           api.getCameraProjection(api.sceneContext, camera, projection, 3) == 0 &&
		           std::equal(projection, projection + 3, newProjection),
		       "Managed Camera projection did not round-trip");
		const Float32 invalidProjection[3]{1.1f, 0.2f, 0.2f};
		Expect(api.setCameraProjection(api.sceneContext, camera, invalidProjection, 3) != 0 &&
		           api.getCameraProjection(api.sceneContext, camera, projection, 3) == 0 &&
		           std::equal(projection, projection + 3, newProjection),
		       "Invalid Camera projection changed the prior valid data");
		const auto handleJson = [](ManagedHandle value)
		{
			return nlohmann::json{{"sessionId", value.sessionId},
			                      {"index", value.index},
			                      {"generation", value.generation},
			                      {"kind", UInt32(value.kind)},
			                      {"reserved", 0}};
		};
		const auto request = [&](const nlohmann::json& value)
		{
			const auto text = value.dump();
			UInt64 size = 0;
			Expect(api.resourceRequest(api.sceneContext, reinterpret_cast<const UInt8*>(text.data()), text.size(),
			                           nullptr, 0, &size) == 4,
			       "Resource request did not return a response size");
			Vector<UInt8> bytes(size);
			Expect(api.resourceRequest(api.sceneContext, nullptr, 0, bytes.data(), size, &size) == 0,
			       "Resource request did not return its completed result");
			return nlohmann::json::parse(bytes.begin(), bytes.end());
		};
		Expect(!request({{"operation", "gameObject.layer.set"}, {"handle", handleJson(parent)}, {"value", 7}})
		               .contains("error") &&
		           request({{"operation", "gameObject.layer.get"}, {"handle", handleJson(parent)}})["value"] == 7,
		       "Managed layer setting did not round-trip");
		Expect(request({{"operation", "gameObject.layer.set"}, {"handle", handleJson(parent)}, {"value", 32}})
		           .contains("error"),
		       "Managed layer setting accepted an invalid value");
		auto settings = request({{"operation", "camera.settings.get"}, {"handle", handleJson(camera)}}).at("value");
		settings["depth"] = -2;
		settings["cullingMask"] = 1u << 7;
		settings["viewport"] = {0.0f, 0.0f, 0.5f, 1.0f};
		Expect(!request({{"operation", "camera.settings.set"}, {"handle", handleJson(camera)}, {"data", settings}})
		               .contains("error") &&
		           request({{"operation", "camera.settings.get"}, {"handle", handleJson(camera)}}).at("value") ==
		               settings,
		       "Managed camera rendering settings did not round-trip");
		auto badSettings = settings;
		badSettings["viewport"] = {0, 0, 2, 1};
		Expect(request({{"operation", "camera.settings.set"}, {"handle", handleJson(camera)}, {"data", badSettings}})
		               .contains("error") &&
		           request({{"operation", "camera.settings.get"}, {"handle", handleJson(camera)}}).at("value") ==
		               settings,
		       "Failed camera setting changed the valid configuration");
		Expect(request({{"operation", "camera.settings.get"}, {"handle", handleJson(foreignCamera)}}).contains("error"),
		       "Camera settings accepted a foreign-session component");
		UInt32 componentCount = 0;
		ManagedHandle listedComponent{};
		Expect(api.getComponentCount(api.sceneContext, parent, &componentCount) == 0 && componentCount == 1 &&
		           api.getComponentAt(api.sceneContext, parent, 0, &listedComponent) == 0 &&
		           SameHandle(camera, listedComponent),
		       "Managed API did not list the added Component");
		const auto componentIds = scene.ComponentsOf(scene.RootGameObjects().front());
		Expect(static_cast<bool>(componentIds), "Could not query the Scene Component IDs");
		const auto directComponentHandle = sceneApi.RegisterComponent(scene.GetComponent(componentIds.value.front()));
		Expect(static_cast<bool>(directComponentHandle) && SameHandle(camera, directComponentHandle.value),
		       "Managed API could not register an attached Component from its runtime pointer");
		UInt64 componentIdRequired = 0;
		Expect(api.getComponentPersistentId(api.sceneContext, camera, nullptr, 0, &componentIdRequired) == 4 &&
		           componentIdRequired == 36,
		       "Managed API did not report the Component persistent ID buffer size");
		Vector<UInt8> componentId(componentIdRequired);
		ManagedHandle resolvedComponent{};
		Expect(api.getComponentPersistentId(api.sceneContext, camera, componentId.data(), componentId.size(),
		                                    &componentIdRequired) == 0 &&
		           api.resolveComponentPersistentId(api.sceneContext, componentId.data(), componentId.size(),
		                                            &resolvedComponent) == 0 &&
		           SameHandle(camera, resolvedComponent),
		       "Managed API did not resolve a Component by persistent ID");
		required = 0;
		Expect(api.getComponentType(api.sceneContext, camera, nullptr, 0, &required) == 4 &&
		           required == sizeof(cameraType) - 1,
		       "Managed API did not report the Component type ID size");
		Vector<UInt8> cameraTypeBytes(required);
		Expect(
		    api.getComponentType(api.sceneContext, camera, cameraTypeBytes.data(), required, &required) == 0 &&
		        std::equal(cameraTypeBytes.begin(), cameraTypeBytes.end(), reinterpret_cast<const UInt8*>(cameraType)),
		    "Managed API returned an incorrect Component type ID");
		ManagedHandle componentOwner{};
		Expect(api.getComponentOwner(api.sceneContext, camera, &componentOwner) == 0 &&
		           SameHandle(parent, componentOwner),
		       "Managed API returned the wrong Component owner");
		UInt32 componentEnabled = 0;
		UInt32 componentActive = 0;
		Expect(api.getComponentEnabled(api.sceneContext, camera, &componentEnabled) == 0 && componentEnabled == 1 &&
		           api.setComponentEnabled(api.sceneContext, camera, 0) == 0 &&
		           api.getComponentEnabled(api.sceneContext, camera, &componentEnabled) == 0 && componentEnabled == 0 &&
		           api.isComponentActive(api.sceneContext, camera, &componentActive) == 0 && componentActive == 0,
		       "Managed API did not expose Component enabled/active state");
		Expect(api.removeComponent(api.sceneContext, camera) == 0 &&
		           api.getComponentEnabled(api.sceneContext, camera, &componentEnabled) != 0 &&
		           api.getCameraProjection(api.sceneContext, camera, projection, 3) != 0,
		       "Managed API accepted a removed Component handle");

		auto childObject = scene.RootGameObjects().front()->Children().front();
		auto rendererComponent = Take(Ursa::Test::AddRenderer(childObject, {0.2f, 0.4f, 0.8f, 1}));
		Done(rendererComponent->SetSharedMaterial(Take(resources->LoadMaterial(materialGuid))));
		ComponentRegistry componentRegistry;
		Done(RegisterBuiltinComponents(componentRegistry));
		const auto scenePath = Take(VirtualPath::Parse("prj:ManagedRenderer.ursascene"));
		Done(SaveSceneResource(resourceFiles, scenePath, ResourceId::Generate(), scene, componentRegistry));
		ComponentLoadContext sceneLoadContext(*resources);
		auto loadedScene = Take(LoadSceneResource(resourceFiles, scenePath, componentRegistry, sceneLoadContext));
		auto loadedChild = loadedScene.scene->RootGameObjects().front()->Children().front();
		auto* loadedRenderer = loadedChild->FindComponent<MeshRendererComponent>();
		auto* loadedFilter = loadedChild->FindComponent<MeshFilterComponent>();
		Expect(loadedRenderer && loadedRenderer->SharedMaterial() && loadedFilter && loadedFilter->SharedMesh(),
		       "Managed Scene API graph lost Renderer/Filter resource bindings on save/load");
		const auto prefabPath = Take(VirtualPath::Parse("prj:ManagedRenderer.ursaprefab"));
		Done(SavePrefabResource(resourceFiles, prefabPath, ResourceId::Generate(), scene, childObject,
		                        componentRegistry));
		ComponentLoadContext prefabLoadContext(*resources);
		auto loadedPrefab = Take(LoadPrefabResource(resourceFiles, prefabPath, componentRegistry, prefabLoadContext));
		auto prefabRoot = loadedPrefab.scene->RootGameObjects().front();
		Expect(prefabRoot->FindComponent<MeshFilterComponent>()->SharedMesh() &&
		           prefabRoot->FindComponent<MeshRendererComponent>()->SharedMaterial(),
		       "Renderer/Filter bindings did not survive Prefab roundtrip");

		FrameContext frame;
		frame.deltaSeconds = 0.02f;
		frame.fixedDeltaSeconds = 1.0f / 60.0f;
		frame.elapsedSeconds = 12.5;
		frame.fixedElapsedSeconds = 12.0;
		frame.frameIndex = 42;
		const Size keyIndex = static_cast<Size>(Key::W);
		frame.input.keys[keyIndex] = true;
		frame.input.keyPressed[keyIndex] = true;
		sceneApi.SetFrameContext(&frame);
		UInt32 inputState = 0;
		Expect(api.getInputState(api.sceneContext, static_cast<UInt32>(Key::W), 0, &inputState) == 0 &&
		           inputState == 1 &&
		           api.getInputState(api.sceneContext, static_cast<UInt32>(Key::W), 1, &inputState) == 0 &&
		           inputState == 1 &&
		           api.getInputState(api.sceneContext, static_cast<UInt32>(Key::W), 2, &inputState) == 0 &&
		           inputState == 0,
		       "Managed API did not preserve the frame input edge snapshot");
		Float32 delta = 0;
		Float32 fixedDelta = 0;
		Float64 time = 0;
		Float64 fixedTime = 0;
		UInt64 frameIndex = 0;
		Expect(api.getTimeState(api.sceneContext, &delta, &fixedDelta, &time, &fixedTime, &frameIndex) == 0 &&
		           delta == frame.deltaSeconds && fixedDelta == frame.fixedDeltaSeconds &&
		           time == frame.elapsedSeconds && fixedTime == frame.fixedElapsedSeconds &&
		           frameIndex == frame.frameIndex,
		       "Managed API did not expose the current frame timing");

		const auto roots = scene.RootGameObjects();
		Expect(roots.size() == 1, "Scene API fixture has an unexpected root count");
		Expect(static_cast<bool>(scene.DestroyGameObject(roots.front())), "Could not destroy the managed API fixture");
		Expect(api.getGameObjectActive(api.sceneContext, child, &active) != 0,
		       "Managed API accepted a handle after its GameObject was destroyed");
		auto renderCameraObject = Take(scene.CreateGameObject("Render Camera"));
		CameraComponent* renderCamera = Take(renderCameraObject->AddComponent<CameraComponent>());
		auto renderObject = Take(scene.CreateGameObject("Visible Mesh"));
		LocalTransform renderTransform;
		renderTransform.position = {0, 0, -3};
		Done(renderObject->SetLocalTransform(renderTransform));
		Take(Test::AddRenderer(renderObject));
		const auto beginRequest = [&](UInt64 frameId)
		{
			return ResourceRequest(api,
			                       {{"operation", "render.context.begin"}, {"frameId", frameId}, {"aspect", 1.0f}});
		};
		const auto cullRequest = [&](const nlohmann::json& renderContext)
		{
			return ResourceRequest(api, {{"operation", "render.cull"},
			                             {"context", renderContext.at("context")},
			                             {"cameraPersistentId", ToStdString(renderCamera->PersistentId().ToString())}});
		};
		const auto listRequest = [&](const nlohmann::json& renderContext, const nlohmann::json& culling)
		{
			return ResourceRequest(api, {{"operation", "render.rendererList.create"},
			                             {"context", renderContext.at("context")},
			                             {"cullingContext", culling.at("context")},
			                             {"cullingIndex", culling.at("index")},
			                             {"cullingGeneration", culling.at("generation")},
			                             {"shaderTags", {"SRPDefaultUnlit"}},
			                             {"sortingCriteria", UInt32(Rendering::SortingCriteria::CommonOpaque)},
			                             {"activePipeline", "UrsaForward"},
			                             {"minimumQueue", 0},
			                             {"maximumQueue", 5000},
			                             {"layerMask", 0xffffffffu}});
		};
		const auto firstContext = beginRequest(1);
		ResourceRequest(api, {{"operation", "render.camera.setup"},
		                      {"context", firstContext.at("context")},
		                      {"cameraPersistentId", ToStdString(renderCamera->PersistentId().ToString())}});
		const auto firstCulling = cullRequest(firstContext);
		const auto firstList = listRequest(firstContext, firstCulling);
		Expect(firstList.at("context").at("frameId") == firstContext.at("context").at("frameId") &&
		           firstList.at("generation").get<UInt32>() != 0,
		       "Managed rendering API did not create a frame-scoped RendererList");
		Done(ResourceRequest(api, {{"operation", "render.context.end"}, {"context", firstContext.at("context")}})
		             .is_null()
		         ? Result<void>{}
		         : Result<void>::Failure("Render context end returned unexpected data"));
		const auto secondContext = beginRequest(2);
		bool staleCullingRejected = false;
		try
		{
			listRequest(secondContext, firstCulling);
		}
		catch (const std::runtime_error&)
		{
			staleCullingRejected = true;
		}
		Expect(staleCullingRejected, "Managed rendering API accepted a previous-frame culling result");
		const auto secondCulling = cullRequest(secondContext);
		const auto secondList = listRequest(secondContext, secondCulling);
		Expect(secondList.at("generation").get<UInt32>() != 0,
		       "Managed rendering API did not create a RendererList for the current frame");
		ResourceRequest(api, {{"operation", "render.context.end"}, {"context", secondContext.at("context")}});
		for (const auto& root : scene.RootGameObjects())
		{
			Done(scene.DestroyGameObject(root));
		}
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
