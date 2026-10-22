#include <Scripting/Bindings/EditorServices.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Editor.generated.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
namespace B = Ursa::Scripting::Bindings;
namespace E = B::Editor;
using P = B::Wire::Enum_Ursa_Editor_SerializedPropertyType;
using K = B::Wire::Enum_Ursa_Editor_EditorReferenceKind;
using Value = Bindings::EditorServices::PropertyValue;

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

template <class T, class F> B::CallbackResult<T> Call(F invoke)
{
	B::CallbackResult<T> result;
	result.status = invoke(&result.value, &result.outcome);
	return result;
}

template <class F> B::CallbackResult<void> CallVoid(F invoke)
{
	B::CallbackResult<void> result;
	result.status = invoke(&result.outcome);
	return result;
}

void TestProperties()
{
	VirtualFileSystem files;
	Expect(
	    static_cast<bool>(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite)),
	    "Memory VFS mount failed");
	auto resources = Take(ResourceSession::Open(files));
	ComponentLoadContext loadContext(*resources);
	ComponentRegistry registry;
	auto owner = GameObject::Create("Typed editor target");
	auto destination = GameObject::Create("Reference destination");
	Expect(static_cast<bool>(destination->SetParent(owner)), "Reference tree setup failed");
	auto* script = Take(owner->AddComponent<Scripting::ScriptBehaviourComponent>("Test.TypedProperties"));
	nlohmann::json payload{{"FormatVersion", 1u},
	                       {"ScriptType", "test.typed"},
	                       {"Version", 1u},
	                       {"Fields", nlohmann::json::object()},
	                       {"FieldTypes", nlohmann::json::object()}};
	const auto field = [&](const char* name, const char* kind, nlohmann::json value)
	{
		payload["Fields"][name] = std::move(value);
		payload["FieldTypes"][name] = {{"Kind", kind}, {"ElementKind", "Unsupported"}};
	};
	field("enabled", "Boolean", true);
	field("small", "SignedInteger", -12);
	payload["FieldTypes"]["small"]["TypeName"] = "System.SByte";
	field("wide", "UnsignedInteger", std::numeric_limits<UInt64>::max());
	field("number", "FloatingPoint", 1.25);
	field("text", "String", nullptr);
	field("v2", "Vector2", {1.0, 2.0});
	field("v3", "Vector3", {1.0, 2.0, 3.0});
	field("v4", "Vector4", {1.0, 2.0, 3.0, 4.0});
	field("rotation", "Quaternion", {0.0, 0.0, 0.0, 1.0});
	field("color", "Color", {0.1, 0.2, 0.3, 1.0});
	field("mode", "Enumeration", std::numeric_limits<UInt64>::max());
	payload["FieldTypes"]["mode"].update({{"EnumValues", {"Zero", "Maximum"}},
	                                      {"EnumNumericValues", {UInt64{0}, std::numeric_limits<UInt64>::max()}},
	                                      {"EnumUnderlyingType", "System.UInt64"}});
	field("values", "List", {1, 2});
	payload["FieldTypes"]["values"].update({{"ElementKind", "SignedInteger"}, {"ElementTypeName", "System.Int32"}});
	field("target", "GameObjectReference", nullptr);
	field("component", "ComponentReference", nullptr);
	field("asset", "AssetReference", nullptr);
	field("future", "Unsupported", {{"unknown", 7}});
	script->SetSerializedPayload(ToUrsaString(payload.dump()));
	const String initial = script->SerializedPayload();
	ManagedEditorContext context;
	context.target = script;
	context.registry = &registry;
	context.selection.object = owner;
	context.selection.roots = {owner};
	context.selection.loadContext = &loadContext;
	void* retired = nullptr;
	{
		Bindings::EditorInvocation invocation(context, 71);
		void* token = invocation.Token();
		retired = token;
		B::BindingArena inputs;
		UInt64 revision = 1;
		const auto path = [&](const String& name) { return inputs.StringValue("/Fields/" + name); };
		const auto get = [&](const String& name)
		{
			const auto valuePath = path(name);
			return Call<Bindings::EditorServices::PropertyState>(
			    [&](auto* value, auto* outcome)
			    { return E::NativeEditorBindings_GetProperty(token, revision, &valuePath, value, outcome); });
		};
		const auto set = [&](const String& name, const Value& value)
		{
			const auto valuePath = path(name);
			return CallVoid(
			    [&](auto* outcome)
			    { return E::NativeEditorBindings_SetProperty(token, revision, &valuePath, &value, outcome); });
		};
		const auto apply = [&]()
		{
			return Call<UInt32>([&](auto* value, auto* outcome)
			                    { return E::NativeEditorBindings_ApplyProperties(token, revision, value, outcome); });
		};
		const auto update = [&]()
		{
			auto refreshed = Call<UInt64>([&](auto* value, auto* outcome)
			                              { return E::NativeEditorBindings_UpdateProperties(token, value, outcome); });
			Expect(static_cast<bool>(refreshed), "Update failed");
			revision = refreshed.value;
		};
		for (const String name : {"enabled", "small", "wide", "number", "text", "v2", "v3", "v4", "rotation", "color",
		                          "mode", "values", "target", "component", "asset", "future"})
		{
			auto value = get(name);
			Expect(static_cast<bool>(value), "Typed property read failed");
		}
		Expect(script->SerializedPayload() == initial && !invocation.properties->HasModifiedProperties(),
		       "Reading properties changed persistent or pending data");
		auto wide = get("wide");
		Expect(wide.value.F_Value.F_UnsignedInteger == std::numeric_limits<UInt64>::max(), "UInt64 value was narrowed");
		auto mode = get("mode");
		Expect(mode.value.F_Type == P::V_Enum && mode.value.F_Value.F_EnumUnsigned &&
		           mode.value.F_EnumUnsignedValues.length == 2 &&
		           static_cast<const UInt64*>(mode.value.F_EnumUnsignedValues.data)[1] ==
		               std::numeric_limits<UInt64>::max(),
		       "Wide unsigned enum metadata/value changed");
		Value changed;
		changed.F_Type = P::V_Float;
		changed.F_Float = 8.5;
		Expect(static_cast<bool>(set("number", changed)), "Float edit failed");
		Value invalid;
		invalid.F_Type = P::V_Integer;
		invalid.F_Integer = 128;
		Expect(static_cast<bool>(set("small", invalid)) && !apply() && script->SerializedPayload() == initial &&
		           !invocation.changed,
		       "Failed transaction partially changed the target");
		Expect(!set("number", invalid), "Mismatched property accessor was accepted");
		const UInt64 staleRevision = revision;
		update();
		const auto numberPath = path("number");
		auto stale = Call<Bindings::EditorServices::PropertyState>(
		    [&](auto* value, auto* outcome)
		    { return E::NativeEditorBindings_GetProperty(token, staleRevision, &numberPath, value, outcome); });
		Expect(!stale && get("number").value.F_Value.F_Float == 1.25 && !invocation.properties->HasModifiedProperties(),
		       "Update did not discard edits and invalidate old property revisions");
		const auto listPath = path("values");
		auto resized = CallVoid(
		    [&](auto* outcome) { return E::NativeEditorBindings_ResizeArray(token, revision, &listPath, 3, outcome); });
		Expect(static_cast<bool>(resized) && get("values/2").value.F_Value.F_Integer == 0,
		       "List growth did not use a typed default");
		invalid.F_Integer = 42;
		Expect(static_cast<bool>(set("values/2", invalid)), "List element edit failed");
		auto oversized = CallVoid(
		    [&](auto* outcome)
		    {
			    return E::NativeEditorBindings_ResizeArray(token, revision, &listPath,
			                                               std::numeric_limits<Int32>::max(), outcome);
		    });
		Expect(!oversized && get("values").value.F_Value.F_ArraySize == 3, "Oversized list mutated the candidate");
		const auto id = inputs.StringValue(destination->Id().ToString());
		Bindings::EditorServices::OptionalAsset noAsset;
		auto reference = Call<Bindings::EditorServices::Reference>(
		    [&](auto* value, auto* outcome)
		    {
			    return E::NativeEditorBindings_ResolveReference(token, K::V_GameObject, &id, &noAsset, value, outcome);
		    });
		Expect(static_cast<bool>(reference), "Object reference resolution failed");
		Value object;
		object.F_Type = P::V_ObjectReference;
		object.F_Reference = {1, reference.value};
		Expect(!set("component", object) && static_cast<bool>(set("target", object)), "Reference kind check failed");
		{
			Bindings::EditorInvocation other(context, 72);
			const auto targetPath = path("target");
			auto crossSession = CallVoid(
			    [&](auto* outcome)
			    { return E::NativeEditorBindings_SetProperty(other.Token(), 1, &targetPath, &object, outcome); });
			Expect(!crossSession && !other.properties->HasModifiedProperties(),
			       "Cross-session Editor reference was accepted");
		}
		auto committed = apply();
		Expect(committed && committed.value && invocation.changed, "Valid property transaction failed");
		const auto saved = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
		Expect(saved["Fields"]["values"] == nlohmann::json({1, 2, 42}) &&
		           saved["Fields"]["future"] == payload["Fields"]["future"] &&
		           saved["Fields"]["target"]["$ursaReference"]["Id"] == ToStdString(destination->Id().ToString()),
		       "Typed transaction lost order, unknown data or persistent identity");
		Value text;
		text.F_Type = P::V_String;
		text.F_String = inputs.StringValue(String("中文\0tail", 11));
		Expect(static_cast<bool>(set("text", text)) && static_cast<bool>(apply()),
		       "Nullable Unicode string edit failed");
		const auto oldPayload = script->SerializedPayload();
		Expect(static_cast<bool>(set("number", changed)), "Conflict candidate setup failed");
		auto external = nlohmann::json::parse(ToStdString(oldPayload));
		external["Fields"]["enabled"] = false;
		script->SetSerializedPayload(ToUrsaString(external.dump()));
		Expect(!apply() && script->SerializedPayload() == ToUrsaString(external.dump()),
		       "Old snapshot overwrote an external edit");
		update();
		bool wrongThreadRejected = false;
		std::thread worker(
		    [&]()
		    {
			    auto scope = Call<B::Wire::Value_Ursa_Editor_EditorScopeValue>(
			        [&](auto* value, auto* outcome)
			        { return E::NativeEditorBindings_GetScope(token, value, outcome); });
			    wrongThreadRejected = !scope;
		    });
		worker.join();
		Expect(wrongThreadRejected, "Native Editor binding accepted a worker thread");
		Expect(static_cast<bool>(owner->RemoveComponent(script)), "Target removal failed");
		context.target = nullptr;
		auto removed = Call<B::Wire::Value_Ursa_Editor_EditorScopeValue>(
		    [&](auto* value, auto* outcome) { return E::NativeEditorBindings_GetScope(token, value, outcome); });
		Expect(!removed, "Removed Inspector target remained callable");
	}
	auto retiredCall = Call<B::Wire::Value_Ursa_Editor_EditorScopeValue>(
	    [&](auto* value, auto* outcome) { return E::NativeEditorBindings_GetScope(retired, value, outcome); });
	Expect(!retiredCall, "Retired callback token was accepted");
}

