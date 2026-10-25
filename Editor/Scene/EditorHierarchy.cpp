#include <Ui/EditorIcons.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Core/Types.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <imgui_internal.h>

namespace Ursa::Editor
{
namespace
{
EditorSelectionEntry GameObjectSelection(ResourceId documentId, ResourceId objectId, bool playSession = false)
{
	EditorSelectionEntry entry;
	entry.kind = EditorSelectionKind::GameObject;
	entry.documentId = documentId;
	entry.objectId = objectId;
	entry.playSession = playSession;
	return entry;
}

void AppendVisibleRows(const Scene& scene, GameObject::Ptr object, ResourceId documentId,
                       Vector<EditorSelectionEntry>& visibleRows, const char* treeNodeId, bool playSession)
{
	const auto id = scene.PersistentId(object);
	const auto children = scene.ChildrenOf(object);
	if (!id || !children)
	{
		return;
	}
	visibleRows.push_back(GameObjectSelection(documentId, id.value, playSession));
	if (children.value.empty())
	{
		return;
	}
	const String key = id.value.ToString();
	ImGui::PushID(key.c_str());
	const bool open = ImGui::TreeNodeGetOpen(ImGui::GetID(treeNodeId));
	ImGui::PopID();
	if (open)
	{
		for (const auto& child : children.value)
		{
			AppendVisibleRows(scene, child, documentId, visibleRows, treeNodeId, playSession);
		}
	}
}

void AppendVisibleRows(const GameObject::Ptr& object, ResourceId documentId, Vector<EditorSelectionEntry>& visibleRows,
                       const char* treeNodeId)
{
	if (!object)
	{
		return;
	}
	visibleRows.push_back(GameObjectSelection(documentId, object->Id()));
	const auto children = object->Children();
	if (children.empty())
	{
		return;
	}
	const String key = object->Id().ToString();
	ImGui::PushID(key.c_str());
	const bool open = ImGui::TreeNodeGetOpen(ImGui::GetID(treeNodeId));
	ImGui::PopID();
	if (open)
	{
		for (const auto& child : children)
		{
			AppendVisibleRows(child, documentId, visibleRows, treeNodeId);
		}
	}
}

void SetLegacySelected(EditorSelectionContext* selection, ResourceId& selected)
{
	if (!selection)
	{
		return;
	}
	const auto* primary = selection->PrimaryEntry();
	selected = primary && primary->kind == EditorSelectionKind::GameObject ? primary->objectId : ResourceId{};
}
} // namespace

HierarchyDropZone ClassifyHierarchyDropZone(float rowY, float rowTop, float rowBottom)
{
	if (rowBottom <= rowTop)
	{
		return HierarchyDropZone::Into;
	}
	const float fraction = std::clamp((rowY - rowTop) / (rowBottom - rowTop), 0.0f, 1.0f);
	if (fraction < 0.25f)
	{
		return HierarchyDropZone::Above;
	}
	if (fraction > 0.75f)
	{
		return HierarchyDropZone::Below;
	}
	return HierarchyDropZone::Into;
}

Result<Vector<EditorSelectionEntry>>
NormalizeHierarchyDragSources(const EditorDragSession& session, ResourceId documentId, bool playSession,
                              const std::function<ResourceId(ResourceId)>& parentOf)
{
	if (!documentId || session.sources.empty() || !parentOf)
	{
		return Result<Vector<EditorSelectionEntry>>::Failure("Hierarchy drag source is empty or has no document");
	}
	Vector<EditorSelectionEntry> sources;
	for (const auto& source : session.sources)
	{
		const auto& entry = source.identity;
		if (source.componentId || entry.kind != EditorSelectionKind::GameObject || entry.IsMissing() ||
		    entry.documentId != documentId || entry.playSession != playSession || !entry.objectId)
		{
			return Result<Vector<EditorSelectionEntry>>::Failure(
			    "Hierarchy drag sources must be GameObjects from this document");
		}
		if (std::none_of(sources.begin(), sources.end(), [&](const auto& item) { return item.SameIdentity(entry); }))
		{
			sources.push_back(entry);
		}
	}
	Vector<EditorSelectionEntry> normalized;
	for (const auto& candidate : sources)
	{
		bool hasSelectedAncestor = false;
		ResourceId ancestor = parentOf(candidate.objectId);
		Size depth = 0;
		while (ancestor && depth++ <= sources.size() + 1024)
		{
			if (std::any_of(sources.begin(), sources.end(),
			                [&](const auto& source) { return source.objectId == ancestor; }))
			{
				hasSelectedAncestor = true;
				break;
			}
			const ResourceId parent = parentOf(ancestor);
			if (parent == ancestor)
			{
				return Result<Vector<EditorSelectionEntry>>::Failure("Hierarchy parent chain contains a cycle");
			}
			ancestor = parent;
		}
		if (depth > sources.size() + 1024)
		{
			return Result<Vector<EditorSelectionEntry>>::Failure("Hierarchy parent chain is invalid");
		}
		if (!hasSelectedAncestor)
		{
			normalized.push_back(candidate);
		}
	}
	return Result<Vector<EditorSelectionEntry>>::Success(std::move(normalized));
}

namespace
{
float VectorLength3(float x, float y, float z)
{
	return std::sqrt(x * x + y * y + z * z);
}

Quat HierarchyRotationFromMatrix(const Mat4& matrix)
{
	const float trace = matrix.At(0, 0) + matrix.At(1, 1) + matrix.At(2, 2);
	if (trace > 0.0f)
	{
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		return {(matrix.At(2, 1) - matrix.At(1, 2)) / s, (matrix.At(0, 2) - matrix.At(2, 0)) / s,
		        (matrix.At(1, 0) - matrix.At(0, 1)) / s, s / 4.0f};
	}
	if (matrix.At(0, 0) > matrix.At(1, 1) && matrix.At(0, 0) > matrix.At(2, 2))
	{
		const float s = std::sqrt(1.0f + matrix.At(0, 0) - matrix.At(1, 1) - matrix.At(2, 2)) * 2.0f;
		return {s / 4.0f, (matrix.At(0, 1) + matrix.At(1, 0)) / s, (matrix.At(0, 2) + matrix.At(2, 0)) / s,
		        (matrix.At(2, 1) - matrix.At(1, 2)) / s};
	}
	if (matrix.At(1, 1) > matrix.At(2, 2))
	{
		const float s = std::sqrt(1.0f + matrix.At(1, 1) - matrix.At(0, 0) - matrix.At(2, 2)) * 2.0f;
		return {(matrix.At(0, 1) + matrix.At(1, 0)) / s, s / 4.0f, (matrix.At(1, 2) + matrix.At(2, 1)) / s,
		        (matrix.At(0, 2) - matrix.At(2, 0)) / s};
	}
	const float s = std::sqrt(1.0f + matrix.At(2, 2) - matrix.At(0, 0) - matrix.At(1, 1)) * 2.0f;
	return {(matrix.At(0, 2) + matrix.At(2, 0)) / s, (matrix.At(1, 2) + matrix.At(2, 1)) / s, s / 4.0f,
	        (matrix.At(1, 0) - matrix.At(0, 1)) / s};
}

Result<LocalTransform> DecomposeHierarchyLocal(const Mat4& matrix)
{
	for (const float value : matrix.values)
	{
		if (!std::isfinite(value))
		{
			return Result<LocalTransform>::Failure("The destination transform is singular or non-finite");
		}
	}
	if (std::abs(matrix.At(3, 0)) > 0.0001f || std::abs(matrix.At(3, 1)) > 0.0001f ||
	    std::abs(matrix.At(3, 2)) > 0.0001f || std::abs(matrix.At(3, 3) - 1.0f) > 0.0001f)
	{
		return Result<LocalTransform>::Failure("The destination transform is not affine");
	}
	LocalTransform local;
	local.position = {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
	const float scales[3] = {VectorLength3(matrix.At(0, 0), matrix.At(1, 0), matrix.At(2, 0)),
	                         VectorLength3(matrix.At(0, 1), matrix.At(1, 1), matrix.At(2, 1)),
	                         VectorLength3(matrix.At(0, 2), matrix.At(1, 2), matrix.At(2, 2))};
	for (const float scale : scales)
	{
		if (!std::isfinite(scale) || scale <= 0.00001f)
		{
			return Result<LocalTransform>::Failure("The destination transform has a singular scale");
		}
	}
	local.scale = {scales[0], scales[1], scales[2]};
	Mat4 rotation = Mat4::Identity();
	for (int column = 0; column < 3; ++column)
	{
		for (int row = 0; row < 3; ++row)
		{
			rotation.At(row, column) = matrix.At(row, column) / scales[column];
		}
	}
	for (int a = 0; a < 3; ++a)
	{
		for (int b = a + 1; b < 3; ++b)
		{
			float dot = 0.0f;
			for (int row = 0; row < 3; ++row)
			{
				dot += rotation.At(row, a) * rotation.At(row, b);
			}
			if (std::abs(dot) > 0.001f)
			{
				return Result<LocalTransform>::Failure("The destination transform would introduce shear");
			}
		}
	}
	const float determinant =
	    rotation.At(0, 0) * (rotation.At(1, 1) * rotation.At(2, 2) - rotation.At(1, 2) * rotation.At(2, 1)) -
	    rotation.At(0, 1) * (rotation.At(1, 0) * rotation.At(2, 2) - rotation.At(1, 2) * rotation.At(2, 0)) +
	    rotation.At(0, 2) * (rotation.At(1, 0) * rotation.At(2, 1) - rotation.At(1, 1) * rotation.At(2, 0));
	if (determinant < 0.999f || determinant > 1.001f)
	{
		return Result<LocalTransform>::Failure("The destination transform contains a reflection");
	}
	local.rotation = HierarchyRotationFromMatrix(rotation);
	const Mat4 rebuilt = local.Matrix();
	for (Size index = 0; index < matrix.values.size(); ++index)
	{
		if (std::abs(rebuilt.values[index] - matrix.values[index]) >
		    0.001f * std::max(1.0f, std::abs(matrix.values[index])))
		{
			return Result<LocalTransform>::Failure("The destination transform cannot be represented as local TRS");
		}
	}
	return Result<LocalTransform>::Success(local);
}

std::optional<HierarchyDropZone>
DrawHierarchyRowDropFeedback(const ImRect& rect, ResourceId documentId, bool playSession,
                             const std::function<ResourceId(ResourceId)>& parentOf, bool allowDrop,
                             const std::function<void(const EditorDragSession&, HierarchyDropZone)>& onDrop)
{
	if (!allowDrop || !ImGui::GetDragDropPayload())
	{
		return std::nullopt;
	}
	if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##hierarchyDropTarget")))
	{
		return std::nullopt;
	}
	std::optional<HierarchyDropZone> zone;
	if (const ImGuiPayload* payload =
	        ImGui::AcceptDragDropPayload(editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
	                                                                       ImGuiDragDropFlags_AcceptBeforeDelivery))
	{
		auto session = DecodeEditorDragPayload(payload->DataType, payload->Data, static_cast<Size>(payload->DataSize));
		if (session && NormalizeHierarchyDragSources(session.value, documentId, playSession, parentOf))
		{
			zone = ClassifyHierarchyDropZone(ImGui::GetIO().MousePos.y, rect.Min.y, rect.Max.y);
			if (onDrop && ClaimEditorDragDropDelivery(payload))
			{
				onDrop(session.value, *zone);
			}
			if (*zone == HierarchyDropZone::Into)
			{
				ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max,
				                                          ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.22f), 2.0f);
			}
			else
			{
				const float y = *zone == HierarchyDropZone::Above ? rect.Min.y + 1.0f : rect.Max.y - 1.0f;
				ImGui::GetWindowDrawList()->AddLine({rect.Min.x, y}, {rect.Max.x, y},
				                                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
			}
		}
	}
	ImGui::EndDragDropTarget();
	return zone;
}

void DrawHierarchyRootDropFeedback(ResourceId documentId, bool playSession,
                                   const std::function<ResourceId(ResourceId)>& parentOf, bool allowDrop,
                                   const std::function<void(const EditorDragSession&, HierarchyDropZone)>& onDrop)
{
	if (!allowDrop || !ImGui::GetDragDropPayload())
	{
		return;
	}
	const ImVec2 min = ImGui::GetCursorScreenPos();
	const ImRect work = ImGui::GetCurrentWindow()->WorkRect;
	const ImRect rect(min, work.Max);
	if (rect.GetHeight() < 2.0f || !ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##hierarchyRootDropTarget")))
	{
		return;
	}
	if (const ImGuiPayload* payload =
	        ImGui::AcceptDragDropPayload(editorDragSessionPayloadType, ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
	                                                                       ImGuiDragDropFlags_AcceptBeforeDelivery))
	{
		auto session = DecodeEditorDragPayload(payload->DataType, payload->Data, static_cast<Size>(payload->DataSize));
		if (session && NormalizeHierarchyDragSources(session.value, documentId, playSession, parentOf))
		{
			if (onDrop && ClaimEditorDragDropDelivery(payload))
			{
				onDrop(session.value, HierarchyDropZone::Into);
			}
			ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max,
			                                          ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.35f));
			ImGui::GetWindowDrawList()->AddText({rect.Min.x + 8.0f, rect.Min.y + 5.0f},
			                                    ImGui::GetColorU32(ImGuiCol_Text), "Move to Scene Root");
		}
	}
	ImGui::EndDragDropTarget();
}
} // namespace

