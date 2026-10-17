#include <Runtime.generated.hpp>
#include <Rendering.generated.hpp>
#include <ManagedHost.generated.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Mesh/Primitive.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>
#include <future>
#include <cmath>
#include <limits>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class TestBindingHost final : public Ursa::Scripting::Bindings::BindingContext,
                              public Ursa::Scripting::Bindings::BindingHostServices
{
public:
	TestBindingHost()
	{
		Ursa::Scripting::Bindings::CompleteBindingResult(
		    files.MountProvider("test", std::make_shared<Ursa::MemoryFileProvider>(), Ursa::MountAccess::ReadWrite));
	}

	~TestBindingHost() override
	{
		Deactivate();
	}

	Ursa::Result<void> WriteScriptLog(Ursa::UInt32 level, const Ursa::String& message) override
	{
		logLevel = level;
		log = message;
		return {};
	}

	Ursa::Result<void> ReportHostMessage(Ursa::UInt32 level, const Ursa::String& message) override
	{
		return WriteScriptLog(level, message);
	}

	Ursa::Result<Ursa::Vector<Ursa::UInt8>> ReadScriptFile(const Ursa::String& path) override
	{
		auto parsed = Ursa::VirtualPath::Parse(path);
		return parsed ? files.Read(parsed.value) : Ursa::Result<Ursa::Vector<Ursa::UInt8>>::Failure(parsed.error);
	}

	Ursa::Result<void> WriteScriptFile(const Ursa::String& path, const Ursa::Vector<Ursa::UInt8>& bytes) override
	{
		auto parsed = Ursa::VirtualPath::Parse(path);
		return parsed ? files.WriteAtomic(parsed.value, bytes) : Ursa::Result<void>::Failure(parsed.error);
	}

	Ursa::String log;
	Ursa::UInt32 logLevel = 0;

private:
	Ursa::VirtualFileSystem files;
};

void CheckPlatformBindings()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	namespace B = Bindings;
	TestBindingHost host;
	Scene scene;
	String windowTitle;
	ManagedSceneApi api(scene, nullptr, [&](const String& value) { windowTitle = value; });
	B::CompleteBindingResult(api.BindHostServices(host.Token()));
	const auto& table = *static_cast<const B::Runtime::Table*>(B::Runtime::NativeDescriptor().functions);
	String text("日志与窗口");
	B::Utf8View message{reinterpret_cast<const UInt8*>(text.data()), text.size(), 1, 0};
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_WriteLog(api.Token(), 3, &message, &result.outcome);
		Expect(bool(result) && host.log == text && host.logLevel == 3, "Typed Script logging lost Unicode or severity");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetWindowTitle(api.Token(), &message, &result.outcome);
		Expect(bool(result) && windowTitle == text, "Typed window title did not reach its callback");
	}
	String pathText("test:文件.bin");
	B::Utf8View path{reinterpret_cast<const UInt8*>(pathText.data()), pathText.size(), 1, 0};
	UInt8 bytes[]{1, 3, 0, 255};
	B::DataView data{bytes, std::size(bytes), 1, 0};
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_WriteFile(api.Token(), &path, &data, &result.outcome);
		Expect(bool(result), "Typed VFS write failed");
	}
	{
		B::CallbackResult<B::DataView> result;
		result.status = table.NativeRuntimeBindings_ReadFile(api.Token(), &path, &result.value, &result.outcome);
		Expect(bool(result) && result.value.length == std::size(bytes) &&
		           std::memcmp(result.value.data, bytes, sizeof(bytes)) == 0,
		       "Typed VFS read did not preserve raw bytes");
		B::DataView empty{nullptr, 0, 1, 0};
		B::CallbackResult<void> cleared;
		cleared.status = table.NativeRuntimeBindings_WriteFile(api.Token(), &path, &empty, &cleared.outcome);
		Expect(bool(cleared) && std::memcmp(result.value.data, bytes, sizeof(bytes)) == 0,
		       "Later VFS writes invalidated owned output");
		B::CallbackResult<B::DataView> readEmpty;
		readEmpty.status =
		    table.NativeRuntimeBindings_ReadFile(api.Token(), &path, &readEmpty.value, &readEmpty.outcome);
		Expect(bool(readEmpty) && readEmpty.value.present == 1 && readEmpty.value.length == 0,
		       "Typed empty VFS file became null or failed");
	}
	FrameContext frame;
	frame.deltaSeconds = 0.016f;
	frame.fixedDeltaSeconds = 0.02f;
	frame.elapsedSeconds = 10.5;
	frame.fixedElapsedSeconds = 10.4;
	frame.frameIndex = 9007199254740993ull;
	frame.input.keys[Size(Key::W)] = true;
	frame.input.keyPressed[Size(Key::W)] = true;
	api.SetFrameContext(&frame);
	for (UInt32 phase = 0; phase < 3; ++phase)
	{
		B::CallbackResult<UInt32> result;
		result.status =
		    table.NativeRuntimeBindings_GetKey(api.Token(), UInt32(Key::W), phase, &result.value, &result.outcome);
		Expect(bool(result) && result.value == (phase != 2 ? 1u : 0u),
		       "Typed Input phase differs from the frame snapshot");
	}
	{
		B::CallbackResult<B::Wire::Value_Ursa_TimeState> result;
		result.status = table.NativeRuntimeBindings_GetTime(api.Token(), &result.value, &result.outcome);
		Expect(bool(result) && result.value.F_DeltaTime == frame.deltaSeconds &&
		           result.value.F_FrameCount == frame.frameIndex && result.value.F_ElapsedTime == 10.5,
		       "Typed Time lost units or 64-bit frame precision");
	}
	api.SetFrameContext(nullptr);
	{
		B::CallbackResult<B::Wire::Value_Ursa_TimeState> result;
		result.status = table.NativeRuntimeBindings_GetTime(api.Token(), &result.value, &result.outcome);
		Expect(!result, "Typed Time accepted a frame after its scope ended");
	}
	host.Deactivate();
	{
		B::CallbackResult<B::DataView> result;
		result.status = table.NativeRuntimeBindings_ReadFile(api.Token(), &path, &result.value, &result.outcome);
		Expect(!result, "Typed Runtime service used a closed host through a live Scene");
	}
}