void TestAssetDescriptions()
{
	VirtualFileSystem files;
	for (const char* scheme : {"prj", "lib"})
	{
		Expect(static_cast<bool>(
		           files.MountProvider(scheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite)),
		       "Asset VFS mount failed");
	}
	const auto source = Take(VirtualPath::Parse("prj:Model.source"));
	Expect(static_cast<bool>(files.WriteAtomic(source, {1, 2, 3})), "Asset source setup failed");
	auto workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
	auto* database = workspace->Database();
	const auto guid = Take(database->Metadata(source)).guid;
	Expect(
	    static_cast<bool>(database->PublishImport(source, "TypedEditorFixture", 1, "test", nlohmann::json::object(), {},
	                                              {{19, "Model", {1}},
	                                               {7, "Mesh", {2}},
	                                               {8, "Material", {3}},
	                                               {9, "Texture", {4}},
	                                               {10, "ScriptableObject", {5}}},
	                                              19)),
	    "Asset description import setup failed");
	ManagedEditorContext context;
	context.workspace = workspace.get();
	context.assetSelection = AssetReference{guid, 8};
	context.assetSelectionName = "Material sub-asset";
	Bindings::EditorInvocation invocation(context, 81);
	auto assets = Call<B::DataView>([&](auto* value, auto* outcome)
	                                { return E::NativeEditorBindings_GetAssets(invocation.Token(), value, outcome); });
	Expect(assets && assets.value.length == 5, "Asset/sub-asset enumeration did not preserve every identity");
	const auto* descriptions = static_cast<const B::Wire::Value_Ursa_Editor_EditorAssetValue*>(assets.value.data);
	bool mesh = false, material = false, texture = false, scriptable = false;
	for (UInt64 index = 0; index < assets.value.length; ++index)
	{
		const auto& reference = descriptions[index].F_Reference;
		Expect((descriptions[index].F_IsSubAsset != 0) == (reference.F_Asset.F_value.F_LocalFileId != 19),
		       "Nonzero main resource was classified as a sub-asset");
		Expect(reference.F_Scope == invocation.Generation() && reference.F_Asset.present &&
		           B::ReadString(reference.F_Asset.F_value.F_Guid) == guid.ToString(),
		       "Asset identity or callback scope changed");
		mesh |= reference.F_Kind == K::V_Mesh && reference.F_Asset.F_value.F_LocalFileId == 7;
		material |= reference.F_Kind == K::V_Material && reference.F_Asset.F_value.F_LocalFileId == 8;
		texture |= reference.F_Kind == K::V_Texture && reference.F_Asset.F_value.F_LocalFileId == 9;
		scriptable |= reference.F_Kind == K::V_ScriptableObject && reference.F_Asset.F_value.F_LocalFileId == 10;
	}
	Expect(mesh && material && texture && scriptable, "Asset description kinds were flattened");
	auto selection =
	    Call<B::DataView>([&](auto* value, auto* outcome)
	                      { return E::NativeEditorBindings_GetSelection(invocation.Token(), value, outcome); });
	Expect(selection && selection.value.length == 1, "Asset selection was omitted");
	const auto* selected = static_cast<const B::Wire::Value_Ursa_Editor_EditorSelectionValue*>(selection.value.data);
	Expect(selected->F_Reference.F_Kind == K::V_Material && selected->F_Reference.F_Asset.F_value.F_LocalFileId == 8 &&
	           B::ReadString(selected->F_Name) == "Material sub-asset",
	       "Selected sub-asset lost its kind or local file ID");
}