Result<void> ApplyHierarchyDrop(Scene& scene, const EditorDragSession& session, ResourceId targetId,
                                HierarchyDropZone zone, ResourceId documentId)
{
	auto parentIdOf = [&](ResourceId objectId)
	{
		auto object = scene.GameObjectById(objectId);
		if (!object)
		{
			return ResourceId{};
		}
		auto parent = object.value->Parent();
		if (!parent)
		{
			return ResourceId{};
		}
		auto id = scene.PersistentId(parent);
		return id ? id.value : ResourceId{};
	};
	auto normalized = NormalizeHierarchyDragSources(session, documentId, false, parentIdOf);
	if (!normalized)
	{
		return Result<void>::Failure(normalized.error);
	}
	Vector<GameObject::Ptr> sources;
	for (const auto& entry : normalized.value)
	{
		auto source = scene.GameObjectById(entry.objectId);
		if (!source)
		{
			return Result<void>::Failure("A hierarchy drag source no longer exists");
		}
		sources.push_back(source.value);
	}
	GameObject::Ptr target;
	if (targetId)
	{
		auto found = scene.GameObjectById(targetId);
		if (!found)
		{
			return Result<void>::Failure("The hierarchy drop target no longer exists");
		}
		target = found.value;
	}
	GameObject::Ptr destinationParent;
	Ursa::Size insertionIndex = 0;
	Vector<GameObject::Ptr> destinationSiblings;
	if (zone == HierarchyDropZone::Into)
	{
		destinationParent = target;
		destinationSiblings = target ? scene.ChildrenOf(target).value : scene.RootGameObjects();
		insertionIndex = destinationSiblings.size();
	}
	else
	{
		if (!target)
		{
			return Result<void>::Failure("An above/below drop needs a hierarchy row target");
		}
		auto parent = scene.ParentOf(target);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		destinationParent = parent.value;
		destinationSiblings = destinationParent ? scene.ChildrenOf(destinationParent).value : scene.RootGameObjects();
		const auto targetPosition = std::find(destinationSiblings.begin(), destinationSiblings.end(), target);
		if (targetPosition == destinationSiblings.end())
		{
			return Result<void>::Failure("The hierarchy target is missing from its sibling list");
		}
		insertionIndex = static_cast<Ursa::Size>(std::distance(destinationSiblings.begin(), targetPosition));
		if (zone == HierarchyDropZone::Below)
		{
			++insertionIndex;
		}
	}
	for (auto ancestor = destinationParent; ancestor; ancestor = ancestor->Parent())
	{
		if (std::find(sources.begin(), sources.end(), ancestor) != sources.end())
		{
			return Result<void>::Failure("Cannot move a GameObject into itself or one of its descendants");
		}
	}

	struct OriginalState
	{
		GameObject::Ptr object;
		GameObject::Ptr parent;
		LocalTransform local;
		Mat4 world;
		Ursa::Size index = 0;
	};

	Vector<OriginalState> original;
	Vector<GameObject::Ptr> originalParents;
	for (const auto& source : sources)
	{
		auto parent = scene.ParentOf(source);
		auto local = scene.GetLocalTransform(source);
		auto world = scene.WorldTransform(source);
		if (!parent || !local || !world)
		{
			return Result<void>::Failure(!parent ? parent.error : !local ? local.error : world.error);
		}
		Vector<GameObject::Ptr> siblings =
		    parent.value ? scene.ChildrenOf(parent.value).value : scene.RootGameObjects();
		const auto position = std::find(siblings.begin(), siblings.end(), source);
		if (position == siblings.end())
		{
			return Result<void>::Failure("A hierarchy drag source is missing from its sibling list");
		}
		original.push_back({source, parent.value, local.value, world.value,
		                    static_cast<Ursa::Size>(std::distance(siblings.begin(), position))});
		if (std::find(originalParents.begin(), originalParents.end(), parent.value) == originalParents.end())
		{
			originalParents.push_back(parent.value);
		}
	}
	if (std::find(originalParents.begin(), originalParents.end(), destinationParent) == originalParents.end())
	{
		originalParents.push_back(destinationParent);
	}

	struct SiblingSnapshot
	{
		GameObject::Ptr parent;
		Vector<GameObject::Ptr> siblings;
	};

	Vector<SiblingSnapshot> siblingSnapshots;
	for (const auto& parent : originalParents)
	{
		siblingSnapshots.push_back({parent, parent ? scene.ChildrenOf(parent).value : scene.RootGameObjects()});
	}
	Ursa::Size removedBeforeInsertion = 0;
	for (Ursa::Size index = 0; index < destinationSiblings.size() && index < insertionIndex; ++index)
	{
		if (std::find(sources.begin(), sources.end(), destinationSiblings[index]) != sources.end())
		{
			++removedBeforeInsertion;
		}
	}
	Vector<GameObject::Ptr> desiredSiblings;
	for (const auto& sibling : destinationSiblings)
	{
		if (std::find(sources.begin(), sources.end(), sibling) == sources.end())
		{
			desiredSiblings.push_back(sibling);
		}
	}
	insertionIndex -= std::min(insertionIndex, removedBeforeInsertion);
	insertionIndex = std::min(insertionIndex, desiredSiblings.size());
	desiredSiblings.insert(desiredSiblings.begin() + static_cast<Ursa::PtrDiff>(insertionIndex), sources.begin(),
	                       sources.end());
	Mat4 destinationWorld = Mat4::Identity();
	if (destinationParent)
	{
		auto world = scene.WorldTransform(destinationParent);
		if (!world)
		{
			return Result<void>::Failure(world.error);
		}
		destinationWorld = world.value;
	}
	Vector<LocalTransform> destinationLocals;
	destinationLocals.reserve(original.size());
	for (const auto& state : original)
	{
		Mat4 localMatrix = state.world;
		if (destinationParent)
		{
			try
			{
				localMatrix = destinationWorld.InverseAffine() * state.world;
			}
			catch (const std::exception& error)
			{
				return Result<void>::Failure(String("The destination transform is singular: ") + error.what());
			}
		}
		auto local = DecomposeHierarchyLocal(localMatrix);
		if (!local)
		{
			return Result<void>::Failure(local.error);
		}
		destinationLocals.push_back(local.value);
	}
	const auto rollback = [&]()
	{
		for (const auto& state : original)
		{
			scene.SetParent(state.object, state.parent);
			scene.SetLocalTransform(state.object, state.local);
		}
		for (const auto& snapshot : siblingSnapshots)
		{
			for (Ursa::Size index = 0; index < snapshot.siblings.size(); ++index)
			{
				scene.ReorderGameObject(snapshot.siblings[index], index);
			}
		}
	};
	for (Ursa::Size index = 0; index < original.size(); ++index)
	{
		auto reparented = scene.SetParent(original[index].object, destinationParent);
		if (!reparented)
		{
			rollback();
			return Result<void>::Failure(reparented.error);
		}
		auto transformed = scene.SetLocalTransform(original[index].object, destinationLocals[index]);
		if (!transformed)
		{
			rollback();
			return Result<void>::Failure(transformed.error);
		}
	}
	for (Ursa::Size index = 0; index < desiredSiblings.size(); ++index)
	{
		auto reordered = scene.ReorderGameObject(desiredSiblings[index], index);
		if (!reordered)
		{
			rollback();
			return Result<void>::Failure(reordered.error);
		}
	}
	return {};
}

