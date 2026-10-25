#include <MeshFixtures.hpp>
#include <ResourceFixtures.hpp>
#include <Assets/SceneDraft.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/DocumentResources.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Core/StringInterop.hpp>
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

ImGuiID PickerButtonId(const Ursa::String& controlId)
{
	return ImHashStr("...", 0, ImHashStr(controlId.c_str(), 0, ImHashStr("Scene Draft Integration Picker")));
}

void DrawPickerFrame(const Ursa::String& controlId, const Ursa::Vector<Ursa::Editor::ReferencePickerOption>& options,
                     const Ursa::String& currentKey, const Ursa::Editor::ReferencePickerTransaction& transaction)
{
	ImGui::NewFrame();
	ImGui::SetNextWindowSize({600, 400}, ImGuiCond_Always);
	ImGui::Begin("Scene Draft Integration Picker");
	(void)Ursa::Editor::DrawReferencePickerButton(controlId, "Prefab Material", "Material", options, currentKey,
	                                              "scene-draft-integration", &transaction);
	ImGui::End();
	ImGui::Render();
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::current_path() / ("scene_draft_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "Assets");
		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		done(material->SetBaseColor({0.7f, 0.2f, 0.1f, 1.f}));
		auto workspace = Take(Editor::ProjectWorkspace::Open(root));
		done(workspace->SetRuntimeContentView(workspace->Index().Filesystem()));
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		const auto scenePath = root / "Assets" / "level.ursascene";
		auto sceneDocument = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                   ResourceKind::Scene, registry, documents->Session()));
		const auto sceneId = sceneDocument->Id();
		auto draft = sceneDocument->Payload<Editor::SceneDraft>();
		Scene& scene = draft->GetScene();
		const auto first = scene.RootGameObjects().front();
		const auto child = Take(scene.CreateGameObject(first));
		const auto last = Take(scene.CreateGameObject());
		done(scene.SetGameObjectName(child, "Nested box"));
		done(scene.SetGameObjectName(last, "Last"));
		done(scene.SetActive(child, false));
		LocalTransform pose;
		pose.position = {1, 2, 3};
		done(scene.SetLocalTransform(child, pose));
		Take(Ursa::Test::AddRenderer(scene, child));
		auto* sceneRenderer = child->FindComponent<MeshRendererComponent>();
		Expect(sceneRenderer != nullptr, "Scene mesh renderer was not created");
		sceneRenderer->SetCastShadows(false);
		sceneRenderer->SetReceiveShadows(false);
		Take(scene.AddComponent<DirectionalLightComponent>(last));
		auto* sceneLight = last->FindComponent<DirectionalLightComponent>();
		Expect(sceneLight != nullptr, "Scene directional light was not created");
		done(sceneLight->SetCastsShadows(false));
		done(sceneLight->SetShadowStrength(0.4f));
		done(sceneLight->SetShadowBias(0.02f));
		done(sceneLight->SetShadowNormalBias(0.2f));
		done(sceneLight->SetSoftShadows(true));
		Expect(!sceneLight->SetShadowStrength(1.1f) && !sceneLight->SetShadowBias(-0.01f) &&
		           sceneLight->ShadowStrength() == 0.4f && sceneLight->ShadowBias() == 0.02f,
		       "Directional light Inspector setters accepted invalid shadow values or changed the stored settings");
		done(sceneRenderer->SetSharedMaterial(material));
		const auto childId = Take(scene.PersistentId(child));
		const auto lastId = Take(scene.PersistentId(last));
		done(documents->RequestOpen(std::move(sceneDocument)));
		done(documents->SaveCurrent());
		auto reopened = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                               ResourceKind::Scene, registry, documents->Session()));
		Expect(reopened->Id() == sceneId, "Scene resource ID changed after reopen");
		auto loaded = reopened->Payload<Editor::SceneDraft>();
		const auto loadedChild = Take(loaded->GetScene().GameObjectById(childId));
		Expect(Take(loaded->GetScene().GameObjectName(loadedChild)) == "Nested box" &&
		           !Take(loaded->GetScene().ActiveSelf(loadedChild)) &&
		           Take(loaded->GetScene().GetLocalTransform(loadedChild)).position.x == 1.f,
		       "Scene hierarchy or transform did not round-trip");
		Expect(Take(loaded->GetScene().PersistentId(loaded->GetScene().RootGameObjects().back())) == lastId,
		       "Scene root order or ID changed");
		const auto* box = loaded->GetScene().FindComponent<MeshRendererComponent>(loadedChild);
		const auto loadedLast = Take(loaded->GetScene().GameObjectById(lastId));
		const auto* loadedLight = loaded->GetScene().FindComponent<DirectionalLightComponent>(loadedLast);
		Expect(box && !box->CastShadows() && !box->ReceiveShadows() && box->SharedMaterial() &&
		           box->SharedMaterial()->Parameters().baseColor.red > 0.6f && loadedLight &&
		           !loadedLight->CastsShadows() && loadedLight->ShadowStrength() == 0.4f &&
		           loadedLight->ShadowBias() == 0.02f && loadedLight->ShadowNormalBias() == 0.2f &&
		           loadedLight->SoftShadows(),
		       "Scene material reference did not round-trip");
		const auto exportRoot = loaded->GetScene().RootGameObjects().front();
		const auto exportRootId = Take(loaded->GetScene().PersistentId(exportRoot));
		const auto exportChildId = Take(loaded->GetScene().PersistentId(loadedChild));
		const auto rootCountBeforeExport = loaded->GetScene().RootGameObjects().size();
		ComponentLoadContext exportContext(documents->Session());
		const ResourceId exportedGuid = ResourceId::Generate();
		const MaterialIdResolver exportMaterialId = [&](const std::shared_ptr<MaterialInstance>& instance)
		{ return documents->Session().IdentifyMaterial(instance); };
		auto exportedPrefab = Take(CapturePrefabAsset(exportedGuid, loaded->GetScene(), exportRoot, registry,
		                                              exportContext, exportMaterialId));
		Scene detachedExport;
		auto detachedExportRoot =
		    Take(exportedPrefab.Instantiate(detachedExport, registry, exportContext, exportMaterialId));
		exportedPrefab = Take(CapturePrefabAsset(exportedGuid, detachedExport, detachedExportRoot, registry,
		                                         exportContext, exportMaterialId));
		Expect(exportedPrefab.Id() == exportedGuid && exportedPrefab.Root()->Id() != exportRootId &&
		           exportedPrefab.Root()->Children().size() == 1 &&
		           exportedPrefab.Root()->Children().front()->Id() != exportChildId,
		       "Scene subtree export did not remap Prefab/object identities while preserving its hierarchy");
		const auto exportedPath = Take(VirtualPath::Parse("prj:exported.ursaprefab"));
		done(SavePrefabAssetResource(workspace->Index().Filesystem(), exportedPath, exportedPrefab, registry,
		                             exportMaterialId));
		auto exportedDocument =
		    Take(Editor::OpenSceneDocument(exportedPath, ResourceKind::Prefab, registry, documents->Session()));
		const auto loadedExport = exportedDocument->Payload<Editor::SceneDraft>()->GetPrefabAsset()->Root();
		const auto* exportedRenderer = loadedExport->Children().front()->FindComponent<MeshRendererComponent>();
		Expect(exportedDocument->Id() == exportedGuid && exportedRenderer && exportedRenderer->SharedMaterial() &&
		           exportedRenderer->SharedMaterial()->Parameters().baseColor.red > 0.6f &&
		           loaded->GetScene().RootGameObjects().size() == rootCountBeforeExport &&
		           Take(loaded->GetScene().PersistentId(exportRoot)) == exportRootId &&
		           Take(loaded->GetScene().PersistentId(loadedChild)) == exportChildId,
		       "Saved Scene Prefab export lost its material reference or replaced source Scene objects");

		const auto prefabPath = root / "Assets" / "tile.ursaprefab";
		auto prefabDocument = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:tile.ursaprefab")),
		                                                    ResourceKind::Prefab, registry, documents->Session()));
		const auto prefabId = prefabDocument->Id();
		auto prefabDraft = prefabDocument->Payload<Editor::SceneDraft>();
		const auto prefabRoot = prefabDraft->GetPrefabAsset()->Root();
		auto prefabLight = Take(prefabRoot->AddComponent<DirectionalLightComponent>());
		done(prefabLight->SetCastsShadows(false));
		done(prefabLight->SetShadowStrength(0.65f));
		done(prefabLight->SetShadowBias(0.015f));
		done(prefabLight->SetShadowNormalBias(0.12f));
		done(prefabLight->SetSoftShadows(true));
		const auto previewRoot = prefabDraft->GetScene().RootGameObjects().front();
		Expect(prefabRoot->Id() != previewRoot->Id() && !prefabRoot->Parent(),
		       "Prefab editing graph was attached to its preview Scene");
		Expect(Take(prefabDraft->PreviewGameObject(prefabRoot->Id())) == previewRoot,
		       "Prefab preview did not retain a source GameObject mapping");
		auto previewTransform = Take(previewRoot->GetLocalTransform());
		previewTransform.position.x = 2.f;
		done(previewRoot->SetLocalTransform(previewTransform));
		done(prefabDraft->SyncPrefabTransform(prefabRoot->Id()));
		Expect(Take(prefabRoot->GetLocalTransform()).position.x == 2.f,
		       "Prefab viewport transform was not copied back to its detached GameObject");
		const auto prefabChild = GameObject::Create("Prefab child");
		done(prefabChild->SetParent(prefabRoot));
		auto prefabBox = Take(Ursa::Test::AddRenderer(prefabChild));
		Expect(prefabBox->TryGetScene() == nullptr, "Prefab component unexpectedly attached to its preview Scene");
		prefabBox->SetCastShadows(false);
		prefabBox->SetReceiveShadows(false);
		done(prefabBox->SetSharedMaterial(material));
		const auto prefabChildId = prefabChild->Id();
		done(documents->RequestOpen(std::move(prefabDocument)));
		done(documents->SaveCurrent());
		auto reopenedPrefab = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:tile.ursaprefab")),
		                                                     ResourceKind::Prefab, registry, documents->Session()));
		Expect(reopenedPrefab->Id() == prefabId, "Prefab resource ID changed after reopen");
		auto prefabLoaded = reopenedPrefab->Payload<Editor::SceneDraft>();
		const auto loadedPrefabRoot = prefabLoaded->GetPrefabAsset()->Root();
		const auto loadedPrefabChild = loadedPrefabRoot->Children().front();
		const auto* loadedPrefabLight = loadedPrefabRoot->FindComponent<DirectionalLightComponent>();
		const auto* loadedPrefabRenderer = loadedPrefabChild->FindComponent<MeshRendererComponent>();
		Expect(loadedPrefabChild->Id() == prefabChildId && Take(loadedPrefabChild->Name()) == "Prefab child" &&
		           loadedPrefabRenderer && !loadedPrefabRenderer->CastShadows() &&
		           !loadedPrefabRenderer->ReceiveShadows() && loadedPrefabRenderer->SharedMaterial() &&
		           loadedPrefabLight && !loadedPrefabLight->CastsShadows() &&
		           loadedPrefabLight->ShadowStrength() == 0.65f && loadedPrefabLight->ShadowBias() == 0.015f &&
		           loadedPrefabLight->ShadowNormalBias() == 0.12f && loadedPrefabLight->SoftShadows(),
		       "Prefab subtree or material reference did not round-trip");
		auto linkedDocument = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                     ResourceKind::Scene, registry, documents->Session()));
		auto linkedDraft = linkedDocument->Payload<Editor::SceneDraft>();
		const auto placementPath = Take(VirtualPath::Parse("prj:Scene.ursascene"));
		const auto beforeFailedPlacement = linkedDraft->GetScene().RootGameObjects().size();
		Expect(!linkedDraft->PlacePrefabBatch({{prefabId, childId}, {{}, childId}}, placementPath, sceneId) &&
		           !linkedDraft->Linked() && linkedDraft->GetScene().RootGameObjects().size() == beforeFailedPlacement,
		       "A failed Prefab placement batch left partial instances in the scene draft");
		const auto placedInstances = Take(linkedDraft->PlacePrefabBatch(
		    {{prefabId, childId}, {prefabId, childId}, {prefabId, childId}}, placementPath, sceneId));
		Expect(placedInstances.size() == 3, "A successful Prefab placement batch lost an instance");
		const auto firstInstance = placedInstances[0];
		const auto secondInstance = placedInstances[1];
		const auto thirdInstance = placedInstances[2];
		const auto modelDocument = [](const std::string& name)
		{
			return nlohmann::json{{"format", "UrsaModel"},
			                      {"version", 2u},
			                      {"rootNodeLocalFileId", 1u},
			                      {"nodes", nlohmann::json::array({{{"localFileId", 1u},
			                                                        {"name", name},
			                                                        {"localTransform",
			                                                         {{"position", {0.f, 0.f, 0.f}},
			                                                          {"rotation", {0.f, 0.f, 0.f, 1.f}},
			                                                          {"scale", {1.f, 1.f, 1.f}}}},
			                                                        {"children", nlohmann::json::array()}}})}};
		};
		const auto currentChild = Take(linkedDraft->GetScene().GameObjectById(childId));
		const auto existingChildCount = Take(linkedDraft->GetScene().ChildrenOf(currentChild)).size();
		const ResourceId firstModelGuid = ResourceId::Generate();
		const ResourceId secondModelGuid = ResourceId::Generate();
		Expect(!linkedDraft->PlaceImportedAssets({{firstModelGuid, "Model", modelDocument("Should roll back")},
		                                          {secondModelGuid, "Model", nlohmann::json::object()}},
		                                         childId, existingChildCount, placementPath, sceneId) &&
		           Take(linkedDraft->GetScene().ChildrenOf(Take(linkedDraft->GetScene().GameObjectById(childId))))
		                   .size() == existingChildCount,
		       "Failed mixed Model placement left a partial hierarchy instance");
		Expect(!linkedDraft->PlaceImportedAssets({{firstModelGuid, "Mesh", nlohmann::json::object()}}, childId,
		                                         existingChildCount, placementPath, sceneId),
		       "Scene placement accepted a child Mesh instead of a Prefab or Model main asset");
		const auto placedModels =
		    Take(linkedDraft->PlaceImportedAssets({{firstModelGuid, "Model", modelDocument("First model")},
		                                           {secondModelGuid, "Model", modelDocument("Second model")}},
		                                          childId, existingChildCount, placementPath, sceneId));
		const auto childrenAfterPlacement =
		    Take(linkedDraft->GetScene().ChildrenOf(Take(linkedDraft->GetScene().GameObjectById(childId))));
		const auto firstChildAfterPlacement =
		    Take(linkedDraft->GetScene().PersistentId(childrenAfterPlacement[existingChildCount]));
		Expect(placedModels.size() == 2 &&
		           Take(linkedDraft->GetScene().GameObjectName(
		               Take(linkedDraft->GetScene().GameObjectById(placedModels[0])))) == "First model" &&
		           Take(linkedDraft->GetScene().GameObjectName(
		               Take(linkedDraft->GetScene().GameObjectById(placedModels[1])))) == "Second model" &&
		           firstChildAfterPlacement == placedModels[0],
		       "Model main assets were not placed under the requested parent in source order");
		const auto rootsBeforePlacement = linkedDraft->GetScene().RootGameObjects().size();
		const ResourceId rootModelGuid = ResourceId::Generate();
		const auto rootModel = Take(linkedDraft->PlaceImportedAssets(
		    {{rootModelGuid, "Model", modelDocument("Root model")}}, {}, rootsBeforePlacement, placementPath, sceneId));
		const auto rootModelObject = Take(linkedDraft->GetScene().GameObjectById(rootModel.front()));
		const auto rootModelParent = Take(linkedDraft->GetScene().ParentOf(rootModelObject));
		Expect(!rootModelParent && Take(linkedDraft->GetScene().PersistentId(
		                               linkedDraft->GetScene().RootGameObjects().back())) == rootModel.front(),
		       "A root Hierarchy drop did not preserve the requested root order");
		const auto beforePrefabPlacement = childrenAfterPlacement.size();
		const auto placedPrefab = Take(linkedDraft->PlaceImportedAssets(
		    {{prefabId, "Prefab", nlohmann::json::object()}}, childId, beforePrefabPlacement, placementPath, sceneId));
		const auto placedPrefabRoot = Take(linkedDraft->GetScene().GameObjectById(placedPrefab.front()));
		const auto placedPrefabParent = Take(linkedDraft->GetScene().ParentOf(placedPrefabRoot));
		Expect(placedPrefabParent && Take(linkedDraft->GetScene().PersistentId(placedPrefabParent)) == childId,
		       "Prefab main asset was not placed beneath its selected Hierarchy parent");
		const auto firstScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(firstInstance.ToString()).at(prefabChildId.ToString())));
		const auto secondScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(secondInstance.ToString()).at(prefabChildId.ToString())));
		const auto thirdScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(thirdInstance.ToString()).at(prefabChildId.ToString())));
		const auto firstNode = Take(linkedDraft->GetScene().GameObjectById(firstScoped));
		const auto secondNode = Take(linkedDraft->GetScene().GameObjectById(secondScoped));
		auto* firstInstanceRenderer = firstNode->FindComponent<MeshRendererComponent>();
		auto* secondInstanceRenderer = secondNode->FindComponent<MeshRendererComponent>();
		Expect(firstInstanceRenderer && secondInstanceRenderer,
		       "Prefab instance selection did not resolve both common MeshRenderer components");
		secondInstanceRenderer->SetCastShadows(true);
		ComponentLoadContext inspectorContext(documents->Session());
		DocumentResources inspectorDocumentResources;
		DocumentResourceScope inspectorResourceScope(inspectorContext, inspectorDocumentResources);
		SerializationScope inspectorSerializationScope(&inspectorContext);
		const ComponentSaveContext inspectorSaveContext{
		    &linkedDraft->GetScene(), [&](const std::shared_ptr<MaterialInstance>& instance)
		    { return documents->Session().IdentifyMaterial(instance); }, &inspectorContext};
		auto firstSerialized = Take(
		    Editor::SerializedObject::Create(*firstInstanceRenderer, registry, inspectorContext, inspectorSaveContext));
		auto secondSerialized = Take(Editor::SerializedObject::Create(*secondInstanceRenderer, registry,
		                                                              inspectorContext, inspectorSaveContext));
		auto selectedRenderers =
		    Take(Editor::SerializedObjectGroup::Create({firstSerialized.get(), secondSerialized.get()}));
		Expect(Take(selectedRenderers.DescribeProperty(ToUrsaString("/Fields/castShadows")))
		           .value("HasMultipleDifferentValues", false),
		       "Multi-object Inspector did not report mixed Prefab instance values");
		done(selectedRenderers.SetValue(ToUrsaString("/Fields/castShadows"), true));
		done(selectedRenderers.Apply());
		Expect(firstInstanceRenderer->CastShadows() && secondInstanceRenderer->CastShadows(),
		       "A multi-object component edit did not update each selected Prefab instance");
		auto firstOriginalMaterial = firstInstanceRenderer->SharedMaterial();
		auto secondOriginalMaterial = secondInstanceRenderer->SharedMaterial();
		auto pickerTrialMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		done(pickerTrialMaterial->SetBaseColor({0.1f, 0.8f, 0.3f, 1.f}));
		const Vector<Editor::ReferencePickerOption> pickerOptions{
		    {"none", "None", "None", {}, true},
		    {"asset:prefab-material", "Prefab Material", "Material", "Assets/tile.ursaprefab", true},
		    {"asset:trial-material", "Trial Material", "Material", "Assets/trial.ursamat", true}};
		Editor::ReferencePickerTransaction pickerTransaction;
		pickerTransaction.capture = [&]()
		{
			return Result<nlohmann::json>::Success(
			    nlohmann::json{{"firstNull", !firstOriginalMaterial}, {"secondNull", !secondOriginalMaterial}});
		};
		pickerTransaction.preview = [&](Size index)
		{
			auto firstSelected = index == 0   ? std::shared_ptr<MaterialInstance>{}
			                     : index == 1 ? firstOriginalMaterial
			                                  : pickerTrialMaterial;
			auto secondSelected = index == 0   ? std::shared_ptr<MaterialInstance>{}
			                      : index == 1 ? secondOriginalMaterial
			                                   : pickerTrialMaterial;
			auto firstApplied = firstInstanceRenderer->SetSharedMaterial(std::move(firstSelected));
			if (!firstApplied)
			{
				return firstApplied;
			}
			return secondInstanceRenderer->SetSharedMaterial(std::move(secondSelected));
		};
		pickerTransaction.restore = [&](const nlohmann::json& checkpoint)
		{
			auto firstRestored = firstInstanceRenderer->SetSharedMaterial(
			    checkpoint.at("firstNull").get<bool>() ? std::shared_ptr<MaterialInstance>{} : firstOriginalMaterial);
			if (!firstRestored)
			{
				return firstRestored;
			}
			return secondInstanceRenderer->SetSharedMaterial(
			    checkpoint.at("secondNull").get<bool>() ? std::shared_ptr<MaterialInstance>{} : secondOriginalMaterial);
		};
		auto* pickerContext = ImGui::CreateContext();
		ImGui::SetCurrentContext(pickerContext);
		ImGui::GetIO().DisplaySize = {800, 600};
		ImGui::GetIO().DeltaTime = 1.f / 60.f;
		unsigned char* pickerPixels = nullptr;
		int pickerWidth = 0;
		int pickerHeight = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pickerPixels, &pickerWidth, &pickerHeight);
		const String pickerControl = "prefab-selection:material";
		ImGui::ActivateItemByID(PickerButtonId(pickerControl));
		DrawPickerFrame(pickerControl, pickerOptions, "asset:prefab-material", pickerTransaction);
		Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
		       "Multi-object Prefab reference field did not open its object picker");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
		DrawPickerFrame(pickerControl, pickerOptions, "asset:prefab-material", pickerTransaction);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
		Expect(firstInstanceRenderer->SharedMaterial() == pickerTrialMaterial &&
		           secondInstanceRenderer->SharedMaterial() == pickerTrialMaterial,
		       "Picker trial did not preview the candidate on every selected Prefab instance");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		DrawPickerFrame(pickerControl, pickerOptions, "asset:prefab-material", pickerTransaction);
		Expect(firstInstanceRenderer->SharedMaterial() == firstOriginalMaterial &&
		           secondInstanceRenderer->SharedMaterial() == secondOriginalMaterial,
		       "Cancelling a multi-object Prefab picker did not restore each original reference");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
		DrawPickerFrame(pickerControl, pickerOptions, "asset:prefab-material", pickerTransaction);
		Editor::CancelReferencePickerSessions(pickerContext);
		ImGui::DestroyContext(pickerContext);
		Editor::EditorSelectionEntry draggedObjectEntry;
		draggedObjectEntry.kind = Editor::EditorSelectionKind::GameObject;
		draggedObjectEntry.documentId = linkedDocument->Id();
		draggedObjectEntry.objectId = firstScoped;
		const auto setObjectReference = [&](GameObject::Ptr target, const char* scriptType, ResourceId draggedId)
		{
			auto added = linkedDraft->GetScene().AddComponent<Scripting::ScriptBehaviourComponent>(target, scriptType);
			if (!added)
			{
				return Result<void>::Failure(added.error);
			}
			auto* script = target->FindComponent<Scripting::ScriptBehaviourComponent>();
			const auto droppedReference =
			    nlohmann::json{{"$ursaReference", {{"Kind", "GameObject"}, {"Id", ToStdString(draggedId.ToString())}}}};
			script->SetSerializedPayload(ToUrsaString(nlohmann::json{{"FormatVersion", 1},
			                                                         {"ScriptType", scriptType},
			                                                         {"Version", 1},
			                                                         {"Fields", {{"target", droppedReference}}}}
			                                              .dump()));
			return Result<void>{};
		};
		auto firstReferenceTarget = Take(linkedDraft->GetScene().GameObjectById(childId));
		auto secondReferenceTarget = Take(linkedDraft->GetScene().GameObjectById(lastId));
		auto* dragContext = ImGui::CreateContext();
		ImGui::SetCurrentContext(dragContext);
		ImGui::GetIO().DisplaySize = {800, 600};
		ImGui::GetIO().DeltaTime = 1.f / 60.f;
		unsigned char* dragPixels = nullptr;
		int dragWidth = 0;
		int dragHeight = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&dragPixels, &dragWidth, &dragHeight);
		const auto objectDrag = Take(Editor::BeginEditorDragSession({draggedObjectEntry}, 23));
		ImVec2 objectSourceCenter{};
		ImVec2 objectTargetCenter{};
		Size deliveredObjectReferences = 0;
		String dragDiagnostic;
		const auto drawObjectDragFrame = [&]
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({20, 20}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({260, 140}, ImGuiCond_Always);
			ImGui::Begin("Scene Draft Object Drag Source");
			ImGui::Button("Selected GameObject");
			const ImVec2 sourceMinimum = ImGui::GetItemRectMin();
			const ImVec2 sourceMaximum = ImGui::GetItemRectMax();
			objectSourceCenter = {(sourceMinimum.x + sourceMaximum.x) * 0.5f,
			                      (sourceMinimum.y + sourceMaximum.y) * 0.5f};
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload(Editor::editorDragSessionPayloadType, &objectDrag, sizeof(objectDrag));
				ImGui::TextUnformatted("Selected GameObject");
				ImGui::EndDragDropSource();
			}
			ImGui::End();
			ImGui::SetNextWindowPos({360, 20}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({320, 140}, ImGuiCond_Always);
			ImGui::Begin("Scene Draft Object Reference Target");
			ImGui::Button("GameObject reference");
			const ImVec2 targetMinimum = ImGui::GetItemRectMin();
			const ImVec2 targetMaximum = ImGui::GetItemRectMax();
			objectTargetCenter = {(targetMinimum.x + targetMaximum.x) * 0.5f,
			                      (targetMinimum.y + targetMaximum.y) * 0.5f};
			if (ImGui::BeginDragDropTarget())
			{
				if (const auto* payload = Editor::AcceptEditorDragDropPayload())
				{
					auto session = Editor::DecodeEditorDragPayload(payload->DataType, payload->Data,
					                                               static_cast<Size>(payload->DataSize), 23);
					if (!session)
					{
						dragDiagnostic = session.error;
					}
					else
					{
						auto reference = Editor::SingleGameObjectDragReference(session.value);
						if (!reference || reference.value.documentId != linkedDocument->Id())
						{
							dragDiagnostic = reference ? "Dragged object belongs to another document" : reference.error;
						}
						else
						{
							auto firstApplied = setObjectReference(firstReferenceTarget, "test.drag-target.first",
							                                       reference.value.objectId);
							auto secondApplied =
							    firstApplied ? setObjectReference(secondReferenceTarget, "test.drag-target.second",
							                                      reference.value.objectId)
							                 : firstApplied;
							if (!secondApplied)
							{
								dragDiagnostic = secondApplied.error;
							}
							else
							{
								++deliveredObjectReferences;
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::End();
			ImGui::Render();
		};
		drawObjectDragFrame();
		ImGui::GetIO().AddMousePosEvent(objectSourceCenter.x, objectSourceCenter.y);
		drawObjectDragFrame();
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		drawObjectDragFrame();
		ImGui::GetIO().AddMousePosEvent(objectSourceCenter.x + 14.f, objectSourceCenter.y);
		drawObjectDragFrame();
		Expect(ImGui::GetDragDropPayload() &&
		           ImGui::GetDragDropPayload()->IsDataType(Editor::editorDragSessionPayloadType),
		       "Dragging the selected GameObject did not create an ImGui DragSession payload");
		ImGui::GetIO().AddMousePosEvent(objectTargetCenter.x, objectTargetCenter.y);
		drawObjectDragFrame();
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		drawObjectDragFrame();
		Expect(dragDiagnostic.empty() && deliveredObjectReferences == 1,
		       "Dropping the selected GameObject on multi-target reference fields did not commit exactly once");
		Editor::EndEditorDragSession(objectDrag.token);
		ImGui::DestroyContext(dragContext);
		done(linkedDraft->GetScene().SetGameObjectName(firstNode, "Instance override"));
		Take(linkedDraft->GetScene().CreateGameObject(firstNode));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabChildId, "/name", "Instance override"));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabBox->PersistentId(), "/data/castShadows", true));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabBox->PersistentId(), "/data/receiveShadows", true));
		const auto beforeInvalidBatch = linkedDraft->LinkedScene()->document;
		Vector<Editor::SceneInstancePropertyOverride> invalidBatch{
		    {secondInstance, prefabBox->PersistentId(), "/data/castShadows", true},
		    {thirdInstance, prefabBox->PersistentId(), "/not-an-override", true}};
		Expect(!linkedDraft->SetInstanceProperties(invalidBatch) &&
		           linkedDraft->LinkedScene()->document == beforeInvalidBatch,
		       "A failed multi-instance override batch did not restore the linked scene document");
		Vector<Editor::SceneInstancePropertyOverride> sharedBatch{
		    {firstInstance, prefabBox->PersistentId(), "/data/castShadows", true},
		    {secondInstance, prefabBox->PersistentId(), "/data/castShadows", true}};
		done(linkedDraft->SetInstanceProperties(sharedBatch));
		Expect(Take(linkedDraft->GetScene().GameObjectName(secondNode)) == "Prefab child",
		       "Editing first placement changed the second placement");
		const auto sourceBefore = Take(ReadJsonResource(prefabPath, ResourceKind::Prefab));
		linkedDocument->MarkDirty();
		done(documents->RequestOpen(std::move(linkedDocument)));
		done(documents->Current()->Dirty() ? Result<void>{} : Result<void>::Failure("Expected dirty scene"));
		done(documents->SaveCurrent());
		auto linkedReopened = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                     ResourceKind::Scene, registry, documents->Session()));
		auto reloadedDraft = linkedReopened->Payload<Editor::SceneDraft>();
		const auto reloadedFirst = Take(reloadedDraft->GetScene().GameObjectById(firstScoped));
		const auto reloadedSecond = Take(reloadedDraft->GetScene().GameObjectById(secondScoped));
		const auto reloadedThird = Take(reloadedDraft->GetScene().GameObjectById(thirdScoped));
		const auto* reloadedFirstRenderer = reloadedFirst->FindComponent<MeshRendererComponent>();
		const auto* reloadedSecondRenderer = reloadedSecond->FindComponent<MeshRendererComponent>();
		const auto* reloadedThirdRenderer = reloadedThird->FindComponent<MeshRendererComponent>();
		const auto reloadedFirstReferenceTarget = Take(reloadedDraft->GetScene().GameObjectById(childId))
		                                              ->FindComponent<Scripting::ScriptBehaviourComponent>();
		const auto reloadedSecondReferenceTarget = Take(reloadedDraft->GetScene().GameObjectById(lastId))
		                                               ->FindComponent<Scripting::ScriptBehaviourComponent>();
		const auto firstReferencePayload =
		    reloadedFirstReferenceTarget
		        ? nlohmann::json::parse(ToStdString(reloadedFirstReferenceTarget->SerializedPayload()))
		        : nlohmann::json::object();
		const auto secondReferencePayload =
		    reloadedSecondReferenceTarget
		        ? nlohmann::json::parse(ToStdString(reloadedSecondReferenceTarget->SerializedPayload()))
		        : nlohmann::json::object();
		Expect(Take(reloadedDraft->GetScene().GameObjectName(reloadedFirst)) == "Instance override" &&
		           Take(reloadedDraft->GetScene().GameObjectName(reloadedSecond)) == "Prefab child" &&
		           reloadedFirstRenderer && reloadedFirstRenderer->CastShadows() &&
		           reloadedFirstRenderer->ReceiveShadows() && reloadedSecondRenderer &&
		           reloadedSecondRenderer->CastShadows() && !reloadedSecondRenderer->ReceiveShadows() &&
		           reloadedThirdRenderer && !reloadedThirdRenderer->CastShadows() &&
		           !reloadedThirdRenderer->ReceiveShadows() && reloadedFirstReferenceTarget &&
		           reloadedSecondReferenceTarget &&
		           firstReferencePayload["Fields"]["target"]["$ursaReference"]["Id"] ==
		               ToStdString(firstScoped.ToString()) &&
		           secondReferencePayload["Fields"]["target"]["$ursaReference"]["Id"] ==
		               ToStdString(firstScoped.ToString()) &&
		           Take(reloadedDraft->GetScene().ChildrenOf(reloadedFirst)).size() == 1,
		       "Prefab overrides or dragged object references changed target identities or did not round-trip");
		Expect(Take(ReadJsonResource(prefabPath, ResourceKind::Prefab)) == sourceBefore,
		       "Saving an instance modified the Prefab source");
		std::filesystem::remove_all(root);
		std::cout << "Editor scene and Prefab document test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
