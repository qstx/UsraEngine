#include <MeshFixtures.hpp>
#include <ResourceFixtures.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <imgui_internal.h>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <optional>
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

} // namespace

int main()
{
	const auto resourceRoot =
	    std::filesystem::current_path() / ("hierarchy_resources_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		{
			Expect(Editor::ClassifyHierarchyDropZone(5.0f, 0.0f, 40.0f) == Editor::HierarchyDropZone::Above,
			       "The upper hierarchy row zone was classified incorrectly");
			Expect(Editor::ClassifyHierarchyDropZone(20.0f, 0.0f, 40.0f) == Editor::HierarchyDropZone::Into,
			       "The center hierarchy row zone was classified incorrectly");
			Expect(Editor::ClassifyHierarchyDropZone(35.0f, 0.0f, 40.0f) == Editor::HierarchyDropZone::Below,
			       "The lower hierarchy row zone was classified incorrectly");
			const auto document = ResourceId::Generate();
			const auto parent = ResourceId::Generate();
			const auto child = ResourceId::Generate();
			const auto sibling = ResourceId::Generate();
			Editor::EditorDragSession session;
			for (const auto id : {child, parent, sibling})
			{
				Editor::EditorSelectionEntry entry;
				entry.kind = Editor::EditorSelectionKind::GameObject;
				entry.documentId = document;
				entry.objectId = id;
				session.sources.push_back({entry, {}, {}});
			}
			const auto normalized = Take(Editor::NormalizeHierarchyDragSources(
			    session, document, false, [&](ResourceId id) { return id == child ? parent : ResourceId{}; }));
			Expect(normalized.size() == 2 && normalized[0].objectId == parent && normalized[1].objectId == sibling,
			       "A hierarchy drag retained a selected descendant or changed source order");
			session.sources.front().identity.documentId = ResourceId::Generate();
			Expect(!Editor::NormalizeHierarchyDragSources(session, document, false,
			                                              [](ResourceId) { return ResourceId{}; }),
			       "A hierarchy drag accepted a GameObject from another document");
		}
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		{
			Scene moving;
			const auto document = ResourceId::Generate();
			auto source = Take(moving.CreateGameObject("Source"));
			auto destination = Take(moving.CreateGameObject("Destination"));
			LocalTransform sourceTransform;
			sourceTransform.position = {5.0f, 2.0f, -1.0f};
			sourceTransform.rotation = Quat::FromAxisAngle({0, 0, 1}, 0.4f);
			done(moving.SetLocalTransform(source, sourceTransform));
			LocalTransform destinationTransform;
			destinationTransform.position = {-2.0f, 4.0f, 3.0f};
			destinationTransform.rotation = sourceTransform.rotation;
			destinationTransform.scale = {2.0f, 3.0f, 4.0f};
			done(moving.SetLocalTransform(destination, destinationTransform));
			const Mat4 before = Take(moving.WorldTransform(source));
			Editor::EditorSelectionEntry sourceEntry;
			sourceEntry.kind = Editor::EditorSelectionKind::GameObject;
			sourceEntry.documentId = document;
			sourceEntry.objectId = Take(moving.PersistentId(source));
			Editor::EditorDragSession drag{1, 1, {{sourceEntry, {}, {}}}, false};
			done(Editor::ApplyHierarchyDrop(moving, drag, Take(moving.PersistentId(destination)),
			                                Editor::HierarchyDropZone::Into, document));
			Expect(Take(moving.ParentOf(source)) == destination, "Hierarchy drop did not reparent the source");
			const Mat4 after = Take(moving.WorldTransform(source));
			for (Size index = 0; index < before.values.size(); ++index)
			{
				Expect(std::abs(before.values[index] - after.values[index]) < 0.002f,
				       "Hierarchy reparenting changed the source world transform");
			}
			auto sibling = Take(moving.CreateGameObject("Sibling"));
			const auto siblingEntryId = Take(moving.PersistentId(sibling));
			Editor::EditorSelectionEntry siblingEntry = sourceEntry;
			siblingEntry.objectId = siblingEntryId;
			Editor::EditorDragSession siblingDrag{2, 1, {{siblingEntry, {}, {}}}, false};
			done(Editor::ApplyHierarchyDrop(moving, siblingDrag, Take(moving.PersistentId(destination)),
			                                Editor::HierarchyDropZone::Above, document));
			Expect(moving.RootGameObjects().front() == sibling,
			       "Hierarchy insertion feedback and the resulting root sibling order diverged");
			Editor::EditorDragSession cycleDrag{3, 1, {{sourceEntry, {}, {}}}, false};
			const auto cycleBefore = Take(moving.WorldTransform(source));
			Expect(!Editor::ApplyHierarchyDrop(moving, cycleDrag, Take(moving.PersistentId(source)),
			                                   Editor::HierarchyDropZone::Into, document),
			       "Hierarchy drop accepted a cycle");
			Expect(Take(moving.ParentOf(source)) == destination &&
			           Take(moving.WorldTransform(source)).values == cycleBefore.values,
			       "Rejected cyclic hierarchy drop partially changed the graph");
			auto singular = Take(moving.CreateGameObject("Singular"));
			LocalTransform singularTransform;
			singularTransform.scale = {0.0f, 1.0f, 1.0f};
			done(moving.SetLocalTransform(singular, singularTransform));
			Expect(!Editor::ApplyHierarchyDrop(moving, drag, Take(moving.PersistentId(singular)),
			                                   Editor::HierarchyDropZone::Into, document),
			       "Hierarchy drop accepted a singular destination transform");
			Expect(Take(moving.ParentOf(source)) == destination,
			       "Rejected singular hierarchy drop partially changed the graph");
			auto shearParent = Take(moving.CreateGameObject("Shear Parent"));
			LocalTransform shearParentTransform;
			shearParentTransform.scale = {2.0f, 1.0f, 1.0f};
			done(moving.SetLocalTransform(shearParent, shearParentTransform));
			auto rotatedSource = Take(moving.CreateGameObject("Rotated Source"));
			LocalTransform rotatedSourceTransform;
			rotatedSourceTransform.rotation = Quat::FromAxisAngle({0, 0, 1}, 0.4f);
			done(moving.SetLocalTransform(rotatedSource, rotatedSourceTransform));
			Editor::EditorSelectionEntry rotatedEntry = sourceEntry;
			rotatedEntry.objectId = Take(moving.PersistentId(rotatedSource));
			Editor::EditorDragSession rotatedDrag{4, 1, {{rotatedEntry, {}, {}}}, false};
			Expect(!Editor::ApplyHierarchyDrop(moving, rotatedDrag, Take(moving.PersistentId(shearParent)),
			                                   Editor::HierarchyDropZone::Into, document),
			       "Hierarchy drop accepted a local transform containing shear");
			Expect(!Take(moving.ParentOf(rotatedSource)),
			       "Rejected sheared hierarchy drop partially changed the graph");
			auto batchSourceA = Take(moving.CreateGameObject("Batch A"));
			auto batchSourceB = Take(moving.CreateGameObject("Batch B"));
			LocalTransform batchTransform;
			batchTransform.rotation = sourceTransform.rotation;
			done(moving.SetLocalTransform(batchSourceA, batchTransform));
			done(moving.SetLocalTransform(batchSourceB, batchTransform));
			Editor::EditorSelectionEntry batchEntryA = sourceEntry;
			batchEntryA.objectId = Take(moving.PersistentId(batchSourceA));
			Editor::EditorSelectionEntry batchEntryB = sourceEntry;
			batchEntryB.objectId = Take(moving.PersistentId(batchSourceB));
			Editor::EditorDragSession batchDrag{5, 1, {{batchEntryA, {}, {}}, {batchEntryB, {}, {}}}, false};
			done(Editor::ApplyHierarchyDrop(moving, batchDrag, Take(moving.PersistentId(destination)),
			                                Editor::HierarchyDropZone::Into, document));
			const auto destinationChildren = Take(moving.ChildrenOf(destination));
			Expect(destinationChildren.size() == 3 && destinationChildren[1] == batchSourceA &&
			           destinationChildren[2] == batchSourceB,
			       "A multi-object hierarchy drop lost source order or split the transaction");
			auto validBatchSource = Take(moving.CreateGameObject("Valid Batch Source"));
			Editor::EditorSelectionEntry validBatchEntry = sourceEntry;
			validBatchEntry.objectId = Take(moving.PersistentId(validBatchSource));
			Editor::EditorDragSession failedBatch{6, 1, {{validBatchEntry, {}, {}}, {rotatedEntry, {}, {}}}, false};
			Expect(!Editor::ApplyHierarchyDrop(moving, failedBatch, Take(moving.PersistentId(shearParent)),
			                                   Editor::HierarchyDropZone::Into, document),
			       "A hierarchy batch accepted one source whose new local transform contains shear");
			Expect(!Take(moving.ParentOf(validBatchSource)) && !Take(moving.ParentOf(rotatedSource)),
			       "A rejected multi-object hierarchy drop partially modified the scene");
			Editor::EditorDragSession foreignGraphDrag{7, 1, {{validBatchEntry, {}, {}}}, false};
			foreignGraphDrag.sources.front().identity.objectId = ResourceId::Generate();
			Expect(!Editor::ApplyHierarchyDrop(moving, foreignGraphDrag, Take(moving.PersistentId(destination)),
			                                   Editor::HierarchyDropZone::Into, document),
			       "A hierarchy drop accepted a GameObject from another scene graph");
		}
		Scene scene;
		const auto first = Take(scene.CreateGameObject());
		const auto child = Take(scene.CreateGameObject(first));
		const auto second = Take(scene.CreateGameObject());
		const auto external = Take(scene.CreateGameObject("External"));
		done(scene.ReorderGameObject(second, 0));
		done(scene.SetGameObjectName(first, "Root"));
		done(scene.SetGameObjectName(child, "Child"));
		done(scene.SetActive(child, false));
		Take(Ursa::Test::AddRenderer(scene, first));
		const auto firstId = Take(scene.PersistentId(first));
		const auto childId = Take(scene.PersistentId(child));
		const auto secondId = Take(scene.PersistentId(second));
		const auto externalId = Take(scene.PersistentId(external));
		auto referenceScriptId = Take(scene.AddComponent<Scripting::ScriptBehaviourComponent>(first, String("Test")));
		auto* referenceScript =
		    static_cast<Scripting::ScriptBehaviourComponent*>(scene.GetComponent(referenceScriptId));
		nlohmann::json scriptPayload = nlohmann::json::object();
		scriptPayload["FormatVersion"] = 1u;
		scriptPayload["ScriptType"] = "Test";
		scriptPayload["Version"] = 1u;
		scriptPayload["Fields"] = nlohmann::json::object();
		scriptPayload["Fields"]["nested"]["$ursaReference"]["Kind"] = "GameObject";
		scriptPayload["Fields"]["nested"]["$ursaReference"]["Id"] = ToStdString(childId.ToString());
		scriptPayload["Fields"]["sibling"]["$ursaReference"]["Kind"] = "GameObject";
		scriptPayload["Fields"]["sibling"]["$ursaReference"]["Id"] = ToStdString(secondId.ToString());
		scriptPayload["Fields"]["external"]["$ursaReference"]["Kind"] = "GameObject";
		scriptPayload["Fields"]["external"]["$ursaReference"]["Id"] = ToStdString(externalId.ToString());
		referenceScript->SetSerializedPayload(ToUrsaString(scriptPayload.dump()));
		Editor::EditorHierarchy hierarchy;
		done(hierarchy.Select(scene, child));

		const auto path = std::filesystem::current_path() / "hierarchy.ursascene";
		std::filesystem::create_directories(resourceRoot);
		{
			const auto linkedPrefabPath = resourceRoot / "hierarchy_move.ursaprefab";
			const auto linkedScenePath = resourceRoot / "hierarchy_move.ursascene";
			Scene prefabSource;
			auto prefabRoot = Take(prefabSource.CreateGameObject("Prefab Root"));
			auto prefabFirst = Take(prefabSource.CreateGameObject("First", prefabRoot));
			auto prefabSecond = Take(prefabSource.CreateGameObject("Second", prefabRoot));
			auto prefabThird = Take(prefabSource.CreateGameObject("Third", prefabRoot));
			const auto prefabId = ResourceId::Generate();
			const auto prefabFirstId = Take(prefabSource.PersistentId(prefabFirst));
			const auto prefabSecondId = Take(prefabSource.PersistentId(prefabSecond));
			const auto prefabThirdId = Take(prefabSource.PersistentId(prefabThird));
			done(SavePrefabResource(linkedPrefabPath, prefabId, prefabSource, prefabRoot, registry));
			Scene emptyScene;
			const auto sceneId = ResourceId::Generate();
			auto linkedDocument = Take(EncodeSceneDocument(linkedScenePath, sceneId, emptyScene, registry));
			const auto instanceId = ResourceId::Generate();
			done(Ursa::AddPrefabInstance(linkedDocument, prefabId, instanceId));
			done(WriteJsonResource(linkedScenePath, linkedDocument));
			done(WriteResourceMetadataTree(resourceRoot));
			auto linkedSession = Take(ResourceSession::Open({resourceRoot}));
			ComponentLoadContext linkedContext(*linkedSession);
			auto linked = Take(LoadLinkedSceneResource(linkedScenePath, registry, linkedContext));
			const auto scope = linked.scopedIds.find(instanceId.ToString());
			Expect(scope != linked.scopedIds.end(), "Linked Prefab instance mapping was not loaded");
			auto runtimeId = [&](ResourceId localId)
			{
				const auto item = scope->second.find(localId.ToString());
				if (item == scope->second.end())
				{
					throw std::runtime_error("Linked Prefab object mapping is missing");
				}
				return Take(ResourceId::Parse(item->second));
			};
			const ResourceId runtimeFirstId = runtimeId(prefabFirstId);
			const ResourceId runtimeSecondId = runtimeId(prefabSecondId);
			const ResourceId runtimeThirdId = runtimeId(prefabThirdId);
			Editor::EditorSelectionEntry linkedSource;
			linkedSource.kind = Editor::EditorSelectionKind::GameObject;
			linkedSource.documentId = linked.id;
			linkedSource.objectId = runtimeSecondId;
			Editor::EditorDragSession linkedDrag{5, 1, {{linkedSource, {}, {}}}, false};
			done(Editor::ApplyHierarchyDrop(*linked.scene, linkedDrag, runtimeFirstId, Editor::HierarchyDropZone::Into,
			                                linked.id));
			linkedSource.objectId = runtimeThirdId;
			Editor::EditorDragSession linkedOrderDrag{6, 1, {{linkedSource, {}, {}}}, false};
			done(Editor::ApplyHierarchyDrop(*linked.scene, linkedOrderDrag, runtimeFirstId,
			                                Editor::HierarchyDropZone::Above, linked.id));
			done(SaveLinkedSceneResource(linkedScenePath, linked, registry));
			ComponentLoadContext linkedReloadContext(*linkedSession);
			done(ReloadLinkedSceneResource(linkedScenePath, registry, linkedReloadContext, linked));
			auto reopenedFirst = Take(linked.scene->GameObjectById(runtimeFirstId));
			auto reopenedSecond = Take(linked.scene->GameObjectById(runtimeSecondId));
			auto reopenedThird = Take(linked.scene->GameObjectById(runtimeThirdId));
			Expect(Take(linked.scene->ParentOf(reopenedSecond)) == reopenedFirst,
			       "Hierarchy reparenting was not restored from a linked Prefab structural override");
			const auto reopenedPrefabRoot = linked.scene->RootGameObjects().front();
			Expect(Take(linked.scene->ChildrenOf(reopenedPrefabRoot)).front() == reopenedThird,
			       "Hierarchy ordering was not restored from a linked Prefab structural override");
		}
		const auto resources = Take(ResourceSession::Open(resourceRoot));
		ComponentLoadContext independentExportContext(*resources);
		auto independentRootExport =
		    CapturePrefabAsset(ResourceId::Generate(), scene, first, registry, independentExportContext);
		Expect(!independentRootExport,
		       "An independently exported root silently retained a GameObject reference to another selected root");
		ComponentLoadContext context(*resources);
		auto capturedSelection = CapturePrefabAsset(ResourceId::Generate(), scene,
		                                            Vector<GameObject::Ptr>{first, second}, registry, context);
		if (!capturedSelection)
		{
			throw std::runtime_error("Multi-root capture failed: " + ToStdString(capturedSelection.error));
		}
		auto selectionPrefab = std::move(capturedSelection.value);
		context.ClearMappings();
		done(context.MapGameObject(externalId, external));
		auto instantiatedSelection = selectionPrefab.Instantiate(scene, registry, context, {}, true);
		if (!instantiatedSelection)
		{
			throw std::runtime_error("Multi-root instantiate failed: " + ToStdString(instantiatedSelection.error));
		}
		auto duplicateGroup = std::move(instantiatedSelection.value);
		const auto duplicateRoots = Take(scene.ChildrenOf(duplicateGroup));
		Expect(duplicateRoots.size() == 2, "Multi-root duplicate capture lost a selected root");
		const auto duplicateFirstId = Take(scene.PersistentId(duplicateRoots[0]));
		const auto duplicateSecondId = Take(scene.PersistentId(duplicateRoots[1]));
		Expect(duplicateFirstId != firstId && duplicateSecondId != secondId && duplicateFirstId != duplicateSecondId,
		       "Multi-root duplicate capture reused a source GameObject identity");
		auto* duplicateScript = scene.FindComponent<Scripting::ScriptBehaviourComponent>(duplicateRoots[0]);
		Expect(duplicateScript != nullptr, "Multi-root duplicate capture lost a selected component");
		const auto clonedPayload = nlohmann::json::parse(ToStdString(duplicateScript->SerializedPayload()));
		const auto duplicateChildren = Take(scene.ChildrenOf(duplicateRoots[0]));
		Expect(clonedPayload["Fields"]["nested"]["$ursaReference"]["Id"] ==
		               ToStdString(Take(scene.PersistentId(duplicateChildren.front())).ToString()) &&
		           clonedPayload["Fields"]["sibling"]["$ursaReference"]["Id"] ==
		               ToStdString(duplicateSecondId.ToString()) &&
		           clonedPayload["Fields"]["external"]["$ursaReference"]["Id"] == ToStdString(externalId.ToString()),
		       "Duplicate failed to remap internal references while retaining a valid external reference");
		done(scene.SetParent(duplicateRoots[0]));
		done(scene.SetParent(duplicateRoots[1]));
		done(scene.DestroyGameObject(duplicateGroup));
		const auto duplicateDocument = Take(EncodeSceneDocument(path, ResourceId::Generate(), scene, registry));
		context.ClearMappings();
		auto duplicateReopened = Take(DecodeSceneDocument(path, duplicateDocument, registry, context));
		Expect(duplicateReopened.scene->GameObjectById(duplicateFirstId) &&
		           duplicateReopened.scene->GameObjectById(duplicateSecondId),
		       "Duplicated GameObject identities did not survive save and reopen");
		const auto reopenedDuplicateChildren =
		    Take(duplicateReopened.scene->ChildrenOf(Take(duplicateReopened.scene->GameObjectById(duplicateFirstId))));
		Expect(reopenedDuplicateChildren.size() == 1 &&
		           Take(duplicateReopened.scene->PersistentId(reopenedDuplicateChildren.front())) != childId,
		       "Duplicated subtree children did not keep their new identity after save and reopen");
		done(scene.DestroyGameObject(duplicateRoots[0]));
		done(scene.DestroyGameObject(duplicateRoots[1]));
		referenceScript->SetSerializedPayload(
		    "{\"FormatVersion\":1,\"ScriptType\":\"Test\",\"Version\":1,\"Fields\":{}}");
		const auto document = Take(EncodeSceneDocument(path, ResourceId::Generate(), scene, registry));
		auto loaded = Take(DecodeSceneDocument(path, document, registry, context));
		Expect(loaded.scene->RootGameObjects().size() == 3, "Root count changed after save/reopen");
		Expect(Take(loaded.scene->PersistentId(loaded.scene->RootGameObjects()[0])) == secondId,
		       "Root order changed after save/reopen");
		Expect(Take(loaded.scene->PersistentId(loaded.scene->RootGameObjects()[1])) == firstId,
		       "Root ID changed after save/reopen");
		const auto reopenedChild = Take(loaded.scene->GameObjectById(childId));
		Expect(Take(loaded.scene->GameObjectName(reopenedChild)) == "Child", "Child label was not restored");
		Expect(!Take(loaded.scene->ActiveSelf(reopenedChild)), "Child activeSelf state changed");
		Expect(Take(loaded.scene->PersistentId(Take(hierarchy.SelectedGameObject(*loaded.scene)))) == childId,
		       "Hierarchy selection did not survive scene replacement");

		auto legacy = document;
		legacy.erase("sceneGraphVersion");
		auto oldScene = DecodeSceneDocument(path, legacy, registry, context);
		Expect(!oldScene &&
		           oldScene.error.find("Unsupported or missing GameObject Scene format version") != String::npos,
		       "Old Scene format was not rejected with a version diagnostic");

		const auto prefabPath = std::filesystem::current_path() / "hierarchy.ursaprefab";
		done(SavePrefabResource(prefabPath, ResourceId::Generate(), scene, first, registry));
		auto prefab = Take(LoadPrefabResource(prefabPath, registry, context));
		Expect(Take(prefab.scene->GameObjectName(Take(prefab.scene->GameObjectById(firstId)))) == "Root",
		       "Prefab root label was not restored");
		Expect(Take(prefab.scene->GameObjectName(Take(prefab.scene->GameObjectById(childId)))) == "Child",
		       "Prefab child label was not restored");

		auto* imgui = ImGui::CreateContext();
		ImGui::SetCurrentContext(imgui);
		ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		ImGui::GetIO().DisplaySize = ImVec2(640, 480);
		ImGui::NewFrame();
		const auto displayed = hierarchy.Draw(*loaded.scene);
		Expect(displayed && !displayed.value, "Hierarchy display changed scene data unexpectedly");
		Expect(ImGui::FindWindowByName("Inspector") == nullptr, "Hierarchy opened a second Inspector window");
		ImGui::Render();
		done(loaded.scene->DestroyGameObject(reopenedChild));
		ImGui::NewFrame();
		auto cleared = hierarchy.Draw(*loaded.scene);
		Expect(cleared && !hierarchy.SelectedId(), "Deleting the selected GameObject left a stale Inspector selection");
		ImGui::Render();

		const ResourceId selectionDocument = ResourceId::Generate();
		Editor::EditorSelectionContext sharedSelection;
		Expect(sharedSelection.SelectGameObject(selectionDocument, secondId), "Cannot seed shared hierarchy selection");
		bool hierarchyOpen = true;
		std::optional<Editor::EditorDragSession> draggedGameObject;
		auto drawInputFrame =
		    [&](ImVec2 mousePosition, std::optional<bool> leftDown, std::optional<bool> rightDown = {})
		{
			ImGui::GetIO().AddMousePosEvent(mousePosition.x, mousePosition.y);
			if (leftDown)
			{
				ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, *leftDown);
			}
			if (rightDown)
			{
				ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, *rightDown);
			}
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize({360, 260}, ImGuiCond_Always);
			const auto drawn =
			    hierarchy.Draw(*loaded.scene, &hierarchyOpen, nullptr, &sharedSelection, selectionDocument);
			Expect(static_cast<bool>(drawn), "Hierarchy failed during deferred-selection input frame");
			if (const auto* payload = ImGui::GetDragDropPayload();
			    payload && payload->IsDataType(Editor::editorDragSessionPayloadType))
			{
				auto decoded = Editor::DecodeEditorDragPayload(payload->DataType, payload->Data, payload->DataSize);
				if (decoded)
				{
					draggedGameObject = std::move(decoded.value);
				}
			}
			auto* hierarchyWindow = ImGui::FindWindowByName("Hierarchy");
			const ImVec2 workMin = hierarchyWindow->WorkRect.Min;
			const ImVec2 workMax = hierarchyWindow->WorkRect.Max;
			ImGui::Render();
			Editor::PruneEditorDragSessions();
			return std::make_tuple(workMin, workMax, ImGui::GetFrameHeight());
		};
		auto row = drawInputFrame({-10, -10}, false);
		const ImVec2 rowCenter{std::get<0>(row).x + 100.0f, std::get<0>(row).y + std::get<2>(row) * 1.5f};
		drawInputFrame(rowCenter, true);
		Expect(sharedSelection.objectId == secondId,
		       "Hierarchy published a selection before click or drag was resolved");
		drawInputFrame({rowCenter.x + 80, rowCenter.y}, std::nullopt);
		Expect(draggedGameObject && draggedGameObject->sources.size() == 1 &&
		           draggedGameObject->sources.front().identity.documentId == selectionDocument &&
		           draggedGameObject->sources.front().identity.objectId == firstId,
		       "Dragging a Hierarchy row did not publish its document-scoped GameObject reference");
		drawInputFrame({rowCenter.x + 80, rowCenter.y}, false);
		Expect(sharedSelection.objectId == secondId, "Dragging a hierarchy row changed the shared selection");
		drawInputFrame(rowCenter, true);
		Expect(sharedSelection.objectId == secondId, "Hierarchy click changed selection on mouse-down");
		drawInputFrame(rowCenter, false);
		Expect(sharedSelection.objectId == firstId, "A completed hierarchy click did not publish its selection");
		Expect(sharedSelection.SelectGameObject(selectionDocument, secondId),
		       "Cannot reset hierarchy context selection");
		Editor::EditorSelectionEntry firstContextEntry;
		firstContextEntry.kind = Editor::EditorSelectionKind::GameObject;
		firstContextEntry.documentId = selectionDocument;
		firstContextEntry.objectId = firstId;
		Editor::EditorSelectionEntry secondContextEntry = firstContextEntry;
		secondContextEntry.objectId = secondId;
		Expect(sharedSelection.SelectEntries({firstContextEntry, secondContextEntry}, 0, firstContextEntry),
		       "Cannot seed hierarchy group drag selection");
		draggedGameObject.reset();
		drawInputFrame(rowCenter, true);
		drawInputFrame({rowCenter.x + 80, rowCenter.y}, std::nullopt);
		Expect(draggedGameObject && draggedGameObject->sources.size() == 2 &&
		           draggedGameObject->sources[0].identity.objectId == firstId &&
		           draggedGameObject->sources[1].identity.objectId == secondId,
		       "Dragging a selected Hierarchy row did not preserve the ordered selection group");
		drawInputFrame({rowCenter.x + 80, rowCenter.y}, false);
		Expect(sharedSelection.objectId == firstId,
		       "Dragging a selected Hierarchy group changed its primary selection");
		Expect(sharedSelection.SelectGameObject(selectionDocument, firstId),
		       "Cannot seed hierarchy drop interaction selection");
		const auto initialRoots = loaded.scene->RootGameObjects();
		drawInputFrame(rowCenter, true);
		drawInputFrame({-10, -10}, std::nullopt);
		Expect(draggedGameObject.has_value(), "The hierarchy drag did not remain active before release");
		const Editor::EditorDragPayload invalidReleasePayload{draggedGameObject->token,
		                                                      draggedGameObject->selectionRevision};
		drawInputFrame({-10, -10}, false);
		Expect(loaded.scene->RootGameObjects() == initialRoots,
		       "Releasing a hierarchy drag outside a valid target changed the scene");
		drawInputFrame({-10, -10}, false);
		Expect(!Editor::ResolveEditorDragSession(invalidReleasePayload),
		       "A released hierarchy drag kept its session token alive");
		drawInputFrame({-10, -10}, false);
		Expect(loaded.scene->RootGameObjects() == initialRoots,
		       "A released hierarchy drag caused a delayed second scene mutation");
		Expect(sharedSelection.SelectGameObject(selectionDocument, firstId),
		       "Cannot seed hierarchy context selection rejection");
		sharedSelection.beforeChange = [](const auto&) { return false; };
		drawInputFrame(rowCenter, std::nullopt, true);
		Expect(sharedSelection.objectId == firstId,
		       "A rejected hierarchy selection change still changed the selection");
		Expect(ImGui::GetCurrentContext()->OpenPopupStack.empty(),
		       "A rejected hierarchy selection change still opened a context menu");
		drawInputFrame(rowCenter, std::nullopt, false);
		sharedSelection.beforeChange = {};
		drawInputFrame(rowCenter, std::nullopt, true);
		Expect(sharedSelection.objectId == secondId, "Right-clicking an unselected hierarchy row did not target it");
		Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
		       "Right-clicking a hierarchy row opened no context menu");
		ImGui::ClosePopupToLevel(0, true);
		drawInputFrame(rowCenter, std::nullopt, false);
		Expect(sharedSelection.SelectEntries({secondContextEntry, firstContextEntry}, 1, firstContextEntry),
		       "Cannot seed multi-object context selection");
		drawInputFrame(rowCenter, std::nullopt, true);
		Expect(sharedSelection.entries.size() == 2 && sharedSelection.PrimaryEntry()->objectId == firstId,
		       "Right-clicking a selected Hierarchy row discarded the rest of the multi-selection");
		auto* hierarchyPopup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
		drawInputFrame(rowCenter, std::nullopt);
		drawInputFrame({hierarchyPopup->Pos.x + hierarchyPopup->Size.x / 2, hierarchyPopup->Pos.y + 16.0f}, true);
		drawInputFrame({hierarchyPopup->Pos.x + hierarchyPopup->Size.x / 2, hierarchyPopup->Pos.y + 16.0f}, false);
		const auto createdChildren = Take(loaded.scene->ChildrenOf(Take(loaded.scene->GameObjectById(firstId))));
		Expect(createdChildren.size() == 1 && sharedSelection.PrimaryEntry() &&
		           Take(loaded.scene->PersistentId(createdChildren.front())) ==
		               sharedSelection.PrimaryEntry()->objectId &&
		           sharedSelection.PrimaryEntry()->documentId == selectionDocument,
		       "Hierarchy Create Empty did not create and select one child of the captured object");
		Editor::EditorSelectionEntry firstSelection;
		firstSelection.kind = Editor::EditorSelectionKind::GameObject;
		firstSelection.documentId = selectionDocument;
		firstSelection.objectId = firstId;
		Editor::EditorSelectionEntry secondSelection = firstSelection;
		secondSelection.objectId = secondId;
		Expect(sharedSelection.SelectEntries({firstSelection, secondSelection}, 0),
		       "Cannot seed a multi-object hierarchy reconciliation test");
		done(loaded.scene->DestroyGameObject(Take(loaded.scene->GameObjectById(firstId))));
		ImGui::NewFrame();
		const auto reconciled =
		    hierarchy.Draw(*loaded.scene, &hierarchyOpen, nullptr, &sharedSelection, selectionDocument);
		Expect(reconciled && sharedSelection.entries.size() == 2 && sharedSelection.entries[0].missing &&
		           !sharedSelection.entries[1].missing && sharedSelection.PrimaryEntry()->objectId == secondId &&
		           hierarchy.SelectedId() == secondId,
		       "Deleting one selected object lost the survivor or left a stale primary target");
		ImGui::Render();
		ImGui::DestroyContext(imgui);
		std::filesystem::remove_all(resourceRoot);
		std::cout << "Editor hierarchy persistence test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(resourceRoot);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
