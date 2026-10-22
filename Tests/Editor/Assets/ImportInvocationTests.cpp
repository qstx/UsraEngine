#include <Scripting/Bindings/ImportServices.hpp>
#include <Import.generated.hpp>
#include <Runtime.generated.hpp>
#include <Rendering.generated.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor::Bindings;
namespace B = Ursa::Scripting::Bindings;
namespace I = B::Import;

class LifecycleCounter final : public Component
{
public:
	explicit LifecycleCounter(int& calls) : calls(calls)
	{
	}

	String SerializationType() const override
	{
		return "test.ImportLifecycleCounter";
	}

	Result<void> Serialize(ArchiveWriter&) const override
	{
		return {};
	}

	Result<void> Deserialize(const ArchiveReader&, SerializationContext&) override
	{
		return {};
	}

	Result<void> Awake(const FrameContext&) override
	{
		++calls;
		return {};
	}

	Result<void> Start(const FrameContext&) override
	{
		++calls;
		return {};
	}

	Result<void> Update(const FrameContext&) override
	{
		++calls;
		return {};
	}

	void OnEnable() override
	{
		++calls;
	}

	void OnDisable() override
	{
		++calls;
	}

	void OnDestroy() override
	{
		++calls;
	}

private:
	int& calls;
};

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

template <class T, class F> B::CallbackResult<T> Call(F function)
{
	B::CallbackResult<T> result;
	result.status = function(&result.value, &result.outcome);
	if (!result)
	{
		throw std::runtime_error(result.Error().c_str());
	}
	return result;
}

