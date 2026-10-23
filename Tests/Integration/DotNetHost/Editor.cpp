#include "TestSupport.hpp"

#if defined(URSA_DOTNET_TEST_EDITOR)
#include <Scripting/ManagedEditorBridge.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Assets/SceneDraft.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Assets/ScriptableAssetInspector.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/AssetPreviewExtension.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakArchive.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
void TestEditorExtensions(DotNetHost& host, VirtualFileSystem& source, const std::filesystem::path& root)
{
	using namespace Ursa::Editor;
	VirtualFileSystem editorFiles;
	Done(editorFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(
	    editorFiles.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
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
                "position":{"Kind":"Vector3","ElementKind":"Unsupported","TypeName":"Ursa.Vector3"},
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
	Expect(descriptors.size() == 14,
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
		ImGui::SetNextWindowSize({600, 450}, ImGuiCond_Always);
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
	const auto textureSource = Path("prj:TextureProbe.png");
	Done(workspace->Index().Filesystem().WriteAtomic(textureSource, {1, 2, 3}));
	Done(workspace->Database()->Refresh());
	auto importer = Take(AssetImporterInspector::Open(*workspace->Database(), textureSource));
	const auto initialMetadata = Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta")));
	context.serializedTarget = importer->Properties();
	context.importerInspector = importer;
	context.assetSelection = importer->Reference();
	context.targetUsesDocument = false;
	UInt32 imports = 0;
	context.requestImport = [&](const VirtualPath& path)
	{
		Expect(path == textureSource, "Managed importer Apply queued a different source");
		++imports;
	};
	Expect(static_cast<bool>(draw("inspector:importer:ursa.Texture")) && importer->HasModified() &&
	           Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta"))) == initialMetadata &&
	           !documents->Current()->Dirty(),
	       "Managed typed importer changed metadata or the active document before Apply");
	auto* importerWindow = ImGui::FindWindowByName("Managed editor fixture");
	ImGui::ActivateItemByID(ImHashStr("Apply", 0, importerWindow->ID));
	Expect(static_cast<bool>(draw("inspector:importer:ursa.Texture")) && !importer->HasModified() && imports == 1 &&
	           Take(workspace->Database()->Metadata(textureSource)).importSettings.at("maxDimension") == 8 &&
	           !documents->Current()->Dirty(),
	       "Managed AssetImporterEditor Apply did not save its typed draft and request exactly one import");
	{
		const auto savedMetadata = Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta")));
		auto preview = std::make_unique<AssetPreviewExtension>(
		    "inspector:importer:ursa.Texture",
		    [&](UInt64 instance, ManagedPreviewPhase phase, const ViewportRect& rect,
		        ManagedEditorContext& previewContext)
		    {
			    return InvokeManagedEditorPreview(host, domain, "inspector:importer:ursa.Texture", instance, phase,
			                                      rect, previewContext);
		    });
		preview->Bind(context);
		auto query = Take(preview->Invoke(ManagedPreviewPhase::Query));
		Expect(query.hasPreview && query.constantRepaint && query.info.find("enables=1 disables=0") != String::npos,
		       "Preview existence, repaint request or OnEnable was lost");
		ImGui::NewFrame();
		ImGui::Begin("Managed Preview toolbar");
		const auto toolbar = preview->Invoke(ManagedPreviewPhase::Settings);
		ImGui::End();
		ImGui::Render();
		Expect(toolbar && toolbar.value.info.find("settings=1") != String::npos,
		       "Preview toolbar did not run in the common callback scope");
		ImGui::NewFrame();
		ImGui::Begin("Shared asset Inspector instance");
		context.importerControlsDrawn = false;
		const auto sharedInspector = preview->DrawInspector(context);
		ImGui::End();
		ImGui::Render();
		Expect(sharedInspector && context.importerControlsDrawn &&
		           Take(preview->Invoke(ManagedPreviewPhase::Query)).info.find("enables=1 disables=0 settings=1") !=
		               String::npos,
		       "Asset Inspector and Preview did not retain one shared instance");
		Take(preview->Invoke(ManagedPreviewPhase::Draw, {0, 0, 200, 100}));
		const auto image = preview->Image();
		Expect(image && image->GetWidth() == 2 && image->GetHeight() == 2 &&
		           image->GetColorSpace() == TextureColorSpace::Linear && image->GetPixels()[3] == 128 &&
		           image->GetPixels()[11] == 0 && !documents->Current()->Dirty() &&
		           Take(workspace->Index().Filesystem().Read(Path("prj:TextureProbe.png.meta"))) == savedMetadata,
		       "Managed Preview changed pixels, alpha, color space, settings or author Dirty");
		Take(preview->Invoke(ManagedPreviewPhase::Query));
		preview.reset();
		auto replacementPreview = std::make_unique<AssetPreviewExtension>(
		    "inspector:importer:ursa.Texture",
		    [&](UInt64 instance, ManagedPreviewPhase phase, const ViewportRect& rect,
		        ManagedEditorContext& previewContext)
		    {
			    return InvokeManagedEditorPreview(host, domain, "inspector:importer:ursa.Texture", instance, phase,
			                                      rect, previewContext);
		    });
		replacementPreview->Bind(context);
		query = Take(replacementPreview->Invoke(ManagedPreviewPhase::Query));
		Expect(query.info.find("enables=2 disables=1") != String::npos,
		       "Preview target replacement retained the old instance or omitted OnDisable");
	}
	context.serializedTarget.reset();
	context.importerInspector.reset();
	context.assetSelection.reset();
	context.requestImport = {};
	const auto assetSource = Path("prj:EditorSettings.ursaasset");
	Done(workspace->Index().Filesystem().WriteAtomic(assetSource, {}));
	Done(workspace->Database()->Refresh());
	const auto assetGuid = Take(workspace->Database()->Metadata(assetSource)).guid;
	ScriptTypeDescriptor assetType;
	assetType.sourceGuid = Take(ResourceId::Parse("11111111-1111-4111-8111-111111111111"));
	assetType.localFileId = 4;
	assetType.scriptType = "test.reload-dependency";
	assetType.kind = ScriptTypeKind::ScriptableObject;
	assetType.fields.push_back({"value", "SignedInteger", "Unsupported", "System.Int32", "7"});
	assetType.fields.push_back(
	    {"self", "ScriptableObjectReference", "Unsupported", "Ursa.SessionFixtures.ReloadDependency", "null"});
	Done(SaveScriptableAssetSource(workspace->Index().Filesystem(), assetSource,
	                               Take(CreateScriptableAssetWithDefaults(assetGuid, assetType))));
	auto assetInspector = Take(ScriptableAssetInspector::Open(*workspace, assetSource, assetType));
	context.serializedTarget = assetInspector->Properties();
	context.assetSelection = AssetReference{assetGuid, 4};
	context.targetIdentity = "asset:test.reload-dependency";
	context.saveTarget = [&] { return assetInspector->Save(); };
	Expect(
	    static_cast<bool>(draw("inspector:asset:test.reload-dependency")) &&
	        Take(LoadScriptableAssetSource(workspace->Index().Filesystem(), assetSource, assetGuid))
	                .Payload()["Fields"]["value"] == 9 &&
	        !documents->Current()->Dirty(),
	    "Typed ScriptableObject Target did not save through its source transaction or dirtied an unrelated document");
	Expect(static_cast<bool>(draw("inspector:asset:test.reload-dependency")) &&
	           Take(LoadScriptableAssetSource(workspace->Index().Filesystem(), assetSource, assetGuid))
	                   .Payload()["Fields"]["value"] == 11,
	       "Typed source Inspector did not refresh its fields or expire the previous callback target");
	const auto rendererDataPath = Path("prj:UniversalRendererData.ursaasset");
	Done(workspace->Index().Filesystem().WriteAtomic(rendererDataPath, {}));
	Done(workspace->Database()->Refresh());
	const auto rendererDataGuid = Take(workspace->Database()->Metadata(rendererDataPath)).guid;
	ScriptTypeDescriptor rendererDataType;
	rendererDataType.sourceGuid = Take(ResourceId::Parse("11111111-1111-4111-8111-111111111111"));
	rendererDataType.localFileId = 42;
	rendererDataType.scriptType = "ursa.rendering.universal-renderer-data";
	rendererDataType.version = 1;
	rendererDataType.kind = ScriptTypeKind::ScriptableObject;
	rendererDataType.fields.push_back(
	    {"cascadeSplits", "Array", "FloatingPoint", "System.Single[]", "[0.25,0.75]", "System.Single"});
	Done(SaveScriptableAssetSource(workspace->Index().Filesystem(), rendererDataPath,
	                               Take(CreateScriptableAssetWithDefaults(rendererDataGuid, rendererDataType))));
	auto rendererDataInspector = Take(ScriptableAssetInspector::Open(*workspace, rendererDataPath, rendererDataType));
	const auto drawRendererData = [&]
	{
		ImGui::NewFrame();
		ImGui::Begin("Renderer asset ordering");
		auto result = rendererDataInspector->Draw({}, {});
		ImGui::End();
		ImGui::Render();
		return result;
	};
	Take(drawRendererData());
	auto* rendererDataWindow = ImGui::FindWindowByName("Renderer asset ordering");
	Expect(rendererDataWindow != nullptr, "Renderer data Inspector window was not created");
	const String splitPath = "/Fields/cascadeSplits";
	const String secondSplitPath = splitPath + "/1";
	const ImGuiID splitFieldId = ImHashStr(splitPath.c_str(), 0, rendererDataWindow->ID);
	const ImGuiID secondSplitId = ImHashStr(secondSplitPath.c_str(), 0, splitFieldId);
	ImGui::ActivateItemByID(ImHashStr("^", 0, secondSplitId));
	Expect(Take(drawRendererData()) && rendererDataInspector->Dirty(),
	       "Renderer data Inspector did not apply an ordered property change or report Dirty");
	Done(rendererDataInspector->Save());
	const auto savedRendererData =
	    Take(LoadScriptableAssetSource(workspace->Index().Filesystem(), rendererDataPath, rendererDataGuid));
	Expect(savedRendererData.Payload()["Fields"]["cascadeSplits"] == nlohmann::json::array({0.75, 0.25}) &&
	           !rendererDataInspector->Dirty(),
	       "Renderer data Inspector did not save and reopen the reordered feature list");
	context.serializedTarget.reset();
	context.assetSelection.reset();
	context.targetIdentity.clear();
	context.saveTarget = {};
	context.selection = {};
	auto materialDraft = std::make_shared<MaterialDraft>(MaterialType::Unlit);
	auto materialShader = std::make_shared<Shader::ShaderAsset>();
	materialShader->id = ResourceId::Generate();
	Shader::PropertyDeclaration strength;
	strength.identifier = "_Strength";
	strength.type = Shader::PropertyType::Range;
	materialShader->properties.push_back(strength);
	Shader::PropertyDeclaration color;
	color.identifier = "_Color";
	color.type = Shader::PropertyType::Color;
	color.defaultVector = {1, 1, 1, 1};
	materialShader->properties.push_back(color);
	Done(materialDraft->Material().SetShaderAsset(materialShader));
	context.serializedTarget = materialDraft->Properties(documents->Session());
	context.targetMaterial = materialDraft->MaterialPtr();
	context.assetSelection = AssetReference{ResourceId::Generate(), 0};
	context.targetIdentity = "asset:ursa.Material";
	bool materialDirty = false;
	UInt32 materialSaves = 0;
	context.markTargetDirty = [&] { materialDirty = true; };
	context.saveTarget = [&]
	{
		++materialSaves;
		return Result<void>{};
	};
	Expect(Take(draw("inspector:asset:ursa.Material")) && materialDirty && materialSaves == 0 &&
	           Take(materialDraft->Material().PropertyValue("_Strength", *materialShader)).floatValue == 0.75f &&
	           Take(materialDraft->Material().PropertyValue("_Color", *materialShader)).vectorValue[0] == 0.2f &&
	           !documents->Current()->Dirty(),
	       "Typed Material Target did not commit its private candidate through the owning property transaction");
	context.targetMaterial = materialDraft->MaterialPtr();
	ImGui::ActivateItemByID(ImHashStr("Save material target", 0, importerWindow->ID));
	Expect(static_cast<bool>(draw("inspector:asset:ursa.Material")) && materialSaves == 1 &&
	           !documents->Current()->Dirty(),
	       "Typed Material save targeted the active Scene or lost its command");
	Done(materialDraft->Material().SetPropertyValue("_Strength", *materialShader, MaterialPropertyValue::Range(0.25f)));
	context.targetMaterial = materialDraft->MaterialPtr();
	context.serializedTarget = materialDraft->Properties(documents->Session(), false);
	context.canEditTarget = false;
	Expect(!draw("inspector:asset:ursa.Material") &&
	           Take(materialDraft->Material().PropertyValue("_Strength", *materialShader)).floatValue == 0.25f,
	       "Read-only typed Material Target wrote back its private candidate");
	context.canEditTarget = true;
	context.targetMaterial.reset();
	context.assetSelection.reset();
	context.serializedTarget.reset();
	context.targetIdentity.clear();
	context.markTargetDirty = {};
	context.saveTarget = {};
	context.targetUsesDocument = true;
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
	Take(prefabRoot->AddComponent<CameraComponent>());
	prefabScript->SetSerializedPayload(script->SerializedPayload());
	Done(documents->RequestOpen(std::move(prefabDocument)));
	context.document = documents->Current();
	context.target = prefabScript;
	context.selection.object = prefabRoot;
	context.selection.roots = {prefabRoot};
	context.selection.scene = nullptr;
	context.selection.loadContext = &loadContext;
	ImGui::NewFrame();
	ImGui::Begin("Detached Prefab fixture");
	auto prefabEdited = InvokeManagedEditorExtension(host, replacement, "inspector:test.session-probe", context);
	ImGui::End();
	ImGui::Render();
	Expect(prefabEdited && prefabEdited.value &&
	           nlohmann::json::parse(ToStdString(prefabScript->SerializedPayload()))["Fields"]["value"] == 9,
	       "Reloaded Inspector retained old state or failed to edit a detached Prefab");
	Expect(documents->Current()->Dirty(), "Prefab property Apply did not mark the current Prefab document Dirty");
	context.target = nullptr;
	ImGui::NewFrame();
	ImGui::Begin("Typed Prefab fixture");
	const auto typedPrefab = InvokeManagedEditorExtension(host, replacement, "inspector:asset:ursa.Prefab", context);
	ImGui::End();
	ImGui::Render();
	Expect(typedPrefab && Take(prefabRoot->Name()) == "Typed Prefab target" &&
	           Take(prefabRoot->GetLocalTransform()).position.x == 2 && documents->Current()->Dirty(),
	       "Typed independent Prefab target did not expose its graph or native components");
	Done(documents->SaveCurrent());
	auto reopenedPrefab = Take(
	    OpenSceneDocument(Path("prj:EditorFixture.ursaprefab"), ResourceKind::Prefab, registry, documents->Session()));
	auto reopenedPrefabRoot = reopenedPrefab->Payload<SceneDraft>()->GetPrefabAsset()->Root();
	auto* reopenedPrefabScript = reopenedPrefabRoot->FindComponent<ScriptBehaviourComponent>();
	Expect(!documents->Current()->Dirty() && reopenedPrefabRoot->Id() == prefabRoot->Id() && reopenedPrefabScript &&
	           reopenedPrefabScript->PersistentId() == prefabScript->PersistentId() &&
	           nlohmann::json::parse(ToStdString(reopenedPrefabScript->SerializedPayload()))["Fields"]["value"] == 11 &&
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
