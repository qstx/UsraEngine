#include <MeshFixtures.hpp>
#include <ComputeFixtures.hpp>
#include <Runtime.generated.hpp>
#include <Rendering.generated.hpp>
#include <Rendering.Commands.generated.hpp>
#include <BindingCalls.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SceneResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <new>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;
using namespace Ursa::Test::BindingCalls;
namespace B = Ursa::Scripting::Bindings;
namespace R = B::Runtime;
namespace G = B::Rendering;
namespace C = G::Commands;
namespace W = B::Wire;
using Handle = W::Value_Ursa_NativeHandle;
using Token = W::Value_Ursa_Rendering_RenderContextToken;
using RenderResult = W::Value_Ursa_Rendering_RenderResultValue;
using TextureDescription = W::Value_Ursa_Rendering_RenderTextureValue;

class ReusedComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "test.ReusedComponent";
	}

	static void* operator new(Size size)
	{
		if (used || size > storage.size())
		{
			throw std::bad_alloc();
		}
		used = true;
		return storage.data();
	}

	static void operator delete(void*) noexcept
	{
		used = false;
	}

private:
	alignas(64) inline static Array<UInt8, 1024> storage{};
	inline static bool used = false;
};

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

bool Same(const Handle& left, const Handle& right)
{
	return std::memcmp(&left, &right, sizeof(Handle)) == 0;
}

Handle Create(void* context, const String& name)
{
	const auto text = StringView(name);
	return Value(Invoke<Handle>(R::NativeRuntimeBindings_CreateGameObject, context, &text));
}

W::Value_Ursa_Rendering_RenderTargetValue Target(const Handle& handle)
{
	W::Value_Ursa_Rendering_RenderTargetValue value;
	value.F_Kind = 1;
	value.F_Texture = {1, handle};
	return value;
}

void RunDetached()
{
	Scene privateScene;
	auto root = GameObject::Create("Prefab root");
	auto camera = Take(root->AddComponent<CameraComponent>());
	ManagedSceneApi scope(privateScene);
	scope.SetDetachedRoots({root});
	const auto rootHandle = B::WriteManagedHandle<Handle>(Take(scope.Register(root)));
	const auto cameraHandle = B::WriteManagedHandle<Handle>(Take(scope.RegisterComponent(camera)));
	Expect(privateScene.RootGameObjects().empty(), "Exposing a Prefab inserted it into a Scene");
	const auto child = Create(scope.Token(), "New prefab child");
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetParent, scope.Token(), &child, &rootHandle));
	const W::Value_Ursa_LocalTransformValue transform{{1, 2, 3}, {0, 0, 0, 1}, {1, 1, 1}};
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetLocalTransform, scope.Token(), &child, &transform));
	Expect(root->Children().size() == 1 && Take(root->Children()[0]->GetLocalTransform()).position.x == 1 &&
	           privateScene.RootGameObjects().empty(),
	       "Managed Prefab edits escaped their detached graph");
	// Keep the identity bytes alive while invoking the generated binding.
	const String stableId = root->Children()[0]->Id().ToString();
	const auto stableView = StringView(stableId);
	Expect(Same(child, Value(Invoke<Handle>(R::NativeRuntimeBindings_ResolveGameObjectPersistentId, scope.Token(),
	                                        &stableView))),
	       "Detached Prefab IDs did not resolve");
	const String cameraId = camera->PersistentId().ToString();
	const auto cameraIdView = StringView(cameraId);
	Expect(Same(cameraHandle, Value(Invoke<Handle>(R::NativeRuntimeBindings_ResolveComponentPersistentId, scope.Token(),
	                                               &cameraIdView))),
	       "Detached component IDs did not resolve");
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetComponentEnabled, scope.Token(), &cameraHandle, UInt32{0}));
	Expect(!camera->IsEnabled() &&
	           !Value(Invoke<UInt32>(R::NativeRuntimeBindings_IsComponentActive, scope.Token(), &cameraHandle)),
	       "Detached component state was lost");
	const auto outside = GameObject::Create("Not exposed");
	Expect(!scope.Register(outside), "Managed Prefab scope accepted another asset's object");
	Complete(InvokeVoid(R::NativeRuntimeBindings_RemoveComponent, scope.Token(), &cameraHandle));
	Expect(!Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetComponentType, scope.Token(), &cameraHandle),
	       "Removed detached component handle was still usable");
	Complete(InvokeVoid(R::NativeRuntimeBindings_DestroyGameObject, scope.Token(), &child));
	Expect(root->Children().empty() && !Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetName, scope.Token(), &child),
	       "Destroyed detached object handle was still usable");
}