void CheckGameObjectBindings(Ursa::Scripting::ManagedSceneApi& sceneApi, Ursa::Scene& scene,
                             const Ursa::Scripting::ManagedHandle& parentHandle)
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	namespace B = Bindings;
	using Handle = B::Wire::Value_Ursa_NativeHandle;
	const auto& table = *static_cast<const B::Runtime::Table*>(B::Runtime::NativeDescriptor().functions);
	void* context = sceneApi.Token();
	String text("中文\0name", 11);
	B::Utf8View input{reinterpret_cast<const UInt8*>(text.data()), text.size(), 1, 0};
	Handle child;
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_CreateGameObject(context, &input, &child, &result.outcome);
		Expect(bool(result) && scene.RootGameObjects().size() == 2,
		       "Typed GameObject creation failed or executed twice");
	}
	{
		B::CallbackResult<B::Utf8View> result;
		result.status = table.NativeRuntimeBindings_GetName(context, &child, &result.value, &result.outcome);
		Expect(bool(result) && B::ReadString(result.value) == text,
		       "Typed GameObject name lost UTF-8 or embedded zero");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetLayer(context, &child, 31, &result.outcome);
		Expect(bool(result), "Typed Layer setter failed");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetLayer(context, &child, 32, &result.outcome);
		Expect(!result, "Invalid typed Layer was accepted");
	}
	{
		B::CallbackResult<Int32> result;
		result.status = table.NativeRuntimeBindings_GetLayer(context, &child, &result.value, &result.outcome);
		Expect(bool(result) && result.value == 31, "Failed Layer edit changed the object");
	}
	Handle parent = B::WriteManagedHandle<Handle>(parentHandle);
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetParent(context, &child, &parent, &result.outcome);
		Expect(bool(result), "Typed parent setter failed");
	}
	{
		B::CallbackResult<Handle> result;
		result.status = table.NativeRuntimeBindings_GetParent(context, &child, &result.value, &result.outcome);
		Expect(bool(result) && result.value.F_index == parent.F_index, "Typed parent getter differs");
	}
	{
		B::Wire::Value_Ursa_LocalTransformValue transform;
		transform.F_Position = {10, 20, 30};
		transform.F_Rotation = {0, 0, 0, 1};
		transform.F_Scale = {2, 3, 4};
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetLocalTransform(context, &parent, &transform, &result.outcome);
		Expect(bool(result), "Typed local transform failed");
	}
	{
		B::Wire::Value_Ursa_Vector3 position{14, 29, 46};
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetWorldPosition(context, &child, &position, &result.outcome);
		Expect(bool(result), "Typed parent-space world position assignment failed");
	}
	{
		B::CallbackResult<B::Wire::Value_Ursa_LocalTransformValue> result;
		result.status = table.NativeRuntimeBindings_GetLocalTransform(context, &child, &result.value, &result.outcome);
		Expect(bool(result) && std::abs(result.value.F_Position.F_X - 2) < 0.00001f &&
		           std::abs(result.value.F_Position.F_Y - 3) < 0.00001f &&
		           std::abs(result.value.F_Position.F_Z - 4) < 0.00001f,
		       "Typed transform changed world/local units or parent conversion");
	}
	{
		B::Wire::Value_Ursa_Vector3 invalid{std::numeric_limits<Float32>::quiet_NaN(), 1, 2};
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetWorldPosition(context, &child, &invalid, &result.outcome);
		Expect(!result, "Nonfinite typed world position was accepted");
		B::CallbackResult<B::Wire::Value_Ursa_Vector3> saved;
		saved.status = table.NativeRuntimeBindings_GetWorldPosition(context, &child, &saved.value, &saved.outcome);
		Expect(bool(saved) && saved.value.F_X == 14 && saved.value.F_Y == 29 && saved.value.F_Z == 46,
		       "Invalid typed world position partially changed the transform");
	}
	for (UInt32 variant = 0; variant != 3; ++variant)
	{
		Handle invalid = child;
		if (variant == 0)
		{
			++invalid.F_generation;
		}
		if (variant == 1)
		{
			++invalid.F_sessionId;
		}
		if (variant == 2)
		{
			invalid.F_kind = UInt32(ManagedHandleKind::Component);
		}
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetActive(context, &invalid, 0, &result.outcome);
		Expect(!result, "Stale, cross-session or wrong-kind typed handle accepted");
	}
	Expect(std::async(std::launch::async,
	                  [&]
	                  {
		                  B::CallbackResult<void> result;
		                  result.status = table.NativeRuntimeBindings_SetActive(context, &child, 0, &result.outcome);
		                  return !result;
	                  })
	           .get(),
	       "Typed scene modification accepted a non-owner thread");
	{
		B::CallbackResult<UInt32> result;
		result.status = table.NativeRuntimeBindings_GetActive(context, &child, &result.value, &result.outcome);
		Expect(bool(result) && result.value == 1, "Rejected handle/thread call modified the object");
	}
	String persistentId;
	{
		B::CallbackResult<B::Utf8View> result;
		result.status =
		    table.NativeRuntimeBindings_GetGameObjectPersistentId(context, &child, &result.value, &result.outcome);
		Expect(bool(result), "Typed persistent identity query failed");
		persistentId = B::ReadString(result.value);
	}
	{
		B::CallbackResult<Handle> result;
		B::Utf8View id{reinterpret_cast<const UInt8*>(persistentId.data()), persistentId.size(), 1, 0};
		result.status =
		    table.NativeRuntimeBindings_ResolveGameObjectPersistentId(context, &id, &result.value, &result.outcome);
		Expect(bool(result) && result.value.F_index == child.F_index, "Typed identity resolution changed the handle");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_DestroyGameObject(context, &child, &result.outcome);
		Expect(bool(result) && bool(scene.CommitChanges()), "Typed GameObject destruction failed");
	}
	{
		B::CallbackResult<B::Utf8View> result;
		result.status = table.NativeRuntimeBindings_GetName(context, &child, &result.value, &result.outcome);
		Expect(!result, "Typed GameObject getter accepted a destroyed object");
	}
	Handle component;
	String type("ursa.Camera");
	B::Utf8View typeName{reinterpret_cast<const UInt8*>(type.data()), type.size(), 1, 0};
	{
		B::CallbackResult<void> result;
		result.status =
		    table.NativeRuntimeBindings_AddComponent(context, &parent, &typeName, &component, &result.outcome);
		Expect(bool(result), "Typed native Component creation failed");
	}
	{
		B::Wire::Value_Ursa_CameraProjection projection{0.9f, 0.25f, 500};
		B::CallbackResult<void> result;
		result.status =
		    table.NativeRuntimeBindings_SetCameraProjection(context, &component, &projection, &result.outcome);
		Expect(bool(result), "Typed Camera projection failed");
	}
	{
		B::Wire::Value_Ursa_CameraProjection invalid{180, 0, 1};
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetCameraProjection(context, &component, &invalid, &result.outcome);
		Expect(!result, "Camera projection silently reinterpreted degrees or invalid near plane");
		B::CallbackResult<B::Wire::Value_Ursa_CameraProjection> saved;
		saved.status =
		    table.NativeRuntimeBindings_GetCameraProjection(context, &component, &saved.value, &saved.outcome);
		Expect(bool(saved) && saved.value.F_VerticalFovRadians == 0.9f && saved.value.F_NearPlane == 0.25f &&
		           saved.value.F_FarPlane == 500,
		       "Invalid typed Camera projection changed saved values");
	}
	{
		B::CallbackResult<B::Wire::Value_Ursa_CameraRenderingSettings> result;
		result.status =
		    table.NativeRuntimeBindings_GetCameraRenderingSettings(context, &component, &result.value, &result.outcome);
		Expect(bool(result), "Typed Camera rendering settings query failed");
		auto value = result.value;
		value.F_Depth = 2.5f;
		value.F_Viewport = {0.5f, 0, 0.25f, 1};
		B::CallbackResult<void> applied;
		applied.status =
		    table.NativeRuntimeBindings_SetCameraRenderingSettings(context, &component, &value, &applied.outcome);
		Expect(bool(applied), "Typed Camera viewport settings failed");
		value.F_Viewport.F_Width = -1;
		B::CallbackResult<void> invalid;
		invalid.status =
		    table.NativeRuntimeBindings_SetCameraRenderingSettings(context, &component, &value, &invalid.outcome);
		Expect(!invalid, "Invalid typed Camera viewport was accepted");
		B::CallbackResult<B::Wire::Value_Ursa_CameraRenderingSettings> saved;
		saved.status =
		    table.NativeRuntimeBindings_GetCameraRenderingSettings(context, &component, &saved.value, &saved.outcome);
		Expect(bool(saved) && saved.value.F_Depth == 2.5f && saved.value.F_Viewport.F_Width == 0.25f,
		       "Invalid Camera rendering settings partially changed the component");
	}
	{
		B::CallbackResult<B::DataView> result;
		result.status = table.NativeRuntimeBindings_GetComponents(context, &parent, &result.value, &result.outcome);
		Expect(bool(result) && result.value.length == 1 &&
		           static_cast<const Handle*>(result.value.data)->F_index == component.F_index,
		       "Typed Component collection did not return a stable handle snapshot");
	}
	{
		B::CallbackResult<B::Utf8View> result;
		result.status =
		    table.NativeRuntimeBindings_GetComponentType(context, &component, &result.value, &result.outcome);
		Expect(bool(result) && B::ReadString(result.value) == type, "Typed Component type identity changed");
	}
	{
		B::CallbackResult<Handle> result;
		result.status =
		    table.NativeRuntimeBindings_GetComponentOwner(context, &component, &result.value, &result.outcome);
		Expect(bool(result) && result.value.F_index == parent.F_index, "Typed Component owner differs");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_SetComponentEnabled(context, &component, 0, &result.outcome);
		Expect(bool(result), "Typed Component enabled setter failed");
	}
	{
		B::CallbackResult<UInt32> result;
		result.status =
		    table.NativeRuntimeBindings_GetComponentEnabled(context, &component, &result.value, &result.outcome);
		Expect(bool(result) && result.value == 0, "Typed Component enabled state differs");
	}
	{
		Handle invalid = component;
		++invalid.F_sessionId;
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_RemoveComponent(context, &invalid, &result.outcome);
		Expect(!result, "Cross-session Component was removed");
	}
	{
		B::CallbackResult<B::Utf8View> result;
		result.status =
		    table.NativeRuntimeBindings_GetComponentPersistentId(context, &component, &result.value, &result.outcome);
		Expect(bool(result), "Typed Component persistent identity query failed");
		B::CallbackResult<Handle> resolved;
		resolved.status = table.NativeRuntimeBindings_ResolveComponentPersistentId(context, &result.value,
		                                                                           &resolved.value, &resolved.outcome);
		Expect(bool(resolved) && resolved.value.F_index == component.F_index,
		       "Typed Component persistent identity did not resolve");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRuntimeBindings_RemoveComponent(context, &component, &result.outcome);
		Expect(bool(result) && bool(scene.CommitChanges()), "Typed Component removal failed");
	}
	{
		B::CallbackResult<UInt32> result;
		result.status =
		    table.NativeRuntimeBindings_GetComponentEnabled(context, &component, &result.value, &result.outcome);
		Expect(!result, "Removed Component binding remained usable");
	}
}