template <class F> void CallVoid(F function)
{
	B::CallbackResult<void> result;
	result.status = function(&result.outcome);
	if (!result)
	{
		throw std::runtime_error(result.Error().c_str());
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

B::CallbackResult<B::Wire::Value_Ursa_ImportScopeValue> Scope(void* token)
{
	B::CallbackResult<B::Wire::Value_Ursa_ImportScopeValue> result;
	result.status = I::NativeImportBindings_GetScope(token, &result.value, &result.outcome);
	return result;
}

B::CallbackResult<void> Candidate(void* token, const Scripting::ManagedHandle& handle)
{
	B::CallbackResult<void> result;
	const auto value = B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(handle);
	result.status = I::NativeImportBindings_RequireCandidate(token, &value, &result.outcome);
	return result;
}

void Run()
{
	UInt64 generation = 17;
	ImportInvocation invocation(42, generation, {"prj", "Test.custom"}, ImportPhase::ScriptedImport,
	                            [&] { return generation; });
	auto scope = Scope(invocation.Token());
	Expect(bool(scope) && scope.value.F_Session == 42 && scope.value.F_Generation == 17 &&
	           scope.value.F_Invocation == invocation.Identity() && scope.value.F_Phase == 1 &&
	           B::ReadString(scope.value.F_AssetPath) == "prj:Test.custom",
	       "Import scope lost its identity or source");
	auto owned = std::make_shared<int>(10);
	std::weak_ptr<int> weak = owned;
	const auto handle = invocation.AddCandidate(owned, Scripting::ManagedHandleKind::Mesh);
	owned.reset();
	Expect(!weak.expired() && bool(Candidate(invocation.Token(), handle)), "Scope did not retain its candidate");
	ImportInvocation other(43, generation, {"prj", "Other.custom"}, ImportPhase::ScriptedImport,
	                       [&] { return generation; });
	Expect(!Candidate(other.Token(), handle), "Another invocation accepted a foreign candidate");
	bool wrongThreadRejected = false;
	std::thread worker([&] { wrongThreadRejected = !Scope(invocation.Token()); });
	worker.join();
	Expect(wrongThreadRejected, "Import binding accepted a foreign thread");
	invocation.RemoveCandidate(handle);
	Expect(weak.expired() && !Candidate(invocation.Token(), handle), "Removed candidate remained alive");
	const auto replacement = invocation.AddCandidate(std::make_shared<int>(20), Scripting::ManagedHandleKind::Mesh);
	Expect(replacement.index == handle.index && replacement.generation != handle.generation &&
	           !Candidate(invocation.Token(), handle) && bool(Candidate(invocation.Token(), replacement)),
	       "Slot reuse revived a retired handle");
	invocation.SetPhase(ImportPhase::ModelPostprocess);
	Expect(Scope(invocation.Token()).value.F_Phase == 5, "Callback phase was not propagated");
	auto descriptor = I::NativeDescriptor();
	descriptor.hash[0] ^= 1;
	Expect(!B::ConnectCallbackModule<I::Table>(&descriptor, I::NativeDescriptor(), invocation.Token()),
	       "Import ABI drift was accepted");
	++generation;
	Expect(!Scope(invocation.Token()) && !Candidate(invocation.Token(), replacement),
	       "Obsolete registry generation retained its capability");
	void* token = invocation.Token();
	invocation.Finish();
	Expect(!Scope(token), "Completed invocation retained its capability");
	std::weak_ptr<int> failedCandidate;
	void* failedToken = nullptr;
	try
	{
		ImportInvocation failed(42, generation, {"prj", "Fail.custom"}, ImportPhase::ScriptedImport,
		                        [&] { return generation; });
		failedToken = failed.Token();
		auto value = std::make_shared<int>(30);
		failedCandidate = value;
		failed.AddCandidate(value, Scripting::ManagedHandleKind::Material);
		throw std::runtime_error("Synthetic callback failure");
	}
	catch (const std::runtime_error&)
	{
	}
	Expect(failedCandidate.expired() && failedToken && !Scope(failedToken),
	       "Callback failure leaked a candidate or capability");
}

void PersistentResources()
{
	VirtualFileSystem files;
	Expect(bool(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite)) &&
	           bool(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite)),
	       "Persistent fixture mounts failed");
	const auto meshGuid = ResourceId::Generate();
	const auto materialGuid = ResourceId::Generate();
	const auto textureGuid = ResourceId::Generate();
	const auto consumer = ResourceId::Generate();
	auto mesh = Take(Mesh::Create());
	MeshData meshData;
	meshData.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	meshData.indices = {0, 1, 2};
	meshData.subMeshes = {{0, 3}};
	Expect(bool(mesh->SetData(std::move(meshData))), "Persistent Mesh data was invalid");
	Expect(bool(SaveMeshResource(files, {"prj", "Mesh.ursamesh"}, meshGuid, *mesh)),
	       "Persistent Mesh source encoding failed");
	MaterialInstance material(MaterialType::Unlit);
	Expect(bool(SaveMaterialResource(files, {"prj", "Material.ursamat"}, materialGuid, material, {})),
	       "Persistent Material source encoding failed");
	const auto texture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 0, 0, 255}));
	Expect(bool(SaveTextureResource(files, {"prj", "Texture.ursatex"}, textureGuid, *texture)),
	       "Persistent Texture source encoding failed");
	Expect(bool(files.WriteAtomic({"prj", "Consumer.custom"}, {1})), "Persistent consumer fixture creation failed");
	for (const auto& entry : Vector<std::pair<String, ResourceId>>{{"Mesh.ursamesh", meshGuid},
	                                                               {"Material.ursamat", materialGuid},
	                                                               {"Texture.ursatex", textureGuid},
	                                                               {"Consumer.custom", consumer}})
	{
		const auto metadata = nlohmann::json{{"version", 1},         {"guid", ToStdString(entry.second.ToString())},
		                                     {"folder", false},      {"importer", ""},
		                                     {"importerVersion", 0}, {"settings", nlohmann::json::object()}}
		                          .dump();
		Expect(bool(files.WriteAtomic({"prj", entry.first + ".meta"},
		                              Vector<UInt8>(metadata.data(), metadata.data() + metadata.size()))),
		       "Persistent fixture metadata creation failed");
	}
	auto database = Take(Ursa::Editor::AssetDatabase::Open(files));
	const auto refreshed = database->Refresh();
	if (!refreshed)
	{
		throw std::runtime_error(refreshed.error.c_str());
	}
	Ursa::Editor::ProjectAssetImporter importer;
	for (const auto* name : {"Mesh.ursamesh", "Material.ursamat", "Texture.ursatex"})
	{
		Take(importer.Import(*database, {"prj", name}, true));
	}
	const auto rules = Take(Ursa::Editor::AssetImportRules::Create());
	UInt64 generation = 1;
	ImportInvocation scope(200, generation, {"prj", "Consumer.custom"}, ImportPhase::ScriptedImport,
	                       [&] { return generation; });
	scope.ConfigureDependencies(*database, rules, consumer, "test.PersistentReads");
	const auto api = scope.CpuApi().Token();
	using Handle = B::Wire::Value_Ursa_NativeHandle;
	const String meshId = meshGuid.ToString();
	const B::Utf8View meshText{reinterpret_cast<const UInt8*>(meshId.data()), meshId.size(), 1, 0};
	const auto loadedMesh =
	    Call<Handle>([&](auto* value, auto* outcome)
	                 { return B::Runtime::NativeMeshBindings_Load(api, &meshText, 0, value, outcome); });
	B::BindingArena arena;
	const auto materialReference =
	    B::WriteAssetValue<B::Wire::Value_Ursa_NativeAssetReference>(arena, {materialGuid, 0});
	const auto loadedMaterial =
	    Call<Handle>([&](auto* value, auto* outcome)
	                 { return B::Runtime::NativeMaterialBindings_Load(api, &materialReference, value, outcome); });
	const auto textureReference = B::WriteAssetValue<B::Wire::Value_Ursa_NativeAssetReference>(arena, {textureGuid, 0});
	Call<Handle>([&](auto* value, auto* outcome)
	             { return B::Runtime::NativeResourceBindings_LoadTexture(api, &textureReference, value, outcome); });
	B::CallbackResult<void> meshWrite;
	meshWrite.status = B::Runtime::NativeMeshBindings_Clear(api, &loadedMesh.value, &meshWrite.outcome);
	B::CallbackResult<void> materialWrite;
	const B::Wire::Value_Ursa_Color red{1, 0, 0, 1};
	materialWrite.status =
	    B::Runtime::NativeMaterialBindings_SetBaseColor(api, &loadedMaterial.value, &red, &materialWrite.outcome);
	Expect(!meshWrite && !materialWrite && !Candidate(scope.Token(), B::ReadManagedHandle(loadedMesh.value)) &&
	           !Candidate(scope.Token(), B::ReadManagedHandle(loadedMaterial.value)),
	       "Persistent import assets were mutable or accepted as output candidates");
	const auto cloned =
	    Call<Handle>([&](auto* value, auto* outcome)
	                 { return B::Runtime::NativeMaterialBindings_Clone(api, &loadedMaterial.value, value, outcome); });
	CallVoid([&](auto* outcome)
	         { return B::Runtime::NativeMaterialBindings_SetBaseColor(api, &cloned.value, &red, outcome); });
	Expect(bool(Candidate(scope.Token(), B::ReadManagedHandle(cloned.value))) &&
	           scope.ArtifactDependencies().size() == 3,
	       "Cloning a persistent asset was not writable or typed reads bypassed dependency tracking");
	Expect(bool(files.WriteAtomic({"prj", "Mesh.ursamesh"}, {1, 2, 3})), "Persistent dependency change failed");
	ImportInvocation stale(201, generation, {"prj", "Consumer.custom"}, ImportPhase::ScriptedImport,
	                       [&] { return generation; });
	stale.ConfigureDependencies(*database, rules, consumer, "test.PersistentReads");
	B::CallbackResult<Handle> pending;
	pending.status =
	    B::Runtime::NativeMeshBindings_Load(stale.CpuApi().Token(), &meshText, 0, &pending.value, &pending.outcome);
	Expect(!pending && stale.PendingArtifacts().size() == 1, "Typed loading bypassed stale dependency scheduling");
}

