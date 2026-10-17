#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Runtime.generated.hpp>
#include <BindingCalls.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <iostream>
#include <cstring>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;
using namespace Ursa::Test::BindingCalls;
namespace R = Bindings::Runtime;
namespace W = Bindings::Wire;
using Handle = W::Value_Ursa_NativeHandle;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

bool Same(const Handle& left, const Handle& right)
{
	return std::memcmp(&left, &right, sizeof(Handle)) == 0;
}

void Run()
{
	Scene scene;
	ManagedSceneApi bridge(scene);
	void* context = bridge.Token();
	auto mesh = Value(Invoke<Handle>(R::NativeMeshBindings_Create, context));
	Array<W::Value_Ursa_Vector3, 3> positions{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
	Array<UInt32, 3> indices{0, 1, 2};
	W::Value_Ursa_MeshData geometry;
	geometry.present = 1;
	geometry.F_Positions = {positions.data(), positions.size(), 1, 0};
	geometry.F_Indices = {indices.data(), indices.size(), 1, 0};
	geometry.F_Normals = geometry.F_Uv0 = geometry.F_Colors = geometry.F_Tangents =
	    geometry.F_SubMeshes = {nullptr, 0, 1, 0};
	geometry.F_Bounds.F_Empty = 1;
	Complete(InvokeVoid(R::NativeMeshBindings_SetData, context, &mesh, &geometry));
	const auto version = Value(Invoke<UInt64>(R::NativeMeshBindings_GetVersion, context, &mesh));
	indices[2] = 1234;
	Expect(!InvokeVoid(R::NativeMeshBindings_SetData, context, &mesh, &geometry) &&
	           Value(Invoke<UInt64>(R::NativeMeshBindings_GetVersion, context, &mesh)) == version,
	       "Invalid managed Mesh data changed its version");
	auto clone = Value(Invoke<Handle>(R::NativeMeshBindings_Clone, context, &mesh));
	auto data = Invoke<W::Value_Ursa_MeshData>(R::NativeMeshBindings_GetData, context, &mesh);
	Expect(data && !Same(mesh, clone) && data.value.F_Positions.length == 3, "Mesh clone lost geometry or identity");
	Complete(InvokeVoid(R::NativeMeshBindings_Destroy, context, &mesh));
	Expect(Value(Invoke<UInt32>(R::NativeResourceBindings_IsValid, context, &mesh)) == 0,
	       "Destroyed Mesh handle remained valid");
	auto reused = Value(Invoke<Handle>(R::NativeMeshBindings_Create, context));
	Expect(!Same(reused, mesh) && reused.F_generation != mesh.F_generation &&
	           !Invoke<W::Value_Ursa_MeshData>(R::NativeMeshBindings_GetData, context, &mesh),
	       "Freed Mesh slot did not advance generation or reject stale access");
	Scene anotherScene;
	ManagedSceneApi anotherBridge(anotherScene);
	Expect(!Invoke<W::Value_Ursa_MeshData>(R::NativeMeshBindings_GetData, anotherBridge.Token(), &clone),
	       "Foreign-session Mesh was accepted");
	auto material = Value(Invoke<Handle>(R::NativeMaterialBindings_Create, context, Int32(MaterialType::Unlit)));
	Expect(!Invoke<W::Value_Ursa_MeshData>(R::NativeMeshBindings_GetData, context, &material),
	       "Mesh getter accepted a Material handle");
	const W::Value_Ursa_Color color{0.3f, 0.8f, 0.1f, 1};
	Complete(InvokeVoid(R::NativeMaterialBindings_SetBaseColor, context, &material, &color));
	const auto previous = Value(
	    Invoke<W::Value_Ursa_MaterialParametersValue>(R::NativeMaterialBindings_GetParameters, context, &material));
	Expect(!InvokeVoid(R::NativeMaterialBindings_SetRoughness, context, &material, -1.f),
	       "Invalid material mutation was accepted");
	const auto retained = Value(
	    Invoke<W::Value_Ursa_MaterialParametersValue>(R::NativeMaterialBindings_GetParameters, context, &material));
	Expect(std::memcmp(&previous, &retained, sizeof(previous)) == 0, "Invalid material mutation changed values");
	Complete(InvokeVoid(R::NativeMaterialBindings_Destroy, context, &material));
	Expect(!Invoke<W::Value_Ursa_MaterialParametersValue>(R::NativeMaterialBindings_GetParameters, context, &material),
	       "Destroyed Material was readable");
	const auto object = GameObject::Create("Object");
	Expect(bool(scene.AttachGameObject(object)), "Object attach failed");
	auto* filter = object->AddComponent<MeshFilterComponent>().value;
	auto* renderer = object->AddComponent<MeshRendererComponent>().value;
	const auto filterHandle = Bindings::WriteManagedHandle<Handle>(bridge.RegisterComponent(filter).value);
	const W::Value_Ursa_NativeHandle_ shared{1, clone};
	Complete(InvokeVoid(R::NativeResourceBindings_SetMeshBinding, context, &filterHandle, &shared, UInt32{0}));
	auto instance = Value(Invoke<W::Value_Ursa_NativeHandle_>(R::NativeResourceBindings_GetMeshBinding, context,
	                                                          &filterHandle, UInt32{1}));
	auto repeated = Value(Invoke<W::Value_Ursa_NativeHandle_>(R::NativeResourceBindings_GetMeshBinding, context,
	                                                          &filterHandle, UInt32{1}));
	auto currentShared = Value(Invoke<W::Value_Ursa_NativeHandle_>(R::NativeResourceBindings_GetMeshBinding, context,
	                                                               &filterHandle, UInt32{0}));
	Expect(instance.present && !Same(instance.F_value, clone) && Same(instance.F_value, repeated.F_value) &&
	           Same(instance.F_value, currentShared.F_value),
	       "Instance Mesh cloning or shared binding rules changed");
	Expect(!object->AddComponent<MeshFilterComponent>() && !object->AddComponent<MeshRendererComponent>(),
	       "Duplicate render component was accepted");
	Expect(bool(renderer->SetEnabled(false)) && !filter->SetEnabled(false),
	       "Renderer/MeshFilter enabled rules changed");
}
} // namespace

int main()
{
	try
	{
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