void CheckMeshBindings()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	namespace B = Bindings;
	using Handle = B::Wire::Value_Ursa_NativeHandle;
	Scene scene;
	ManagedSceneApi api(scene);
	const auto& table = *static_cast<const B::Runtime::Table*>(B::Runtime::NativeDescriptor().functions);
	Handle mesh;
	{
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_Create(api.Token(), &mesh, &result.outcome);
		Expect(bool(result) && mesh.F_kind == UInt32(ManagedHandleKind::Mesh), "Typed Mesh creation failed");
	}
	std::vector<B::Wire::Value_Ursa_Vector3> vertices(65536);
	for (Size index = 0; index < vertices.size(); ++index)
	{
		vertices[index] = {Float32(index % 256), Float32(index / 256), Float32(index % 3)};
	}
	std::vector<B::Wire::Value_Ursa_Vector2> uv(vertices.size(), {0.25f, 0.75f});
	std::vector<B::Wire::Value_Ursa_Color> colors(vertices.size(), {0.125f, 0.25f, 0.75f, 1});
	std::vector<B::Wire::Value_Ursa_Vector4> tangents(vertices.size(), {1, 2, 3, -1});
	UInt32 indices[]{1, 2, 3};
	B::Wire::Value_Ursa_SubMesh section{0, 3, -1};
	B::Wire::Value_Ursa_MeshData data;
	data.present = 1;
	data.F_Positions = {vertices.data(), vertices.size(), 1, 0};
	data.F_Indices = {indices, std::size(indices), 1, 0};
	data.F_SubMeshes = {&section, 1, 1, 0};
	data.F_Normals = {nullptr, 0, 1, 0};
	data.F_Uv0 = {uv.data(), uv.size(), 1, 0};
	data.F_Colors = {colors.data(), colors.size(), 1, 0};
	data.F_Tangents = {tangents.data(), tangents.size(), 1, 0};
	data.F_Bounds.F_Empty = 1;
	{
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_SetData(api.Token(), &mesh, &data, &result.outcome);
		Expect(bool(result), "Typed large Mesh data or baseVertex was rejected");
	}
	UInt64 version = 0;
	{
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_GetVersion(api.Token(), &mesh, &version, &result.outcome);
		Expect(bool(result), "Typed Mesh version query failed");
	}
	{
		B::CallbackResult<B::Wire::Value_Ursa_MeshData> result;
		result.status = table.NativeMeshBindings_GetData(api.Token(), &mesh, &result.value, &result.outcome);
		Expect(bool(result) && result.value.F_Positions.length == vertices.size() &&
		           result.value.F_SubMeshes.length == 1 &&
		           static_cast<const B::Wire::Value_Ursa_SubMesh*>(result.value.F_SubMeshes.data)->F_BaseVertex == -1,
		       "Typed Mesh output lost channels or signed baseVertex");
		Expect(static_cast<const B::Wire::Value_Ursa_Vector2*>(result.value.F_Uv0.data)[0].F_Y == 0.75f &&
		           static_cast<const B::Wire::Value_Ursa_Color*>(result.value.F_Colors.data)[0].F_B == 0.75f &&
		           static_cast<const B::Wire::Value_Ursa_Vector4*>(result.value.F_Tangents.data)[0].F_W == -1,
		       "Typed Mesh UV/color/tangent layouts differ");
		static_cast<B::Wire::Value_Ursa_Vector3*>(const_cast<void*>(result.value.F_Positions.data))[0].F_X = 999;
		B::CallbackResult<B::Wire::Value_Ursa_MeshData> next;
		next.status = table.NativeMeshBindings_GetData(api.Token(), &mesh, &next.value, &next.outcome);
		Expect(bool(next) && static_cast<const B::Wire::Value_Ursa_Vector3*>(next.value.F_Positions.data)[0].F_X == 0,
		       "Modifying a returned Mesh array bypassed publication");
	}
	{
		B::CallbackResult<B::DataView> result;
		result.status = table.NativeMeshBindings_GetTriangles(api.Token(), &mesh, 0, &result.value, &result.outcome);
		Expect(bool(result) && result.value.length == 3 && static_cast<const UInt32*>(result.value.data)[0] == 0 &&
		           static_cast<const UInt32*>(result.value.data)[2] == 2,
		       "Typed Mesh triangle query lost signed baseVertex");
	}
	indices[2] = 999999;
	{
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_SetData(api.Token(), &mesh, &data, &result.outcome);
		Expect(!result, "Typed Mesh accepted an invalid vertex reference");
	}
	{
		B::DataView invalid{nullptr, std::numeric_limits<UInt64>::max(), 1, 0};
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_SetVertices(api.Token(), &mesh, &invalid, &result.outcome);
		Expect(!result, "Typed Mesh accepted an overflowing channel");
	}
	{
		B::CallbackResult<UInt64> result;
		result.status = table.NativeMeshBindings_GetVersion(api.Token(), &mesh, &result.value, &result.outcome);
		Expect(bool(result) && result.value == version, "Invalid or copied Mesh edits changed its version");
	}
	Handle clone;
	{
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_Clone(api.Token(), &mesh, &clone, &result.outcome);
		Expect(bool(result) && clone.F_index != mesh.F_index, "Typed Mesh clone reused its source identity");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeMeshBindings_Destroy(api.Token(), &mesh, &result.outcome);
		Expect(bool(result), "Typed Mesh destruction failed");
	}
	{
		B::CallbackResult<B::Wire::Value_Ursa_MeshData> result;
		result.status = table.NativeMeshBindings_GetData(api.Token(), &mesh, &result.value, &result.outcome);
		Expect(!result, "Destroyed Mesh binding remained usable");
	}
	{
		B::CallbackResult<B::Wire::Value_Ursa_MeshData> result;
		result.status = table.NativeMeshBindings_GetData(api.Token(), &clone, &result.value, &result.outcome);
		Expect(bool(result) && result.value.F_Positions.length == vertices.size(),
		       "Destroying Mesh source damaged its clone");
	}
}