Result<void> EditorHierarchy::Select(const Scene& scene, GameObject::Ptr node)
{
	auto id = scene.PersistentId(node);
	if (!id)
	{
		return Result<void>::Failure(id.error);
	}
	selected = id.value;
	return {};
}

Result<void> EditorHierarchy::Select(const PrefabAsset& asset, GameObject::Ptr node)
{
	if (!node || !asset.Root())
	{
		return Result<void>::Failure("Select a valid GameObject in the Prefab hierarchy");
	}
	std::function<bool(const GameObject::Ptr&)> contains = [&](const GameObject::Ptr& candidate)
	{
		if (candidate == node)
		{
			return true;
		}
		for (const auto& child : candidate->Children())
		{
			if (contains(child))
			{
				return true;
			}
		}
		return false;
	};
	if (!contains(asset.Root()))
	{
		return Result<void>::Failure("GameObject does not belong to the Prefab hierarchy");
	}
	selected = node->Id();
	return {};
}

void EditorHierarchy::ClearSelection()
{
	selected = {};
	pendingSelectionClick.Cancel();
}

Result<GameObject::Ptr> EditorHierarchy::SelectedGameObject(const Scene& scene) const
{
	if (!selected)
	{
		return Result<GameObject::Ptr>::Failure("No node selected");
	}
	return scene.GameObjectById(selected);
}