void TestAssetPropertyContext()
{
	using Json = nlohmann::json;
	Json draft{{"Fields", {{"quality", 1}}},
	           {"FieldTypes",
	            {{"quality", {{"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "int"}}}}}};
	auto unrelated = Take(EditorDocument::Create(
	    ResourceKind::Material, ResourceId::Generate(), Take(VirtualPath::Parse("prj:Unrelated.ursamat")),
	    [](VirtualFileSystem&, const VirtualPath&, ResourceId) { return Result<void>{}; }));
	ManagedEditorContext context;
	context.document = unrelated.get();
	context.targetUsesDocument = false;
	bool alive = true;
	context.validateTarget = [&]
	{ return alive ? Result<void>{} : Result<void>::Failure("Asset Inspector target was replaced"); };
	SerializedTargetAdapter adapter;
	adapter.capture = [&] { return Result<Json>::Success(draft); };
	adapter.apply = [&](const Json& payload)
	{
		draft = payload;
		return Result<Json>::Success(draft);
	};
	context.serializedTarget = adapter;
	Bindings::EditorInvocation invocation(context, 73);
	auto* token = invocation.Token();
	B::BindingArena inputs;
	const auto path = inputs.StringValue("/Fields/quality");
	const auto document = Call<B::Wire::Value_Ursa_Editor_EditorDocumentValue_>(
	    [&](auto* value, auto* outcome) { return E::NativeEditorBindings_GetDocument(token, value, outcome); });
	Expect(document && !document.value.present, "Importer properties exposed an unrelated active document");
	Value changed;
	changed.F_Type = P::V_Integer;
	changed.F_Integer = 2;
	Expect(static_cast<bool>(CallVoid(
	           [&](auto* outcome) { return E::NativeEditorBindings_SetProperty(token, 1, &path, &changed, outcome); })),
	       "Asset property control did not accept a typed value");
	const auto applied = Call<UInt32>([&](auto* value, auto* outcome)
	                                  { return E::NativeEditorBindings_ApplyProperties(token, 1, value, outcome); });
	Expect(applied && applied.value && draft["Fields"]["quality"] == 2 && !unrelated->Dirty(),
	       "Applying an importer draft dirtied the unrelated active document");
	Expect(!CallVoid([&](auto* outcome) { return E::NativeEditorBindings_Save(token, outcome); }) &&
	           !CallVoid([&](auto* outcome) { return E::NativeEditorBindings_MarkDirty(token, outcome); }) &&
	           !unrelated->Dirty(),
	       "Importer context saved or dirtied the unrelated active document");
	UInt32 saves = 0;
	context.saveTarget = [&]
	{
		++saves;
		return Result<void>{};
	};
	Expect(static_cast<bool>(CallVoid([&](auto* outcome) { return E::NativeEditorBindings_Save(token, outcome); })) &&
	           saves == 1,
	       "Writable asset Inspector did not route its explicit save to the owning asset");
	alive = false;
	Expect(!Call<Bindings::EditorServices::PropertyState>(
	           [&](auto* value, auto* outcome)
	           { return E::NativeEditorBindings_GetProperty(token, 1, &path, value, outcome); }),
	       "A replaced asset target accepted its old callback's property access");
}

void TestGuiContextKind()
{
	Scene scene;
	Scripting::ManagedSceneApi wrongContext(scene);
	ImGui::CreateContext();
	ImGui::GetIO().DisplaySize = {64, 64};
	ImGui::GetIO().DeltaTime = 1.f / 60;
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	ImGui::NewFrame();
	ImGui::Begin("Editor context kind");
	const String label = "Invalid Scene context";
	const B::Utf8View text{reinterpret_cast<const UInt8*>(label.data()), label.size(), 1, 0};
	auto rejected =
	    CallVoid([&](auto* outcome) { return E::NativeEditorBindings_Text(wrongContext.Token(), &text, outcome); });
	ImGui::End();
	ImGui::Render();
	ImGui::DestroyContext();
	Expect(!rejected, "Editor GUI accepted a Runtime Scene context during an active GUI frame");
}
} // namespace

int main()
{
	try
	{
		TestProperties();
		TestAssetDescriptions();
		TestAssetPropertyContext();
		TestGuiContextKind();
		std::cout << "Typed Editor property, reference, transaction and lifetime checks passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