void CheckRenderingBindings()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	namespace B = Bindings;
	using Token = B::Wire::Value_Ursa_Rendering_RenderContextToken;
	using ResultValue = B::Wire::Value_Ursa_Rendering_RenderResultValue;
	Scene scene;
	auto first = B::TakeBindingResult(
	    B::TakeBindingResult(scene.CreateGameObject("First camera"))->AddComponent<CameraComponent>());
	auto second = B::TakeBindingResult(
	    B::TakeBindingResult(scene.CreateGameObject("Second camera"))->AddComponent<CameraComponent>());
	first->rendering.depth = 1;
	second->rendering.depth = 2;
	B::CompleteBindingResult(scene.CommitChanges());
	ManagedSceneApi api(scene);
	const auto& table = *static_cast<const B::Rendering::Table*>(B::Rendering::NativeDescriptor().functions);
	B::CallbackResult<Token> begun;
	begun.status = table.NativeRenderingBindings_Begin(api.Token(), 11, 1, &begun.value, &begun.outcome);
	Expect(bool(begun), "Typed render context did not begin");
	const Token token = begun.value;
	first->rendering.depth = 8;
	{
		B::CallbackResult<B::DataView> cameras;
		cameras.status = table.NativeRenderingBindings_Cameras(api.Token(), &token, &cameras.value, &cameras.outcome);
		Expect(bool(cameras) && cameras.value.length == 2, "Typed render context lost captured cameras");
		const auto* frames = static_cast<const B::Wire::Value_Ursa_Rendering_CameraFrameValue*>(cameras.value.data);
		Expect(frames[0].F_Settings.F_Depth == 1 && frames[1].F_Settings.F_Depth == 2,
		       "Typed camera snapshot retained a live mutable settings reference");
	}
	String identity = first->PersistentId().ToString();
	B::Utf8View id{reinterpret_cast<const UInt8*>(identity.data()), identity.size(), 1, 0};
	{
		B::CallbackResult<B::Wire::Value_Ursa_Rendering_CameraFrameValue> camera;
		camera.status =
		    table.NativeRenderingBindings_SetupCamera(api.Token(), &token, &id, &camera.value, &camera.outcome);
		Expect(bool(camera) && camera.value.F_Settings.F_Depth == 1, "Typed camera setup lost captured settings");
	}
	B::CallbackResult<ResultValue> culling;
	culling.status = table.NativeRenderingBindings_Cull(api.Token(), &token, &id, &culling.value, &culling.outcome);
	Expect(bool(culling), "Typed Cull failed for the active camera");
	B::Wire::Value_Ursa_Rendering_DrawingSettingsValue drawing;
	drawing.F_ShaderTags = {nullptr, 0, 1, 0};
	drawing.F_Sorting = 1;
	String pipeline("UrsaForward");
	drawing.F_ActivePipeline = {reinterpret_cast<const UInt8*>(pipeline.data()), pipeline.size(), 1, 0};
	drawing.F_OverrideMaterialPass = -1;
	B::Wire::Value_Ursa_Rendering_FilteringSettings filtering;
	filtering.present = 1;
	filtering.F_MaximumQueue = 5000;
	filtering.F_LayerMask = 0xffffffffu;
	B::CallbackResult<ResultValue> list;
	list.status = table.NativeRenderingBindings_CreateRendererList(api.Token(), &token, &culling.value, &drawing,
	                                                               &filtering, &list.value, &list.outcome);
	Expect(bool(list), "Typed RendererList creation failed");
	for (UInt32 variant = 0; variant < 4; ++variant)
	{
		Token invalid = token;
		if (variant == 0)
		{
			++invalid.F_sessionId;
		}
		if (variant == 1)
		{
			++invalid.F_frameId;
		}
		if (variant == 2)
		{
			++invalid.F_index;
		}
		if (variant == 3)
		{
			++invalid.F_generation;
		}
		B::CallbackResult<void> result;
		result.status = table.NativeRenderingBindings_Submit(api.Token(), &invalid, &result.outcome);
		Expect(!result, "Typed Submit accepted a foreign or stale render token");
	}
	{
		B::CallbackResult<void> result;
		result.status = table.NativeRenderingBindings_Abort(api.Token(), &token, &result.outcome);
		Expect(bool(result), "Typed render abort failed");
	}
	{
		B::CallbackResult<B::DataView> cameras;
		cameras.status = table.NativeRenderingBindings_Cameras(api.Token(), &token, &cameras.value, &cameras.outcome);
		Expect(!cameras, "Ended typed render frame remained readable");
	}
	{
		B::CallbackResult<Token> next;
		next.status = table.NativeRenderingBindings_Begin(api.Token(), 12, 1, &next.value, &next.outcome);
		Expect(bool(next) && next.value.F_generation != token.F_generation,
		       "Typed abort did not permit a new generation");
		B::CallbackResult<ResultValue> invalid;
		invalid.status = table.NativeRenderingBindings_CreateRendererList(
		    api.Token(), &next.value, &culling.value, &drawing, &filtering, &invalid.value, &invalid.outcome);
		Expect(!invalid, "A new typed frame accepted old CullingResults");
		B::CallbackResult<void> ended;
		ended.status = table.NativeRenderingBindings_End(api.Token(), &next.value, &ended.outcome);
		Expect(bool(ended), "Typed render context did not end");
	}
}