Result<GameObject::Ptr> EditorHierarchy::SelectedGameObject(const PrefabAsset& asset) const
{
	if (!selected || !asset.Root())
	{
		return Result<GameObject::Ptr>::Failure("No Prefab GameObject selected");
	}
	std::function<GameObject::Ptr(const GameObject::Ptr&)> find = [&](const GameObject::Ptr& object)
	{
		if (object->Id() == selected)
		{
			return object;
		}
		for (const auto& child : object->Children())
		{
			auto result = find(child);
			if (result)
			{
				return result;
			}
		}
		return GameObject::Ptr{};
	};
	auto object = find(asset.Root());
	return object ? Result<GameObject::Ptr>::Success(std::move(object))
	              : Result<GameObject::Ptr>::Failure("Selected Prefab GameObject no longer exists");
}

Result<bool> EditorHierarchy::Draw(PrefabAsset& asset, bool* open, EditorSelectionContext* selection,
                                   ResourceId documentId, UInt64 documentRevision)
{
	(void)documentRevision;
	if (open && !*open)
	{
		return Result<bool>::Success(false);
	}
	if (!asset.Root())
	{
		return Result<bool>::Failure("PrefabAsset has no root GameObject");
	}
	ImGui::Begin("Hierarchy", open);
	if (!ImGui::GetDragDropPayload())
	{
		dragHoverTarget = {};
	}
	if (selection && selection->MissingCount())
	{
		ImGui::TextDisabled("%zu selected object(s) no longer exist", selection->MissingCount());
	}
	pendingClickHovered = false;
	pointerOverHierarchyRow = false;
	Vector<EditorSelectionEntry> visibleRows;
	AppendVisibleRows(asset.Root(), documentId, visibleRows, "##gameObject");
	std::function<void(const GameObject::Ptr&)> draw = [&](const GameObject::Ptr& object)
	{
		const ResourceId id = object->Id();
		const String key = id.ToString();
		const auto name = object->Name();
		const auto children = object->Children();
		ImGui::PushID(key.c_str());
		const String label = name && !name.value.empty() ? name.value : String("GameObject ") + key.substr(0, 8);
		const bool hasContent = !children.empty();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasContent)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		const auto entry = GameObjectSelection(documentId, id);
		if (selection ? selection->Contains(entry) : selected == id)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		const auto active = object->ActiveInHierarchy();
		const bool autoExpand = hasContent && dragHoverTarget == id && ImGui::GetTime() - dragHoverStartedAt >= 0.6;
		if (autoExpand)
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}
		if (active && !active.value)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
		}
		const bool open =
		    Icons::TreeNode("##gameObject", flags, Icons::ForObject(object, object == asset.Root()), label.c_str());
		const ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		pointerOverHierarchyRow |= ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, false);
		if (active && !active.value)
		{
			ImGui::PopStyleVar();
		}
		if (ImGui::IsItemClicked())
		{
			if (selection)
			{
				pendingSelectionClick.Begin(entry, visibleRows, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift,
				                            selection->revision);
			}
			else
			{
				selected = id;
			}
		}
		if (ImGui::BeginDragDropSource())
		{
			const auto drag =
			    selection ? BeginEditorDragSession(*selection, entry) : BeginEditorDragSession({entry}, 0);
			if (drag)
			{
				ImGui::SetDragDropPayload(editorDragSessionPayloadType, &drag.value, sizeof(drag.value));
			}
			ImGui::TextUnformatted(label.c_str());
			ImGui::EndDragDropSource();
		}
		auto parentOf = [&](ResourceId objectId)
		{
			std::function<GameObject::Ptr(const GameObject::Ptr&)> find = [&](const GameObject::Ptr& current)
			{
				if (!current)
				{
					return GameObject::Ptr{};
				}
				if (current->Id() == objectId)
				{
					return current;
				}
				for (const auto& child : current->Children())
				{
					auto found = find(child);
					if (found)
					{
						return found;
					}
				}
				return GameObject::Ptr{};
			};
			auto found = find(asset.Root());
			auto parent = found ? found->Parent() : GameObject::Ptr{};
			return parent ? parent->Id() : ResourceId{};
		};
		const auto dropZone = DrawHierarchyRowDropFeedback(rowRect, documentId, false, parentOf, false, {});
		if (dropZone == HierarchyDropZone::Into && hasContent && ImGui::GetDragDropPayload())
		{
			if (dragHoverTarget != id)
			{
				dragHoverTarget = id;
				dragHoverStartedAt = ImGui::GetTime();
			}
		}
		else if (dragHoverTarget == id)
		{
			dragHoverTarget = {};
		}
		if (selection && pendingSelectionClick.Matches(entry) && ImGui::IsItemHovered())
		{
			pendingClickHovered = true;
		}
		if (open && hasContent)
		{
			for (const auto& child : children)
			{
				draw(child);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	};
	draw(asset.Root());
	if (!pointerOverHierarchyRow)
	{
		Icons::DrawPanelContextMenu("PrefabHierarchyPanelActions", open);
	}
	ImGui::End();
	if (selection && pendingSelectionClick.active)
	{
		pendingSelectionClick.ObserveDrag(ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f));
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			pendingSelectionClick.Finish(*selection, pendingClickHovered);
			SetLegacySelected(selection, selected);
		}
		else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			pendingSelectionClick.Cancel();
		}
	}
	if (selection)
	{
		auto reconciled = selection->entries;
		Vector<ResourceId> objectIds;
		std::function<void(const GameObject::Ptr&)> collect = [&](const GameObject::Ptr& object)
		{
			if (!object)
			{
				return;
			}
			objectIds.push_back(object->Id());
			for (const auto& child : object->Children())
			{
				collect(child);
			}
		};
		collect(asset.Root());
		for (auto& entry : reconciled)
		{
			if (entry.kind == EditorSelectionKind::GameObject && entry.documentId == documentId && !entry.playSession)
			{
				entry.missing = std::find(objectIds.begin(), objectIds.end(), entry.objectId) == objectIds.end();
			}
		}
		selection->ReconcileEntries(std::move(reconciled));
		SetLegacySelected(selection, selected);
	}
	if (selected && !SelectedGameObject(asset))
	{
		ClearSelection();
	}
	return Result<bool>::Success(false);
}

