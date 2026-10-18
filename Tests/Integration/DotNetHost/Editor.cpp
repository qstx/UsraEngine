#include "TestSupport.hpp"

#if defined(URSA_DOTNET_TEST_EDITOR)
#include <Scripting/ManagedEditorBridge.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Assets/SceneDraft.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakArchive.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
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
	    R"({"FormatVersion":1,"ScriptType":"test.session-probe","Version":1,
            "Fields":{"value":7,"position":[1.0,2.0,3.0],"modes":[1,3],"future":{"preserved":"unknown data"}},
            "FieldTypes":{
                "value":{"Kind":"SignedInteger","ElementKind":"Unsupported","TypeName":"System.Int32"},
                "position":{"Kind":"Vector3","ElementKind":"Unsupported"},
                "modes":{"Kind":"List","ElementKind":"Enumeration","ElementTypeName":"Ursa.SessionFixtures.ProbeMode",
                    "EnumUnderlyingType":"System.Byte","EnumValues":["First","Second"],"EnumNumericValues":[1,3]}}})");
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
	Expect(payload["Fields"]["position"][0] == 9.0 && payload["Fields"]["modes"][1] == 1,
	       "Unity-style nested and array property edits did not persist");
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
} // namespace Ursa::Test
#endif