void CheckMaterialBindings()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	namespace B = Bindings;
	using Handle = B::Wire::Value_Ursa_NativeHandle;
	using OptionalHandle = B::Wire::Value_Ursa_NativeHandle_;
	Scene scene;
	ManagedSceneApi api(scene);
	const auto& table = *static_cast<const B::Runtime::Table*>(B::Runtime::NativeDescriptor().functions);
	auto object = B::TakeBindingResult(scene.CreateGameObject("Material bindings"));
	auto* renderer = B::TakeBindingResult(object->AddComponent<MeshRendererComponent>());
	auto shader = std::make_shared<Shader::ShaderAsset>();
	shader->id = ResourceId::Generate();
	for (const auto& type :
	     {Shader::PropertyType::Float, Shader::PropertyType::Range, Shader::PropertyType::Int,
	      Shader::PropertyType::Color, Shader::PropertyType::Vector, Shader::PropertyType::Texture2D})
	{
		Shader::PropertyDeclaration property;
		property.identifier =
		    Array<const char*, 6>{"_Float", "_Range", "_Int", "_Color", "_Vector", "_Texture"}[Size(type)];
		property.type = type;
		property.rangeMinimum = 0;
		property.rangeMaximum = 1;
		property.defaultTexture = "white";
		shader->properties.push_back(property);
	}
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	B::CompleteBindingResult(material->SetShaderAsset(shader));
	B::CompleteBindingResult(renderer->SetSharedMaterials({material, nullptr}));
	const auto component = B::WriteManagedHandle<Handle>(B::TakeBindingResult(api.RegisterComponent(renderer)));
	Handle handle;
	{
		B::CallbackResult<B::DataView> result;
		result.status = table.NativeResourceBindings_GetMaterialBindings(api.Token(), &component, 0, 0, &result.value,
		                                                                 &result.outcome);
		Expect(bool(result) && result.value.length == 2, "Typed renderer slots lost their null entry");
		const auto* slots = static_cast<const OptionalHandle*>(result.value.data);
		Expect(slots[0].present == 1 && slots[1].present == 0, "Typed renderer slot presence differs");
		handle = slots[0].F_value;
	}
	auto check = [&](auto function, auto... arguments)
	{
		B::CallbackResult<void> result;
		result.status = function(api.Token(), &handle, arguments..., &result.outcome);
		if (!result)
		{
			throw std::runtime_error(ToStdString(result.Error()));
		}
	};
	auto propertyName = [&](Shader::PropertyType type)
	{
		const auto& identifier = shader->properties[Size(type)].identifier;
		return B::Utf8View{reinterpret_cast<const UInt8*>(identifier.data()), identifier.size(), 1, 0};
	};
	for (const auto type : {Shader::PropertyType::Float, Shader::PropertyType::Range})
	{
		const auto name = propertyName(type);
		check(table.NativeMaterialBindings_SetFloat, &name, 0.25f);
		B::CallbackResult<Float32> value;
		value.status = table.NativeMaterialBindings_GetFloat(api.Token(), &handle, &name, &value.value, &value.outcome);
		Expect(bool(value) && value.value == 0.25f, "Typed Float/Range material value differs");
	}
	const auto range = propertyName(Shader::PropertyType::Range);
	{
		B::CallbackResult<void> invalid;
		invalid.status = table.NativeMaterialBindings_SetFloat(api.Token(), &handle, &range, 2, &invalid.outcome);
		Expect(!invalid &&
		           B::TakeBindingResult(material->PropertyValue(B::ReadString(range), *shader)).floatValue == 0.25f,
		       "Rejected material Range update changed the value");
	}
	const auto integer = propertyName(Shader::PropertyType::Int);
	check(table.NativeMaterialBindings_SetInt, &integer, -123456789);
	{
		B::CallbackResult<Int32> value;
		value.status =
		    table.NativeMaterialBindings_GetInt(api.Token(), &handle, &integer, &value.value, &value.outcome);
		Expect(bool(value) && value.value == -123456789, "Typed material Int lost signed precision");
		B::CallbackResult<Float32> invalid;
		invalid.status =
		    table.NativeMaterialBindings_GetFloat(api.Token(), &handle, &integer, &invalid.value, &invalid.outcome);
		Expect(!invalid, "Typed material getter accepted a different Shader property kind");
	}
	const auto color = propertyName(Shader::PropertyType::Color);
	const B::Wire::Value_Ursa_Color tint{0.125f, 0.25f, 0.75f, 1};
	check(table.NativeMaterialBindings_SetColor, &color, &tint);
	{
		B::CallbackResult<B::Wire::Value_Ursa_Color> value;
		value.status =
		    table.NativeMaterialBindings_GetColor(api.Token(), &handle, &color, &value.value, &value.outcome);
		Expect(bool(value) && value.value.F_R == tint.F_R && value.value.F_B == tint.F_B,
		       "Typed material Color differs");
	}
	const auto vector = propertyName(Shader::PropertyType::Vector);
	const B::Wire::Value_Ursa_Vector4 parameters{-10, 2, 300, -4};
	check(table.NativeMaterialBindings_SetVector, &vector, &parameters);
	{
		B::CallbackResult<B::Wire::Value_Ursa_Vector4> value;
		value.status =
		    table.NativeMaterialBindings_GetVector(api.Token(), &handle, &vector, &value.value, &value.outcome);
		Expect(bool(value) && value.value.F_X == -10 && value.value.F_Z == 300,
		       "Typed material Vector was clamped as Color");
	}
	const auto texture = propertyName(Shader::PropertyType::Texture2D);
	const B::Wire::Value_Ursa_NativeAssetReference_ emptyTexture;
	check(table.NativeMaterialBindings_SetTextureProperty, &texture, &emptyTexture);
	{
		B::CallbackResult<B::Wire::Value_Ursa_NativeAssetReference_> value;
		value.status = table.NativeMaterialBindings_GetTextureProperty(api.Token(), &handle, &texture, &value.value,
		                                                               &value.outcome);
		Expect(bool(value) && value.value.present == 0, "Typed null material texture acquired an asset identity");
	}
	String keyword("TYPED_BINDING");
	const B::Utf8View keywordView{reinterpret_cast<const UInt8*>(keyword.data()), keyword.size(), 1, 0};
	check(table.NativeMaterialBindings_SetKeyword, &keywordView, 1);
	{
		B::CallbackResult<B::DataView> value;
		value.status = table.NativeMaterialBindings_GetKeywords(api.Token(), &handle, &value.value, &value.outcome);
		Expect(bool(value) && value.value.length == 1 &&
		           B::ReadString(*static_cast<const B::Utf8View*>(value.value.data)) == keyword,
		       "Typed material keywords did not return owned strings");
	}
	Handle instance;
	{
		B::CallbackResult<B::DataView> value;
		value.status = table.NativeResourceBindings_GetMaterialBindings(api.Token(), &component, 1, 1, &value.value,
		                                                                &value.outcome);
		Expect(bool(value), "Typed renderer instance access failed");
		instance = static_cast<const OptionalHandle*>(value.value.data)->F_value;
		Expect(instance.F_index != handle.F_index, "Typed renderer instance access reused the shared Material");
	}
	{
		B::CallbackResult<void> value;
		value.status = table.NativeMaterialBindings_SetInt(api.Token(), &instance, &integer, 42, &value.outcome);
		Expect(bool(value) && B::TakeBindingResult(material->PropertyValue(B::ReadString(integer), *shader)).intValue ==
		                          -123456789,
		       "Typed instance edit changed the shared Material");
	}
	OptionalHandle slots[2]{{1, handle}, {1, instance}};
	++slots[1].F_value.F_sessionId;
	B::DataView invalidSlots{slots, 2, 1, 0};
	{
		const auto original = renderer->SharedMaterials();
		B::CallbackResult<void> value;
		value.status = table.NativeResourceBindings_SetMaterialBindings(api.Token(), &component, &invalidSlots, 1, 0,
		                                                                &value.outcome);
		Expect(!value && renderer->SharedMaterials() == original,
		       "Cross-session renderer array partially changed its slots");
	}
	check(table.NativeMaterialBindings_RemovePropertyOverride, &integer);
	check(table.NativeMaterialBindings_Destroy);
	{
		B::CallbackResult<B::Wire::Value_Ursa_MaterialParametersValue> value;
		value.status = table.NativeMaterialBindings_GetParameters(api.Token(), &handle, &value.value, &value.outcome);
		Expect(!value, "Destroyed Material binding remained readable");
		B::CallbackResult<UInt32> valid;
		valid.status = table.NativeResourceBindings_IsValid(api.Token(), &handle, &valid.value, &valid.outcome);
		Expect(bool(valid) && valid.value == 0, "Released Material remained valid");
	}
}