void EditorHierarchy::DrawEmpty(bool* open)
{
	if (open && !*open)
	{
		return;
	}
	if (ImGui::Begin("Hierarchy", open))
	{
		Icons::DrawPanelContextMenu("EmptyHierarchyPanelActions", open);
		ImGui::TextDisabled("Open a scene or Prefab from Project.");
	}
	ImGui::End();
}

Result<bool> EditorHierarchy::DrawGameObject(Scene& scene, GameObject::Ptr node,
                                             const Vector<EditorSelectionEntry>& visibleRows,
                                             EditorSelectionContext* selection, ResourceId documentId, bool playSession,
                                             UInt64 documentRevision, bool editableScene)
{
	auto id = scene.PersistentId(node);
	auto name = scene.GameObjectName(node);
	auto enabled = scene.ActiveSelf(node);
	auto children = scene.ChildrenOf(node);
	if (!id || !name || !enabled || !children)
	{
		return Result<bool>::Failure("Cannot inspect hierarchy node");
	}
	const auto key = id.value.ToString();
	ImGui::PushID(key.c_str());
	String display = name.value.empty() ? String("Node ") + key.substr(0, 8) : name.value;
	const bool hasChildren = !children.value.empty();
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	const auto entry = GameObjectSelection(documentId, id.value, playSession);
	if (selection ? selection->Contains(entry) : selected == id.value)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	const auto active = scene.ActiveInHierarchy(node);
	const bool autoExpand = hasChildren && dragHoverTarget == id.value && ImGui::GetTime() - dragHoverStartedAt >= 0.6;
	if (autoExpand)
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	if (active && !active.value)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
	}
	const bool open = Icons::TreeNode(
	    "##node", flags, Icons::ForObject(node, prefabObjects.find(key) != prefabObjects.end()), display.c_str());
	const ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	pointerOverHierarchyRow |= ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, false);
	const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	const bool rowHovered = ImGui::IsItemHovered();
	if (active && !active.value)
	{
		ImGui::PopStyleVar();
	}
	if (ImGui::IsItemClicked())
	{
		if (selection)
		{
			pendingSelectionClick.Begin(entry, visibleRows, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift,
			                            selection->revision);
		}
		else
		{
			selected = id.value;
		}
	}
	if (ImGui::BeginDragDropSource())
	{
		const auto drag = selection ? BeginEditorDragSession(*selection, entry) : BeginEditorDragSession({entry}, 0);
		if (drag)
		{
			ImGui::SetDragDropPayload(editorDragSessionPayloadType, &drag.value, sizeof(drag.value));
		}
		ImGui::TextUnformatted(display.c_str());
		ImGui::EndDragDropSource();
	}
	auto parentOf = [&](ResourceId objectId)
	{
		auto objectResult = scene.GameObjectById(objectId);
		if (!objectResult)
		{
			return ResourceId{};
		}
		auto parent = objectResult.value->Parent();
		if (!parent)
		{
			return ResourceId{};
		}
		auto parentId = scene.PersistentId(parent);
		return parentId ? parentId.value : ResourceId{};
	};
	bool changed = false;
	const auto targetPrefabInstance = prefabInstanceByObject.find(key);
	const bool allowHierarchyDrop =
	    !playSession && (editableScene || targetPrefabInstance != prefabInstanceByObject.end());
	const auto dropZone = DrawHierarchyRowDropFeedback(
	    rowRect, documentId, playSession, parentOf, allowHierarchyDrop,
	    [this, &scene, documentId, targetId = id.value, &changed, editableScene,
	     targetInstance = targetPrefabInstance == prefabInstanceByObject.end()
	                          ? String{}
	                          : targetPrefabInstance->second](const EditorDragSession& drag, HierarchyDropZone zone)
	    {
		    if (!editableScene)
		    {
			    if (targetInstance.empty() ||
			        std::any_of(drag.sources.begin(), drag.sources.end(),
			                    [&](const EditorDragSource& source)
			                    {
				                    const auto owner = prefabInstanceByObject.find(source.identity.objectId.ToString());
				                    return owner == prefabInstanceByObject.end() || owner->second != targetInstance;
			                    }))
			    {
				    commandDiagnostic = "Prefab hierarchy moves must stay within one Prefab instance";
				    return;
			    }
		    }
		    const bool containsAsset =
		        std::any_of(drag.sources.begin(), drag.sources.end(), [](const EditorDragSource& source)
		                    { return source.identity.kind == EditorSelectionKind::Asset; });
		    auto result = containsAsset && assetDropAction
		                      ? assetDropAction(scene, drag, targetId, zone, documentId)
		                      : ApplyHierarchyDrop(scene, drag, targetId, zone, documentId);
		    changed |= static_cast<bool>(result);
		    commandDiagnostic = result ? String{} : result.error;
	    });
	if (dropZone == HierarchyDropZone::Into && hasChildren && ImGui::GetDragDropPayload())
	{
		if (dragHoverTarget != id.value)
		{
			dragHoverTarget = id.value;
			dragHoverStartedAt = ImGui::GetTime();
		}
	}
	else if (dragHoverTarget == id.value)
	{
		dragHoverTarget = {};
	}
	if (rightClicked)
	{
		bool contextSelectionAccepted = true;
		if (selection && !selection->Contains(entry))
		{
			contextSelectionAccepted = selection->SelectEntries({entry}, 0, entry);
			if (contextSelectionAccepted)
			{
				SetLegacySelected(selection, selected);
			}
		}
		if (contextSelectionAccepted && selection)
		{
			contextMenuTarget = EditorCommandContext::Capture(*selection, documentId, documentRevision);
			ImGui::OpenPopup("GameObjectActions");
		}
		else if (!selection)
		{
			ImGui::OpenPopup("GameObjectActions");
		}
	}
	if (ImGui::BeginPopup("GameObjectActions"))
	{
		EditorCommands commands;
		commands.Register({"hierarchy.create-empty-child",
		                   "Create Empty",
		                   {},
		                   [documentId, editableScene](const EditorCommandContext& context)
		                   {
			                   const auto* primary = context.selection.PrimaryEntry();
			                   return editableScene && primary && primary->kind == EditorSelectionKind::GameObject &&
			                                  primary->documentId == documentId && !primary->playSession &&
			                                  !primary->IsMissing()
			                              ? Result<void>{}
			                              : Result<void>::Failure("Select a valid object in this editable scene");
		                   },
		                   [&scene, selection, documentId, this](const EditorCommandContext& context)
		                   {
			                   const auto* primary = context.selection.PrimaryEntry();
			                   if (!primary)
			                   {
				                   return Result<void>::Failure("The hierarchy selection is empty");
			                   }
			                   auto parent = scene.GameObjectById(primary->objectId);
			                   if (!parent)
			                   {
				                   return Result<void>::Failure("The selected hierarchy object no longer exists");
			                   }
			                   auto created = scene.CreateGameObject("GameObject", parent.value);
			                   if (!created)
			                   {
				                   return Result<void>::Failure(created.error);
			                   }
			                   auto identity = scene.PersistentId(created.value);
			                   if (!identity)
			                   {
				                   scene.DestroyGameObject(created.value);
				                   return Result<void>::Failure(identity.error);
			                   }
			                   if (selection)
			                   {
				                   selection->SelectGameObject(documentId, identity.value);
				                   SetLegacySelected(selection, selected);
			                   }
			                   else
			                   {
				                   selected = identity.value;
			                   }
			                   return Result<void>{};
		                   }});
		commands.Register(
		    {"hierarchy.duplicate", "Duplicate", "Ctrl+D",
		     [editableScene, documentId, duplicateObjects = duplicateObjectsAction](const EditorCommandContext& context)
		     {
			     const bool valid = std::all_of(context.selection.entries.begin(), context.selection.entries.end(),
			                                    [&](const EditorSelectionEntry& item)
			                                    {
				                                    return item.kind == EditorSelectionKind::GameObject &&
				                                           item.documentId == documentId && !item.playSession &&
				                                           !item.IsMissing();
			                                    });
			     return editableScene && duplicateObjects && valid && !context.selection.entries.empty()
			                ? Result<void>{}
			                : Result<void>::Failure("Select editable GameObjects to duplicate");
		     },
		     [&scene, duplicateObjects = duplicateObjectsAction](const EditorCommandContext& context)
		     {
			     Vector<ResourceId> ids;
			     ids.reserve(context.selection.entries.size());
			     for (const auto& item : context.selection.entries)
			     {
				     ids.push_back(item.objectId);
			     }
			     const auto* primary = context.selection.PrimaryEntry();
			     return duplicateObjects(scene, ids, primary ? primary->objectId : ResourceId{});
		     }});
		commands.Register({"hierarchy.rename", "Rename", "F2",
		                   [editableScene, documentId, targetId = entry.objectId](const EditorCommandContext& context)
		                   {
			                   const auto* primary = context.selection.PrimaryEntry();
			                   return editableScene && context.selection.entries.size() == 1 && primary &&
			                                  primary->kind == EditorSelectionKind::GameObject &&
			                                  primary->documentId == documentId && primary->objectId == targetId &&
			                                  !primary->playSession && !primary->IsMissing()
			                              ? Result<void>{}
			                              : Result<void>::Failure("Rename requires one editable GameObject");
		                   },
		                   [this, &scene, targetId = entry.objectId](const EditorCommandContext&)
		                   {
			                   auto object = scene.GameObjectById(targetId);
			                   if (!object)
			                   {
				                   return Result<void>::Failure(object.error);
			                   }
			                   auto name = object.value->Name();
			                   if (!name)
			                   {
				                   return Result<void>::Failure(name.error);
			                   }
			                   renameTarget = targetId;
			                   std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
			                   std::copy_n(name.value.data(), std::min(name.value.size(), renameBuffer.size() - 1),
			                               renameBuffer.data());
			                   ImGui::OpenPopup("Rename GameObject");
			                   return Result<void>{};
		                   }});
		commands.Register(
		    {"hierarchy.delete", "Delete", "Delete",
		     [documentId, playSession, deleteObjects = deleteObjectsAction](const EditorCommandContext& context)
		     {
			     const bool valid = std::all_of(context.selection.entries.begin(), context.selection.entries.end(),
			                                    [&](const EditorSelectionEntry& item)
			                                    {
				                                    return item.kind == EditorSelectionKind::GameObject &&
				                                           item.documentId == documentId &&
				                                           item.playSession == playSession && !item.IsMissing();
			                                    });
			     return !playSession && deleteObjects && valid && !context.selection.entries.empty()
			                ? Result<void>{}
			                : Result<void>::Failure("Select editable GameObjects to delete");
		     },
		     [this](const EditorCommandContext& context)
		     {
			     pendingDeleteIds.clear();
			     pendingDeleteIds.reserve(context.selection.entries.size());
			     for (const auto& item : context.selection.entries)
			     {
				     pendingDeleteIds.push_back(item.objectId);
			     }
			     const auto* primary = context.selection.PrimaryEntry();
			     pendingDeletePrimary = primary ? primary->objectId : ResourceId{};
			     return Result<void>{};
		     }});
		const EditorCommandContext current =
		    selection ? EditorCommandContext::Capture(*selection, documentId, documentRevision)
		              : EditorCommandContext{};
		const auto canCreate = contextMenuTarget
		                           ? commands.CanExecute("hierarchy.create-empty-child", *contextMenuTarget, current)
		                           : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Create Empty", nullptr, false, static_cast<bool>(canCreate)))
		{
			auto executed = commands.Execute("hierarchy.create-empty-child", *contextMenuTarget, current);
			changed = static_cast<bool>(executed);
		}
		const auto canDuplicate = contextMenuTarget
		                              ? commands.CanExecute("hierarchy.duplicate", *contextMenuTarget, current)
		                              : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(canDuplicate)))
		{
			auto duplicated = commands.Execute("hierarchy.duplicate", *contextMenuTarget, current);
			changed = static_cast<bool>(duplicated);
		}
		const auto canRename = contextMenuTarget ? commands.CanExecute("hierarchy.rename", *contextMenuTarget, current)
		                                         : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Rename", "F2", false, static_cast<bool>(canRename)))
		{
			commands.Execute("hierarchy.rename", *contextMenuTarget, current);
		}
		const auto canDelete = contextMenuTarget ? commands.CanExecute("hierarchy.delete", *contextMenuTarget, current)
		                                         : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Delete", "Delete", false, static_cast<bool>(canDelete)))
		{
			commands.Execute("hierarchy.delete", *contextMenuTarget, current);
		}
		ImGui::EndPopup();
	}
	if (selection && pendingSelectionClick.Matches(entry) && rowHovered)
	{
		pendingClickHovered = true;
	}
	if (open && hasChildren)
	{
		for (GameObject::Ptr child : children.value)
		{
			auto drawn = DrawGameObject(scene, child, visibleRows, selection, documentId, playSession, documentRevision,
			                            editableScene);
			if (!drawn)
			{
				ImGui::TreePop();
				ImGui::PopID();
				return drawn;
			}
			changed |= drawn.value;
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
	return Result<bool>::Success(changed);
}

Result<bool> EditorHierarchy::Draw(Scene& scene, bool* open, const LinkedSceneResource* linked,
                                   EditorSelectionContext* selection, ResourceId documentId, bool playSession,
                                   UInt64 documentRevision, DuplicateObjectsAction duplicateObjects,
                                   DeleteObjectsAction deleteObjects, AssetDropAction assetDrop)
{
	if (open && !*open)
	{
		return Result<bool>::Success(false);
	}
	duplicateObjectsAction = std::move(duplicateObjects);
	deleteObjectsAction = std::move(deleteObjects);
	assetDropAction = std::move(assetDrop);
	pendingDeleteIds.clear();
	pendingDeletePrimary = {};
	prefabObjects.clear();
	prefabInstanceByObject.clear();
	if (linked)
	{
		for (const auto& scope : linked->scopedIds)
		{
			for (const auto& entry : scope.second)
			{
				prefabObjects.emplace(entry.second, true);
				prefabInstanceByObject.emplace(entry.second, scope.first);
			}
		}
	}
	ImGui::Begin("Hierarchy", open);
	if (!ImGui::GetDragDropPayload())
	{
		dragHoverTarget = {};
	}
	if (selection && selection->MissingCount())
	{
		ImGui::TextDisabled("%zu selected object(s) no longer exist", selection->MissingCount());
	}
	pendingClickHovered = false;
	pointerOverHierarchyRow = false;
	Vector<EditorSelectionEntry> visibleRows;
	for (const auto& root : scene.RootGameObjects())
	{
		AppendVisibleRows(scene, root, documentId, visibleRows, "##node", playSession);
	}
	const bool editableScene = linked == nullptr && !playSession;
	bool changed = false;
	for (GameObject::Ptr root : scene.RootGameObjects())
	{
		auto drawn = DrawGameObject(scene, root, visibleRows, selection, documentId, playSession, documentRevision,
		                            editableScene);
		if (!drawn)
		{
			ImGui::End();
			return drawn;
		}
		changed |= drawn.value;
	}
	if (!pointerOverHierarchyRow)
	{
		Icons::DrawPanelContextMenu("SceneHierarchyPanelActions", open);
	}
	DrawHierarchyRootDropFeedback(
	    documentId, playSession,
	    [&](ResourceId objectId)
	    {
		    auto object = scene.GameObjectById(objectId);
		    if (!object)
		    {
			    return ResourceId{};
		    }
		    auto parent = object.value->Parent();
		    if (!parent)
		    {
			    return ResourceId{};
		    }
		    auto parentId = scene.PersistentId(parent);
		    return parentId ? parentId.value : ResourceId{};
	    },
	    editableScene,
	    [this, &scene, documentId, &changed](const EditorDragSession& drag, HierarchyDropZone zone)
	    {
		    const bool containsAsset =
		        std::any_of(drag.sources.begin(), drag.sources.end(), [](const EditorDragSource& source)
		                    { return source.identity.kind == EditorSelectionKind::Asset; });
		    auto result = containsAsset && assetDropAction ? assetDropAction(scene, drag, {}, zone, documentId)
		                                                   : ApplyHierarchyDrop(scene, drag, {}, zone, documentId);
		    changed |= static_cast<bool>(result);
		    commandDiagnostic = result ? String{} : result.error;
	    });
	if (!pendingDeleteIds.empty() && deleteObjectsAction)
	{
		auto deleted = deleteObjectsAction(scene, pendingDeleteIds, pendingDeletePrimary);
		changed |= static_cast<bool>(deleted);
		commandDiagnostic = deleted ? String{} : deleted.error;
		pendingDeleteIds.clear();
		pendingDeletePrimary = {};
	}
	if (!commandDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", commandDiagnostic.c_str());
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_F2) && selection && selection->entries.size() == 1 && editableScene)
	{
		const auto* primary = selection->PrimaryEntry();
		if (primary && primary->kind == EditorSelectionKind::GameObject && primary->documentId == documentId &&
		    !primary->playSession && !primary->IsMissing())
		{
			auto object = scene.GameObjectById(primary->objectId);
			if (object)
			{
				auto name = object.value->Name();
				if (name)
				{
					renameTarget = primary->objectId;
					std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
					std::copy_n(name.value.data(), std::min(name.value.size(), renameBuffer.size() - 1),
					            renameBuffer.data());
					ImGui::OpenPopup("Rename GameObject");
				}
			}
		}
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_Delete) && !playSession && deleteObjectsAction && selection &&
	    !selection->entries.empty())
	{
		Vector<ResourceId> ids;
		bool valid = true;
		for (const auto& item : selection->entries)
		{
			valid &= item.kind == EditorSelectionKind::GameObject && item.documentId == documentId &&
			         !item.playSession && !item.IsMissing();
			ids.push_back(item.objectId);
		}
		const auto* primary = selection->PrimaryEntry();
		if (valid)
		{
			auto deleted = deleteObjectsAction(scene, ids, primary ? primary->objectId : ResourceId{});
			changed |= static_cast<bool>(deleted);
		}
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && editableScene && duplicateObjectsAction &&
	    selection && !selection->entries.empty())
	{
		bool valid = true;
		Vector<ResourceId> ids;
		for (const auto& item : selection->entries)
		{
			valid &= item.kind == EditorSelectionKind::GameObject && item.documentId == documentId &&
			         !item.playSession && !item.IsMissing();
			ids.push_back(item.objectId);
		}
		const auto* primary = selection->PrimaryEntry();
		if (valid)
		{
			auto duplicated = duplicateObjectsAction(scene, ids, primary ? primary->objectId : ResourceId{});
			changed |= static_cast<bool>(duplicated);
		}
	}
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
	{
		EditorSelectionContext emptySelection;
		emptySelection.activeDocumentId = documentId;
		emptySelection.revision = selection ? selection->revision : 0;
		contextMenuTarget = EditorCommandContext::Capture(emptySelection, documentId, documentRevision);
	}
	if (ImGui::BeginPopupContextWindow("HierarchyBackground",
	                                   ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		EditorCommands commands;
		commands.Register(
		    {"hierarchy.create-empty-root",
		     "Create Empty",
		     {},
		     [editableScene](const EditorCommandContext&)
		     { return editableScene ? Result<void>{} : Result<void>::Failure("The current hierarchy is read-only"); },
		     [&scene, selection, documentId, this](const EditorCommandContext&)
		     {
			     auto created = scene.CreateGameObject("GameObject");
			     if (!created)
			     {
				     return Result<void>::Failure(created.error);
			     }
			     auto identity = scene.PersistentId(created.value);
			     if (!identity)
			     {
				     scene.DestroyGameObject(created.value);
				     return Result<void>::Failure(identity.error);
			     }
			     if (selection)
			     {
				     selection->SelectGameObject(documentId, identity.value);
				     SetLegacySelected(selection, selected);
			     }
			     else
			     {
				     selected = identity.value;
			     }
			     return Result<void>{};
		     }});
		EditorSelectionContext emptySelection;
		emptySelection.activeDocumentId = documentId;
		emptySelection.revision = selection ? selection->revision : 0;
		const EditorCommandContext current =
		    EditorCommandContext::Capture(emptySelection, documentId, documentRevision);
		const auto canCreate = contextMenuTarget
		                           ? commands.CanExecute("hierarchy.create-empty-root", *contextMenuTarget, current)
		                           : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Create Empty", nullptr, false, static_cast<bool>(canCreate)))
		{
			auto executed = commands.Execute("hierarchy.create-empty-root", *contextMenuTarget, current);
			changed = static_cast<bool>(executed);
		}
		ImGui::EndPopup();
	}
	if (renameTarget && ImGui::BeginPopupModal("Rename GameObject", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::SetNextItemWidth(260.0f);
		const bool submit =
		    ImGui::InputText("Name", renameBuffer.data(), renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		if (submit || ImGui::Button("Apply"))
		{
			auto target = scene.GameObjectById(renameTarget);
			if (target)
			{
				auto renamed = scene.SetGameObjectName(target.value, renameBuffer.data());
				changed |= static_cast<bool>(renamed);
			}
			renameTarget = {};
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			renameTarget = {};
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::End();
	if (selection && pendingSelectionClick.active)
	{
		pendingSelectionClick.ObserveDrag(ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f));
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			pendingSelectionClick.Finish(*selection, pendingClickHovered);
			SetLegacySelected(selection, selected);
		}
		else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			pendingSelectionClick.Cancel();
		}
	}
	if (selection)
	{
		auto reconciled = selection->entries;
		for (auto& entry : reconciled)
		{
			if (entry.kind == EditorSelectionKind::GameObject && entry.documentId == documentId &&
			    entry.playSession == playSession)
			{
				entry.missing = !scene.GameObjectById(entry.objectId);
			}
		}
		selection->ReconcileEntries(std::move(reconciled));
		SetLegacySelected(selection, selected);
	}
	if (selected)
	{
		auto selectedObject = SelectedGameObject(scene);
		if (!selectedObject)
		{
			ClearSelection();
		}
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