void CpuResources()
{
	Scene author;
	Scene play;
	const auto authorObject = Take(author.CreateGameObject("Author"));
	const auto playObject = Take(play.CreateGameObject("Play"));
	UInt64 generation = 1;
	ImportInvocation scope(100, generation, {"prj", "CPU.custom"}, ImportPhase::ScriptedImport,
	                       [&] { return generation; });
	void* cpu = scope.CpuApi().Token();
	using Handle = B::Wire::Value_Ursa_NativeHandle;
	auto mesh = Call<Handle>([&](auto* value, auto* result)
	                         { return B::Runtime::NativeMeshBindings_Create(cpu, value, result); });
	B::Wire::Value_Ursa_Vector3 vertices[]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	B::DataView positions{vertices, 3, 1, 0};
	CallVoid([&](auto* result)
	         { return B::Runtime::NativeMeshBindings_SetVertices(cpu, &mesh.value, &positions, result); });
	UInt32 indices[]{0, 1, 2};
	B::DataView triangles{indices, 3, 1, 0};
	CallVoid([&](auto* result) { return B::Runtime::NativeMeshBindings_SetSubMeshCount(cpu, &mesh.value, 1, result); });
	CallVoid([&](auto* result)
	         { return B::Runtime::NativeMeshBindings_SetTriangles(cpu, &mesh.value, 0, &triangles, result); });
	const auto candidateMesh = std::static_pointer_cast<Mesh>(scope.Candidate(B::ReadManagedHandle(mesh.value)));
	const ResourceId guid = Take(ResourceId::Parse("12345678-1234-4234-8234-123456789abc"));
	VirtualFileSystem files;
	Expect(bool(files.MountProvider("mem", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite)),
	       "CPU test mount failed");
	Expect(bool(SaveMeshResource(files, {"mem", "Mesh.ursamesh"}, guid, *candidateMesh)), "CPU Mesh encoding failed");
	SerializationContext context;
	const auto loaded = Take(LoadMeshResource(files, {"mem", "Mesh.ursamesh"}, context));
	Expect(Take(loaded.mesh->GetData()).positions.size() == 3 && Take(loaded.mesh->GetData()).indices.size() == 3,
	       "CPU Mesh edits were lost during encoding");
	auto material = Call<Handle>([&](auto* value, auto* result)
	                             { return B::Runtime::NativeMaterialBindings_Create(cpu, 0, value, result); });
	const B::Wire::Value_Ursa_Color green{0, 1, 0, 1};
	CallVoid([&](auto* result)
	         { return B::Runtime::NativeMaterialBindings_SetBaseColor(cpu, &material.value, &green, result); });
	const auto candidateMaterial =
	    std::static_pointer_cast<MaterialInstance>(scope.Candidate(B::ReadManagedHandle(material.value)));
	Expect(bool(SaveMaterialResource(files, {"mem", "Material.ursamat"}, guid, *candidateMaterial, {})),
	       "CPU Material encoding failed");
	const auto restored = Take(LoadMaterialResource(files, {"mem", "Material.ursamat"}, {}));
	Expect(restored.material->Parameters().baseColor.green == 1 && restored.material->Parameters().baseColor.red == 0,
	       "CPU Material edits were lost during serialization");
	const auto text = [](const char* value)
	{ return B::Utf8View{reinterpret_cast<const UInt8*>(value), std::strlen(value), 1, 0}; };
	const auto createObject = [&](const char* name)
	{
		const auto label = text(name);
		return Call<Handle>([&](auto* value, auto* result)
		                    { return B::Runtime::NativeRuntimeBindings_CreateGameObject(cpu, &label, value, result); });
	};
	auto root = createObject("PrefabRoot");
	auto child = createObject("MeshChild");
	CallVoid([&](auto* result)
	         { return B::Runtime::NativeRuntimeBindings_SetParent(cpu, &child.value, &root.value, result); });
	const B::Wire::Value_Ursa_LocalTransformValue transform{{1, 2, 3}, {0, 0, 0, 1}, {2, 2, 2}};
	CallVoid([&](auto* result)
	         { return B::Runtime::NativeRuntimeBindings_SetLocalTransform(cpu, &child.value, &transform, result); });
	const auto filterType = text("ursa.MeshFilter");
	auto filter = Call<Handle>(
	    [&](auto* value, auto* result)
	    { return B::Runtime::NativeRuntimeBindings_AddComponent(cpu, &child.value, &filterType, value, result); });
	const B::Wire::Value_Ursa_NativeHandle_ meshBinding{1, mesh.value};
	CallVoid(
	    [&](auto* result)
	    { return B::Runtime::NativeResourceBindings_SetMeshBinding(cpu, &filter.value, &meshBinding, 0, result); });
	const auto rendererType = text("ursa.MeshRenderer");
	auto renderer = Call<Handle>(
	    [&](auto* value, auto* result)
	    { return B::Runtime::NativeRuntimeBindings_AddComponent(cpu, &child.value, &rendererType, value, result); });
	B::Wire::Value_Ursa_NativeHandle_ materialBinding{1, material.value};
	const B::DataView materialBindings{&materialBinding, 1, 1, 0};
	CallVoid(
	    [&](auto* result)
	    {
		    return B::Runtime::NativeResourceBindings_SetMaterialBindings(cpu, &renderer.value, &materialBindings, 1, 0,
		                                                                  result);
	    });
	const auto prefabRoot = std::static_pointer_cast<GameObject>(scope.Candidate(B::ReadManagedHandle(root.value)));
	const auto node = std::static_pointer_cast<GameObject>(scope.Candidate(B::ReadManagedHandle(child.value)));
	const auto nodeTransform = Take(scope.SceneGraph().GetLocalTransform(node));
	Expect(prefabRoot->Children().size() == 1 && nodeTransform.position.y == 2 && nodeTransform.scale.x == 2,
	       "Import hierarchy or local transform was lost");
	const auto nativeFilter =
	    static_cast<MeshFilterComponent*>(scope.Candidate(B::ReadManagedHandle(filter.value)).get());
	const auto nativeRenderer =
	    static_cast<MeshRendererComponent*>(scope.Candidate(B::ReadManagedHandle(renderer.value)).get());
	Expect(nativeFilter->SharedMesh() == candidateMesh && nativeRenderer->SharedMaterial() == candidateMaterial,
	       "Import component bindings do not refer to owned Mesh and Material candidates");
	ComponentRegistry registry;
	Expect(bool(RegisterBuiltinComponents(registry)), "Prefab component registration failed");
	Expect(bool(SavePrefabResource(files, {"mem", "Candidate.ursaprefab"}, guid, scope.SceneGraph(), prefabRoot,
	                               registry)),
	       "Import hierarchy serialization failed");
	Expect(Take(files.ReadText({"mem", "Candidate.ursaprefab"})).find("MeshChild") != String::npos,
	       "Serialized prefab omitted its child");
	int lifecycleCalls = 0;
	const auto counter = Take(scope.SceneGraph().AddComponent<LifecycleCounter>(prefabRoot, lifecycleCalls));
	Expect(bool(scope.SceneGraph().RemoveComponent(counter)) && lifecycleCalls == 0,
	       "Import hierarchy invoked game lifecycle callbacks");
	Expect(author.RootGameObjects().size() == 1 && play.RootGameObjects().size() == 1 &&
	           author.RootGameObjects()[0] == authorObject && play.RootGameObjects()[0] == playObject,
	       "Import hierarchy modified an author or Play Scene");
	for (bool linear : {false, true})
	{
		auto texture = Call<Handle>(
		    [&](auto* value, auto* result)
		    { return I::NativeImportBindings_CreateTexture2D(scope.Token(), 2, 1, linear, value, result); });
		B::Wire::Value_Ursa_Color colors[]{{1, 0.5f, 0, 1}, {0, 1, 0, 0.25f}};
		B::DataView pixels{colors, 2, 1, 0};
		CallVoid(
		    [&](auto* result)
		    { return I::NativeImportBindings_SetTexture2DPixels(scope.Token(), &texture.value, &pixels, result); });
		auto read = Call<B::DataView>(
		    [&](auto* value, auto* result)
		    { return I::NativeImportBindings_GetTexture2DPixels(scope.Token(), &texture.value, value, result); });
		Expect(read.value.length == 2 && static_cast<const B::Wire::Value_Ursa_Color*>(read.value.data)[0].F_R == 1,
		       "Texture2D pixels did not round-trip");
		const auto candidate =
		    std::static_pointer_cast<ImportTextureCandidate>(scope.Candidate(B::ReadManagedHandle(texture.value)));
		const auto image = Take(candidate->Texture());
		Expect(bool(SaveTextureResource(files, {"mem", "Texture.ursatex"}, guid, *image)),
		       "CPU Texture encoding failed");
		const auto decoded = Take(LoadTextureResource(files, {"mem", "Texture.ursatex"}));
		Expect(decoded.texture->GetWidth() == 2 && decoded.texture->GetHeight() == 1 &&
		           decoded.texture->GetColorSpace() == (linear ? TextureColorSpace::Linear : TextureColorSpace::Srgb) &&
		           decoded.texture->GetPixels()[1] == 128,
		       "Texture2D dimensions, color space or encoded pixels differ");
		B::CallbackResult<void> invalid;
		pixels.length = 1;
		invalid.status =
		    I::NativeImportBindings_SetTexture2DPixels(scope.Token(), &texture.value, &pixels, &invalid.outcome);
		Expect(!invalid && candidate->pixels[1] == 128, "Invalid pixel count changed a candidate");
		pixels.length = 2;
		colors[1].F_R = std::numeric_limits<Float32>::quiet_NaN();
		B::CallbackResult<void> nonfinite;
		nonfinite.status =
		    I::NativeImportBindings_SetTexture2DPixels(scope.Token(), &texture.value, &pixels, &nonfinite.outcome);
		Expect(!nonfinite && candidate->pixels[1] == 128, "Nonfinite pixels changed a candidate");
	}
	B::CallbackResult<Handle> empty;
	empty.status = I::NativeImportBindings_CreateTexture2D(scope.Token(), 0, 1, false, &empty.value, &empty.outcome);
	Expect(!empty, "Empty Texture2D dimensions were accepted");
	FrameContext frame;
	scope.CpuApi().SetFrameContext(&frame);
	B::CallbackResult<B::Wire::Value_Ursa_TimeState> time;
	time.status = B::Runtime::NativeRuntimeBindings_GetTime(cpu, &time.value, &time.outcome);
	Expect(!time && time.Error().find("Import callbacks") != String::npos,
	       "Import used a game frame that was accidentally installed");
	const auto title = text("Import must not own a window");
	B::CallbackResult<void> window;
	window.status = B::Runtime::NativeRuntimeBindings_SetWindowTitle(cpu, &title, &window.outcome);
	Expect(!window && window.Error().find("Import callbacks") != String::npos,
	       "Import window operation bypassed its role gate");
	B::CallbackResult<B::DataView> untracked;
	untracked.status = B::Runtime::NativeRuntimeBindings_ReadFile(cpu, &title, &untracked.value, &untracked.outcome);
	Expect(!untracked && untracked.Error().find("Import callbacks") != String::npos,
	       "Import performed untracked VFS access");
	B::CallbackResult<B::Wire::Value_Ursa_Rendering_RenderContextToken> render;
	render.status = B::Rendering::NativeRenderingBindings_Begin(cpu, 1, 1.0f, &render.value, &render.outcome);
	Expect(!render && render.Error().find("Import callbacks") != String::npos,
	       "Import rendering bypassed its role gate");
	++generation;
	B::CallbackResult<void> obsolete;
	obsolete.status = B::Runtime::NativeMeshBindings_Clear(cpu, &mesh.value, &obsolete.outcome);
	Expect(!obsolete, "Obsolete import generation retained its Runtime resource capability");
	scope.Finish();
	B::CallbackResult<void> expired;
	expired.status = B::Runtime::NativeMeshBindings_Clear(cpu, &mesh.value, &expired.outcome);
	Expect(!expired, "Finished import retained its CPU resource capability");
}
} // namespace

int main()
{
	try
	{
		Run();
		CpuResources();
		PersistentResources();
		std::cout << "Import invocation ownership and generated bindings passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