void CheckPrimitiveBinding()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	namespace B = Bindings;
	VirtualFileSystem files;
	B::CompleteBindingResult(
	    files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto meshPath = B::TakeBindingResult(VirtualPath::Parse("prj:Cube.ursamesh"));
	const auto materialPath = B::TakeBindingResult(VirtualPath::Parse("prj:Default.ursamat"));
	MeshData data;
	data.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	data.indices = {0, 1, 2};
	auto mesh = B::TakeBindingResult(Mesh::Create(std::move(data)));
	B::CompleteBindingResult(SaveMeshResource(files, meshPath, BuiltinCubeMeshId(), *mesh));
	MaterialInstance material(MaterialType::Unlit);
	B::CompleteBindingResult(SaveMaterialResource(files, materialPath, BuiltinDefaultMaterialId(), material, {}));
	for (const auto& asset :
	     {std::make_pair(meshPath, BuiltinCubeMeshId()), std::make_pair(materialPath, BuiltinDefaultMaterialId())})
	{
		const auto metadata = B::TakeBindingResult(VirtualPath::Parse(asset.first.ToString() + ".meta"));
		const auto json =
		    nlohmann::json{{"version", 1}, {"guid", ToStdString(asset.second.ToString())}, {"folder", false}}.dump();
		B::CompleteBindingResult(files.WriteAtomic(metadata, Vector<UInt8>(json.data(), json.data() + json.size())));
	}
	auto resources = B::TakeBindingResult(ResourceSession::Open(files));
	Scene scene;
	ManagedSceneApi api(scene, resources.get());
	const auto& table = *static_cast<const B::Runtime::Table*>(B::Runtime::NativeDescriptor().functions);
	B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> result;
	result.status = table.NativeRuntimeBindings_CreatePrimitive(api.Token(), 0, &result.value, &result.outcome);
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.Error()));
	}
	Expect(scene.RootGameObjects().size() == 1, "Typed primitive was not created once");
	B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> invalid;
	invalid.status = table.NativeRuntimeBindings_CreatePrimitive(api.Token(), 1, &invalid.value, &invalid.outcome);
	Expect(!invalid && scene.RootGameObjects().size() == 1, "Unsupported typed primitive caused a partial creation");
	String guid = BuiltinCubeMeshId().ToString();
	B::Utf8View asset{reinterpret_cast<const UInt8*>(guid.data()), guid.size(), 1, 0};
	B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> loaded;
	loaded.status = table.NativeMeshBindings_Load(api.Token(), &asset, 0, &loaded.value, &loaded.outcome);
	Expect(bool(loaded), "Typed persistent Mesh load failed");
	B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> same;
	same.status = table.NativeMeshBindings_Load(api.Token(), &asset, 0, &same.value, &same.outcome);
	Expect(bool(same) && loaded.value.F_index == same.value.F_index, "Typed Mesh load lost asset sharing");
	B::CallbackResult<void> persistentDestroy;
	persistentDestroy.status = table.NativeMeshBindings_Destroy(api.Token(), &loaded.value, &persistentDestroy.outcome);
	Expect(!persistentDestroy, "Typed Mesh destruction accepted a persistent asset");
	{
		B::CallbackResult<B::Wire::Value_Ursa_ResourceReferenceArchive> snapshot;
		snapshot.status = table.NativeSerializationBindings_SnapshotMesh(api.Token(), &loaded.value, &snapshot.value,
		                                                                 &snapshot.outcome);
		Expect(bool(snapshot) && snapshot.value.F_Version == 1, "Typed resource reference archive creation failed");
		auto json = nlohmann::json::parse(ToStdString(B::ReadString(snapshot.value.F_Json)));
		Expect(json.at("kind") == "asset" && json.at("asset").at("guid") == ToStdString(guid),
		       "Resource archive changed persistent asset identity");
		json["future"] = {{"preserved", true}};
		const String text = ToUrsaString(json.dump());
		B::Wire::Value_Ursa_ResourceReferenceArchive archive{
		    1, {reinterpret_cast<const UInt8*>(text.data()), text.size(), 1, 0}};
		B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> restored;
		restored.status =
		    table.NativeSerializationBindings_RestoreMesh(api.Token(), &archive, &restored.value, &restored.outcome);
		Expect(bool(restored) && restored.value.F_index == loaded.value.F_index,
		       "Resource archive restoration lost sharing or rejected an unknown field");
		archive.F_Version = 99;
		B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> invalid;
		invalid.status =
		    table.NativeSerializationBindings_RestoreMesh(api.Token(), &archive, &invalid.value, &invalid.outcome);
		Expect(!invalid && invalid.Error().find("version") != String::npos,
		       "Unknown resource archive version was accepted");
	}
	const String materialGuid = BuiltinDefaultMaterialId().ToString();
	B::Wire::Value_Ursa_NativeAssetReference materialAsset{
	    {reinterpret_cast<const UInt8*>(materialGuid.data()), materialGuid.size(), 1, 0}, 0};
	B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> materialHandle;
	materialHandle.status =
	    table.NativeMaterialBindings_Load(api.Token(), &materialAsset, &materialHandle.value, &materialHandle.outcome);
	Expect(bool(materialHandle), "Typed persistent Material load failed");
	{
		B::CallbackResult<B::Wire::Value_Ursa_ResourceReferenceArchive> snapshot;
		snapshot.status = table.NativeSerializationBindings_SnapshotMaterial(api.Token(), &materialHandle.value,
		                                                                     &snapshot.value, &snapshot.outcome);
		Expect(bool(snapshot), "Typed Material reference snapshot failed");
		B::CallbackResult<B::Wire::Value_Ursa_NativeHandle> restored;
		restored.status = table.NativeSerializationBindings_RestoreMaterial(api.Token(), &snapshot.value,
		                                                                    &restored.value, &restored.outcome);
		Expect(bool(restored) && restored.value.F_index == materialHandle.value.F_index,
		       "Material archive lost asset sharing");
		B::CallbackResult<B::Wire::Value_Ursa_ResourceReferenceArchive> wrongKind;
		wrongKind.status = table.NativeSerializationBindings_SnapshotMesh(api.Token(), &materialHandle.value,
		                                                                  &wrongKind.value, &wrongKind.outcome);
		Expect(!wrongKind, "Mesh snapshot accepted a Material handle");
		B::CallbackResult<void> invalidDestroy;
		invalidDestroy.status =
		    table.NativeMaterialBindings_Destroy(api.Token(), &materialHandle.value, &invalidDestroy.outcome);
		Expect(!invalidDestroy, "Typed Material destruction accepted a persistent asset");
	}
}
} // namespace