void Run()
{
	VirtualFileSystem resourceFiles;
	Done(resourceFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const ResourceId materialGuid = ResourceId::Generate();
	const auto materialPath = Take(VirtualPath::Parse("prj:managed-renderer.ursamat"));
	MaterialInstance material(MaterialType::Unlit);
	Done(SaveMaterialResource(resourceFiles, materialPath, materialGuid, material, {}));
	const ResourceId computeGuid = ResourceId::Generate();
	const auto computePath = Take(VirtualPath::Parse("prj:Fill.ursashaderasset"));
	auto computeFixture = Take(Test::CreateComputeFixture({URSA_TEST_DXC_EXECUTABLE, URSA_TEST_SPIRV_VAL_EXECUTABLE,
	                                                       URSA_TEST_SPIRV_CROSS_EXECUTABLE, URSA_SHADER_STAGING_ROOT},
	                                                      computeGuid));
	Done(Shader::SaveShaderAssetResource(resourceFiles, computePath, *computeFixture));
	auto resources =
	    Take(ResourceSession::Open(resourceFiles,
	                               [=](ResourceId guid, UInt64 localFileId) -> Result<VirtualPath>
	                               {
		                               if (guid == computeGuid && !localFileId)
		                               {
			                               return Result<VirtualPath>::Success(computePath);
		                               }
		                               return guid == materialGuid && !localFileId
		                                          ? Result<VirtualPath>::Success(materialPath)
		                                          : Result<VirtualPath>::Failure("Fixture asset is missing");
	                               }));
	String windowTitle;
	Scene scene;
	ManagedSceneApi sceneApi(scene, resources.get(), [&](const String& value) { windowTitle = value; });
	void* api = sceneApi.Token();
	for (const bool registerBeforeAccess : {false, true})
	{
		auto reuseOwner = Take(scene.CreateGameObject("Native component reuse"));
		auto* previous = Take(reuseOwner->AddComponent<ReusedComponent>());
		const auto previousHandle = B::WriteManagedHandle<Handle>(Take(sceneApi.RegisterComponent(previous)));
		Done(reuseOwner->RemoveComponent(previous));
		auto* replacement = Take(reuseOwner->AddComponent<ReusedComponent>());
		Expect(previous == replacement, "Component reuse fixture did not reuse its native address");
		Handle replacementHandle;
		if (registerBeforeAccess)
		{
			replacementHandle = B::WriteManagedHandle<Handle>(Take(sceneApi.RegisterComponent(replacement)));
		}
		Expect(!Invoke<UInt32>(R::NativeRuntimeBindings_GetComponentEnabled, api, &previousHandle),
		       "A stale component handle aliased a native replacement at the same address");
		if (!registerBeforeAccess)
		{
			replacementHandle = B::WriteManagedHandle<Handle>(Take(sceneApi.RegisterComponent(replacement)));
		}
		Expect(!Same(previousHandle, replacementHandle) &&
		           Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetComponentEnabled, api, &replacementHandle)),
		       "Native replacement did not receive a fresh valid handle");
		Done(scene.DestroyGameObject(reuseOwner));
	}
	auto device = Take(Rhi::CreateDevice({}));
	sceneApi.SetRenderDevice(device);
	const auto createTexture = [&](const TextureDescription& description)
	{ return Invoke<Handle>(G::NativeRenderingBindings_CreateRenderTexture, api, &description); };
	TextureDescription description{32, 16, 0, 32, 0, 1};
	auto texture = Value(createTexture(description));
	auto buffer =
	    Value(Invoke<Handle>(G::NativeRenderingBindings_CreateGraphicsBuffer, api, UInt32{4}, UInt32{32}, UInt32{16}));
	Expect(texture.F_kind == UInt32(ManagedHandleKind::RenderTexture) &&
	           buffer.F_kind == UInt32(ManagedHandleKind::GraphicsBuffer) &&
	           Value(Invoke<UInt32>(R::NativeResourceBindings_IsValid, api, &texture)) &&
	           Value(Invoke<UInt32>(R::NativeResourceBindings_IsValid, api, &buffer)),
	       "Typed RHI resource creation failed");
	Complete(InvokeVoid(G::NativeRenderingBindings_ReleaseRenderTexture, api, &texture));
	Complete(InvokeVoid(G::NativeRenderingBindings_ReleaseGraphicsBuffer, api, &buffer));
	Expect(!Value(Invoke<UInt32>(R::NativeResourceBindings_IsValid, api, &texture)) &&
	           !Value(Invoke<UInt32>(R::NativeResourceBindings_IsValid, api, &buffer)),
	       "Released RHI handles remained valid");
	description.F_Width = device->Capabilities().maxTextureSize + 1;
	Expect(!createTexture(description), "RenderTexture exceeded device capacity");
	const UInt32 excessiveCount = static_cast<UInt32>(device->Capabilities().maxStorageBufferRange / 16 + 1);
	Expect(!Invoke<Handle>(G::NativeRenderingBindings_CreateGraphicsBuffer, api, UInt32{4}, excessiveCount, UInt32{16}),
	       "GraphicsBuffer exceeded device capacity");
	const auto begin = [&](UInt64 frame)
	{ return Value(Invoke<Token>(G::NativeRenderingBindings_Begin, api, frame, 1.f)); };
	const auto submitBatch = [&](const Token& token, const Vector<UInt8>& bytes)
	{
		const B::DataView data{bytes.data(), bytes.size(), 1, 0};
		const auto name = StringView("");
		return InvokeVoid(G::NativeRenderingBindings_EnqueueBatch, api, &token, &data, &name);
	};
	const auto end = [&](const Token& token)
	{
		auto result = InvokeVoid(G::NativeRenderingBindings_End, api, &token);
		if (!result)
		{
			throw std::runtime_error("End render frame " + std::to_string(token.F_frameId) + ": " +
			                         ToStdString(result.Error()));
		}
	};
	B::BindingCommandWriter commands(C::hash);
	const auto emptyBatch = commands.Bytes();
	const auto firstToken = begin(77);
	Complete(submitBatch(firstToken, emptyBatch));
	Expect(sceneApi.AcceptedRenderBatchCount() == 1, "Typed render batch was not accepted once");
	auto stale = firstToken;
	++stale.F_generation;
	auto foreignSession = firstToken;
	++foreignSession.F_sessionId;
	auto malformed = emptyBatch;
	malformed[0] = 0xff;
	Expect(!submitBatch(stale, emptyBatch) && !submitBatch(foreignSession, emptyBatch) &&
	           !submitBatch(firstToken, malformed) && sceneApi.AcceptedRenderBatchCount() == 1,
	       "Invalid frame identity or malformed batch was accepted");
	bool workerRejected = false;
	std::thread worker([&] { workerRejected = !submitBatch(firstToken, emptyBatch); });
	worker.join();
	Expect(workerRejected && sceneApi.AcceptedRenderBatchCount() == 1, "Worker thread submitted a render batch");
	Complete(InvokeVoid(G::NativeRenderingBindings_Submit, api, &firstToken));
	end(firstToken);

	const auto parent = Create(api, "父节点");
	const auto child = Create(api, "Child");
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetParent, api, &child, &parent));
	const auto actualParent = Value(Invoke<Handle>(R::NativeRuntimeBindings_GetParent, api, &child));
	Expect(Same(actualParent, parent), "GameObject parent changed");
	const String renamed = "子节点-雪";
	const auto renamedView = StringView(renamed);
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetName, api, &child, &renamedView));
	Expect(Text(Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetName, api, &child)) == renamed, "UTF-8 name changed");
	const auto persistentId =
	    Text(Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetGameObjectPersistentId, api, &child));
	const auto persistentView = StringView(persistentId);
	Expect(persistentId.size() == 36 &&
	           Same(child, Value(Invoke<Handle>(R::NativeRuntimeBindings_ResolveGameObjectPersistentId, api,
	                                            &persistentView))),
	       "Persistent GameObject identity did not resolve");
	using Transform = W::Value_Ursa_LocalTransformValue;
	auto transform = Value(Invoke<Transform>(R::NativeRuntimeBindings_GetLocalTransform, api, &child));
	Expect(transform.F_Rotation.F_W == 1 && transform.F_Scale.F_X == 1 && transform.F_Scale.F_Y == 1 &&
	           transform.F_Scale.F_Z == 1,
	       "Default transform changed");
	Transform updated{{1, 2, 3}, {0, 0, 0.7071067f, 0.7071067f}, {2, 3, 4}};
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetLocalTransform, api, &child, &updated));
	transform = Value(Invoke<Transform>(R::NativeRuntimeBindings_GetLocalTransform, api, &child));
	Expect(std::memcmp(&updated, &transform, sizeof(updated)) == 0, "Local transform did not round-trip");
	const Transform parentTransform{{10, 0, 0}, {0, 0, 0, 1}, {2, 3, 4}};
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetLocalTransform, api, &parent, &parentTransform));
	const auto world = Value(Invoke<W::Value_Ursa_Vector3>(R::NativeRuntimeBindings_GetWorldPosition, api, &child));
	Expect(world.F_X == 12 && world.F_Y == 6 && world.F_Z == 12, "World position lost the parent transform");
	const W::Value_Ursa_Vector3 desired{20, 15, 24};
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetWorldPosition, api, &child, &desired));
	transform = Value(Invoke<Transform>(R::NativeRuntimeBindings_GetLocalTransform, api, &child));
	Expect(transform.F_Position.F_X == 5 && transform.F_Position.F_Y == 5 && transform.F_Position.F_Z == 6,
	       "World assignment did not use the parent inverse");
	const auto disposable = Create(api, "Disposable");
	Complete(InvokeVoid(R::NativeRuntimeBindings_DestroyGameObject, api, &disposable));
	Expect(!Invoke<W::Value_Ursa_Vector3>(R::NativeRuntimeBindings_GetWorldPosition, api, &disposable) &&
	           !InvokeVoid(R::NativeRuntimeBindings_DestroyGameObject, api, &disposable),
	       "Destroyed GameObject remained usable");
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetActive, api, &child, UInt32{0}));
	Expect(!Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetActive, api, &child)), "ActiveSelf assignment changed");
	Scene otherScene;
	ManagedSceneApi otherApi(otherScene);
	const auto foreign = Create(otherApi.Token(), "Foreign");
	Expect(!Invoke<UInt32>(R::NativeRuntimeBindings_GetActive, api, &foreign),
	       "Foreign GameObject handle was accepted");
	const String cameraType = "ursa.Camera";
	const auto cameraTypeView = StringView(cameraType);
	const auto foreignCamera =
	    Value(Invoke<Handle>(R::NativeRuntimeBindings_AddComponent, otherApi.Token(), &foreign, &cameraTypeView));
	using Projection = W::Value_Ursa_CameraProjection;
	Expect(!Invoke<Projection>(R::NativeRuntimeBindings_GetCameraProjection, api, &foreignCamera),
	       "Foreign Camera projection was accepted");
	const auto camera = Value(Invoke<Handle>(R::NativeRuntimeBindings_AddComponent, api, &parent, &cameraTypeView));
	auto projection = Value(Invoke<Projection>(R::NativeRuntimeBindings_GetCameraProjection, api, &camera));
	Expect(std::abs(projection.F_VerticalFovRadians - 0.78539816f) < 0.0001f && projection.F_NearPlane == 0.1f &&
	           projection.F_FarPlane == 100.f,
	       "Default Camera projection changed");
	const Projection desiredProjection{1.1f, 0.2f, 80.f, 0, 5.f};
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetCameraProjection, api, &camera, &desiredProjection));
	const Projection invalidProjection{1.1f, 0.2f, 0.2f, 0, 5.f};
	Expect(!InvokeVoid(R::NativeRuntimeBindings_SetCameraProjection, api, &camera, &invalidProjection),
	       "Invalid Camera projection was accepted");
	projection = Value(Invoke<Projection>(R::NativeRuntimeBindings_GetCameraProjection, api, &camera));
	Expect(std::memcmp(&projection, &desiredProjection, sizeof(projection)) == 0,
	       "Invalid projection changed prior values");
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetLayer, api, &parent, Int32{7}));
	Expect(Value(Invoke<Int32>(R::NativeRuntimeBindings_GetLayer, api, &parent)) == 7 &&
	           !InvokeVoid(R::NativeRuntimeBindings_SetLayer, api, &parent, Int32{32}),
	       "Layer range or round-trip changed");
	using CameraSettings = W::Value_Ursa_CameraRenderingSettings;
	auto settings = Value(Invoke<CameraSettings>(R::NativeRuntimeBindings_GetCameraRenderingSettings, api, &camera));
	settings.F_Depth = -2;
	settings.F_CullingMask = 1u << 7;
	settings.F_Viewport = {0, 0, 0.5f, 1};
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetCameraRenderingSettings, api, &camera, &settings));
	auto badSettings = settings;
	badSettings.F_Viewport.F_Width = 2;
	Expect(!InvokeVoid(R::NativeRuntimeBindings_SetCameraRenderingSettings, api, &camera, &badSettings) &&
	           !Invoke<CameraSettings>(R::NativeRuntimeBindings_GetCameraRenderingSettings, api, &foreignCamera),
	       "Invalid or foreign Camera settings were accepted");
	const auto retainedSettings =
	    Value(Invoke<CameraSettings>(R::NativeRuntimeBindings_GetCameraRenderingSettings, api, &camera));
	Expect(retainedSettings.F_Depth == -2 && retainedSettings.F_CullingMask == (1u << 7) &&
	           retainedSettings.F_Viewport.F_Width == 0.5f,
	       "Invalid Camera settings changed prior values");
	auto components = Invoke<B::DataView>(R::NativeRuntimeBindings_GetComponents, api, &parent);
	Expect(components && components.value.length == 1 &&
	           Same(static_cast<const Handle*>(components.value.data)[0], camera),
	       "Component snapshot changed");
	auto* nativeCamera =
	    Take(scene.GameObjectById(Take(ResourceId::Parse(
	             Text(Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetGameObjectPersistentId, api, &parent))))))
	        ->FindComponent<CameraComponent>();
	Expect(Same(B::WriteManagedHandle<Handle>(Take(sceneApi.RegisterComponent(nativeCamera))), camera),
	       "RegisterComponent did not preserve identity");
	const auto componentId = Text(Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetComponentPersistentId, api, &camera));
	const auto componentView = StringView(componentId);
	Expect(componentId.size() == 36 &&
	           Same(camera, Value(Invoke<Handle>(R::NativeRuntimeBindings_ResolveComponentPersistentId, api,
	                                             &componentView))) &&
	           Text(Invoke<B::Utf8View>(R::NativeRuntimeBindings_GetComponentType, api, &camera)) == cameraType &&
	           Same(Value(Invoke<Handle>(R::NativeRuntimeBindings_GetComponentOwner, api, &camera)), parent),
	       "Component persistent identity, type or owner changed");
	Expect(Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetComponentEnabled, api, &camera)) == 1,
	       "Default component enable changed");
	Complete(InvokeVoid(R::NativeRuntimeBindings_SetComponentEnabled, api, &camera, UInt32{0}));
	Expect(!Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetComponentEnabled, api, &camera)) &&
	           !Value(Invoke<UInt32>(R::NativeRuntimeBindings_IsComponentActive, api, &camera)),
	       "Component enable/activity rules changed");
	Complete(InvokeVoid(R::NativeRuntimeBindings_RemoveComponent, api, &camera));
	Expect(!Invoke<UInt32>(R::NativeRuntimeBindings_GetComponentEnabled, api, &camera) &&
	           !Invoke<Projection>(R::NativeRuntimeBindings_GetCameraProjection, api, &camera),
	       "Removed component handle remained valid");

	auto childObject = Take(scene.GameObjectById(Take(ResourceId::Parse(persistentId))));
	auto* renderer = Take(Test::AddRenderer(childObject, {0.2f, 0.4f, 0.8f, 1}));
	Done(renderer->SetSharedMaterial(Take(resources->LoadMaterial(materialGuid))));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	const auto scenePath = Take(VirtualPath::Parse("prj:ManagedRenderer.ursascene"));
	Done(SaveSceneResource(resourceFiles, scenePath, ResourceId::Generate(), scene, registry));
	ComponentLoadContext sceneLoadContext(*resources);
	auto loadedScene = Take(LoadSceneResource(resourceFiles, scenePath, registry, sceneLoadContext));
	auto loadedChild = loadedScene.scene->RootGameObjects().front()->Children().front();
	Expect(loadedChild->FindComponent<MeshRendererComponent>()->SharedMaterial() &&
	           loadedChild->FindComponent<MeshFilterComponent>()->SharedMesh(),
	       "Renderer/Filter bindings were lost on Scene save/load");
	const auto prefabPath = Take(VirtualPath::Parse("prj:ManagedRenderer.ursaprefab"));
	Done(SavePrefabResource(resourceFiles, prefabPath, ResourceId::Generate(), scene, childObject, registry));
	ComponentLoadContext prefabLoadContext(*resources);
	auto loadedPrefab = Take(LoadPrefabResource(resourceFiles, prefabPath, registry, prefabLoadContext));
	const auto prefabRoot = loadedPrefab.scene->RootGameObjects().front();
	Expect(prefabRoot->FindComponent<MeshFilterComponent>()->SharedMesh() &&
	           prefabRoot->FindComponent<MeshRendererComponent>()->SharedMaterial(),
	       "Renderer/Filter bindings were lost on Prefab save/load");
	FrameContext frame;
	frame.deltaSeconds = 0.02f;
	frame.fixedDeltaSeconds = 1.f / 60;
	frame.elapsedSeconds = 12.5;
	frame.fixedElapsedSeconds = 12;
	frame.frameIndex = 42;
	frame.input.keys[Size(Key::W)] = true;
	frame.input.keyPressed[Size(Key::W)] = true;
	sceneApi.SetFrameContext(&frame);
	Expect(Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetKey, api, UInt32(Key::W), UInt32{0})) &&
	           Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetKey, api, UInt32(Key::W), UInt32{1})) &&
	           !Value(Invoke<UInt32>(R::NativeRuntimeBindings_GetKey, api, UInt32(Key::W), UInt32{2})),
	       "Frame input edge snapshot changed");
	const auto timing = Value(Invoke<W::Value_Ursa_TimeState>(R::NativeRuntimeBindings_GetTime, api));
	Expect(timing.F_DeltaTime == frame.deltaSeconds && timing.F_FixedDeltaTime == frame.fixedDeltaSeconds &&
	           timing.F_ElapsedTime == frame.elapsedSeconds && timing.F_FixedElapsedTime == frame.fixedElapsedSeconds &&
	           timing.F_FrameCount == frame.frameIndex,
	       "Frame timing changed");
	for (const auto& root : scene.RootGameObjects())
	{
		Done(scene.DestroyGameObject(root));
	}
	Expect(!Invoke<UInt32>(R::NativeRuntimeBindings_GetActive, api, &child), "Destroyed graph handle remained valid");

	auto cameraObject = Take(scene.CreateGameObject("Render Camera"));
	auto* renderCamera = Take(cameraObject->AddComponent<CameraComponent>());
	auto renderObject = Take(scene.CreateGameObject("Visible Mesh"));
	LocalTransform renderTransform;
	renderTransform.position = {0, 0, -3};
	Done(renderObject->SetLocalTransform(renderTransform));
	Take(Test::AddRenderer(renderObject));
	const String cameraIdentity = renderCamera->PersistentId().ToString();
	const auto cameraIdentityView = StringView(cameraIdentity);
	const auto cull = [&](const Token& token)
	{ return Value(Invoke<RenderResult>(G::NativeRenderingBindings_Cull, api, &token, &cameraIdentityView)); };
	const auto wireMatrix = [](const Mat4& matrix)
	{
		W::Value_Ursa_Matrix4x4 value;
		value.F_M00 = matrix.At(0, 0);
		value.F_M10 = matrix.At(1, 0);
		value.F_M20 = matrix.At(2, 0);
		value.F_M30 = matrix.At(3, 0);
		value.F_M01 = matrix.At(0, 1);
		value.F_M11 = matrix.At(1, 1);
		value.F_M21 = matrix.At(2, 1);
		value.F_M31 = matrix.At(3, 1);
		value.F_M02 = matrix.At(0, 2);
		value.F_M12 = matrix.At(1, 2);
		value.F_M22 = matrix.At(2, 2);
		value.F_M32 = matrix.At(3, 2);
		value.F_M03 = matrix.At(0, 3);
		value.F_M13 = matrix.At(1, 3);
		value.F_M23 = matrix.At(2, 3);
		value.F_M33 = matrix.At(3, 3);
		return value;
	};
	const B::Utf8View tags[]{StringView("SRPDefaultUnlit")};
	W::Value_Ursa_Rendering_DrawingSettingsValue drawing;
	drawing.F_ShaderTags = {tags, 1, 1, 0};
	drawing.F_Sorting = UInt32(Rendering::SortingCriteria::CommonOpaque);
	drawing.F_ActivePipeline = StringView("UrsaForward");
	const W::Value_Ursa_Rendering_FilteringSettings filtering{1, 0, 5000, 0xffffffffu};
	const auto list = [&](const Token& token, const RenderResult& culling)
	{
		return Invoke<RenderResult>(G::NativeRenderingBindings_CreateRendererList, api, &token, &culling, &drawing,
		                            &filtering);
	};
	const auto first = begin(1);
	(void)Value(Invoke<W::Value_Ursa_Rendering_CameraFrameValue>(G::NativeRenderingBindings_SetupCamera, api, &first,
	                                                             &cameraIdentityView));
	const auto firstCulling = cull(first);
	const auto firstList = Value(list(first, firstCulling));
	Expect(firstList.F_Context.F_frameId == first.F_frameId && firstList.F_Generation != 0,
	       "RendererList lost frame identity");
	const auto shadowViewProjection = wireMatrix(Mat4::Perspective(1.0f, 1.0f, 0.1f, 100.0f));
	const auto shadowCulling = Value(Invoke<RenderResult>(G::NativeRenderingBindings_CullShadowCasters, api, &first,
	                                                      &cameraIdentityView, &shadowViewProjection));
	Expect(Value(list(first, shadowCulling)).F_Generation != 0,
	       "Shadow caster culling did not return a usable same-frame RendererList");
	Complete(InvokeVoid(G::NativeRenderingBindings_Submit, api, &first));
	end(first);
	const auto second = begin(2);
	Expect(!list(second, firstCulling), "Previous-frame culling was accepted");
	Expect(Value(list(second, cull(second))).F_Generation != 0, "Current-frame RendererList failed");
	Complete(InvokeVoid(G::NativeRenderingBindings_Submit, api, &second));
	end(second);

	description = {8, 8, 0, 0, 0, 1};
	auto sourceTarget = Value(createTexture(description));
	auto destination = Value(createTexture(description));
	const auto execution = begin(3);
	commands.Clear();
	const Array<W::Value_Ursa_Rendering_RenderTargetValue, 2> colors{Target(sourceTarget), Target(destination)};
	C::Record_RenderCommands_SetRenderTargets(commands, {{colors.data(), colors.size(), 1, 0}, {}});
	C::Record_RenderCommands_ClearTarget(commands, {1, {0.25f, 0.5f, 1, 1}, 1, 0});
	C::Record_RenderCommands_CopyTexture(commands, {Target(sourceTarget), Target(destination)});
	auto executionBytes = commands.Bytes();
	Complete(submitBatch(execution, executionBytes));
	executionBytes.clear();
	Complete(InvokeVoid(G::NativeRenderingBindings_ReleaseRenderTexture, api, &sourceTarget));
	Complete(InvokeVoid(G::NativeRenderingBindings_Submit, api, &execution));
	end(execution);
	const auto pixels = Bytes(Invoke<B::DataView>(G::NativeRenderingBindings_ReadRenderTexture, api, &destination));
	Expect(pixels.size() == 8u * 8u * 4u && pixels[0] >= 62 && pixels[0] <= 65 && pixels[1] >= 126 &&
	           pixels[1] <= 129 && pixels[2] == 255 && pixels[3] == 255,
	       "Batch resource snapshots or clear/copy GPU pixels changed");
	Complete(InvokeVoid(G::NativeRenderingBindings_ReleaseRenderTexture, api, &destination));
	const String computeAssetGuid = computeGuid.ToString();
	const W::Value_Ursa_NativeAssetReference computeAsset{StringView(computeAssetGuid), 0};
	const auto kernelName = StringView("Fill");
	const auto kernel =
	    Value(Invoke<Int32>(R::NativeResourceBindings_FindComputeKernel, api, &computeAsset, &kernelName));
	const auto group = Value(Invoke<W::Value_Ursa_ComputeThreadGroupSize>(
	    R::NativeResourceBindings_GetComputeThreadGroupSize, api, &computeAsset, kernel));
	Expect(kernel == 0 && group.F_X == 8 && group.F_Y == 8 && group.F_Z == 1,
	       "Imported Compute kernel metadata changed");
	description.F_EnableRandomWrite = 1;
	auto computeTarget = Value(createTexture(description));
	auto computeBuffer =
	    Value(Invoke<Handle>(G::NativeRenderingBindings_CreateGraphicsBuffer, api, UInt32{4}, UInt32{64}, UInt32{4}));
	const auto compute = begin(4);
	commands.Clear();
	C::Record_RenderCommands_SetComputeTexture(commands,
	                                           {computeAsset, 0, StringView("_Output"), Target(computeTarget)});
	C::Record_RenderCommands_SetComputeBuffer(commands, {computeAsset, 0, StringView("_Data"), computeBuffer});
	C::Record_RenderCommands_SetComputeVector(commands,
	                                          {computeAsset, 0, StringView("_Color"), {0.5f, 0.25f, 0.75f, 1}});
	C::Record_RenderCommands_SetComputeInt(commands, {computeAsset, 0, StringView("_Bias"), 31});
	C::Record_RenderCommands_DispatchCompute(commands, {computeAsset, 0, 1, 1, 1});
	Complete(submitBatch(compute, commands.Bytes()));
	Complete(InvokeVoid(G::NativeRenderingBindings_Submit, api, &compute));
	end(compute);
	const auto computePixels =
	    Bytes(Invoke<B::DataView>(G::NativeRenderingBindings_ReadRenderTexture, api, &computeTarget));
	Expect(computePixels[0] >= 126 && computePixels[0] <= 129 && computePixels[1] >= 62 && computePixels[1] <= 65 &&
	           computePixels[2] >= 190 && computePixels[2] <= 193,
	       "Compute texture parameters did not reach the GPU");
	const auto computeBytes = Bytes(Invoke<B::DataView>(G::NativeRenderingBindings_ReadGraphicsBuffer, api,
	                                                    &computeBuffer, UInt64{0}, UInt64{256}));
	for (UInt32 index = 0; index < 64; ++index)
	{
		UInt32 word = 0;
		std::memcpy(&word, computeBytes.data() + Size(index) * 4, sizeof(word));
		Expect(word == index + 31, "Compute buffer parameters or owned readback changed");
	}
	Complete(InvokeVoid(G::NativeRenderingBindings_ReleaseRenderTexture, api, &computeTarget));
	Complete(InvokeVoid(G::NativeRenderingBindings_ReleaseGraphicsBuffer, api, &computeBuffer));
	auto sceneOutput = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
	sceneApi.SetRenderOutput(sceneOutput);
	const auto draw = begin(5);
	(void)Value(Invoke<W::Value_Ursa_Rendering_CameraFrameValue>(G::NativeRenderingBindings_SetupCamera, api, &draw,
	                                                             &cameraIdentityView));
	const auto drawList = Value(list(draw, cull(draw)));
	commands.Clear();
	C::Record_RenderCommands_ClearTarget(commands, {3, {0, 0, 0, 1}, 1, 0});
	C::Record_RenderCommands_DrawRendererList(commands,
	                                          {{drawList.F_Context, drawList.F_Index, drawList.F_Generation}});
	Complete(submitBatch(draw, commands.Bytes()));
	renderTransform.position.x = 1000;
	Done(renderObject->SetLocalTransform(renderTransform));
	Complete(InvokeVoid(G::NativeRenderingBindings_Submit, api, &draw));
	end(draw);
	const auto scenePixels = Take(Rendering::ReadRenderTexture(*device, *sceneOutput));
	const Size center = (32 * 64 + 32) * 4;
	Expect(scenePixels[center] > 220 && scenePixels[center + 1] > 220 && scenePixels[center + 2] > 220,
	       "RendererList lost captured geometry or used a post-enqueue transform");
	sceneApi.SetRenderOutput({});
	for (const auto& root : scene.RootGameObjects())
	{
		Done(scene.DestroyGameObject(root));
	}
}
} // namespace

int main()
{
	try
	{
		RunDetached();
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