int main()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	try
	{
		const auto& descriptor = Bindings::Runtime::NativeDescriptor();
		CheckPrimitiveBinding();
		CheckPlatformBindings();
		CheckMeshBindings();
		CheckMaterialBindings();
		CheckRenderingBindings();
		if (descriptor.version != 1 || descriptor.pointerSize != sizeof(void*) || !descriptor.validateContext)
		{
			throw std::runtime_error("Invalid generated native descriptor");
		}
		Scene scene;
		ManagedSceneApi sceneApi(scene);
		auto object = scene.CreateGameObject("Binding snapshot object");
		if (!object)
		{
			throw std::runtime_error(ToStdString(object.error));
		}
		auto handle = sceneApi.Register(object.value);
		if (!handle)
		{
			throw std::runtime_error(ToStdString(handle.error));
		}
		sceneApi.CheckHandle(&handle.value, sizeof(handle.value), UInt32(ManagedHandleKind::GameObject));
		CheckGameObjectBindings(sceneApi, scene, handle.value);
		bool rejected = false;
		try
		{
			sceneApi.CheckHandle(&handle.value, sizeof(handle.value), UInt32(ManagedHandleKind::Component));
		}
		catch (const std::exception&)
		{
			rejected = true;
		}
		if (!rejected)
		{
			throw std::runtime_error("Binding context accepted an incorrect handle kind");
		}
		Bindings::BindingOutcome outcome;
		const auto status = descriptor.validateContext(sceneApi.Token(), &outcome);
		Bindings::Release(outcome);
		if (status != 0)
		{
			throw std::runtime_error("Generated context validation failed");
		}
		sceneApi.Deactivate();
		const auto closed = descriptor.validateContext(sceneApi.Token(), &outcome);
		Bindings::Release(outcome);
		if (closed == 0)
		{
			throw std::runtime_error("Closed scene binding context accepted");
		}
		std::cout << "Native binding snapshot and real scene context checks passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
