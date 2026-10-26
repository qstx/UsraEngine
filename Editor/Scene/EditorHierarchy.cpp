#include <Ui/EditorIcons.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

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

Vector<GameObject::Ptr> OrderedGameObjects(const Vector<GameObject::Ptr>& objects, bool alphabetical)
{
	if (!alphabetical)
	{
		return objects;
	}
	struct NamedObject
	{
		String sortName;
		GameObject::Ptr object;
	};
	Vector<NamedObject> named;
	named.reserve(objects.size());
	for (const GameObject::Ptr& object : objects)
	{
		const auto name = object ? object->Name() : Result<String>::Failure("Missing GameObject");
		named.push_back({name ? name.value : String{}, object});
	}
	std::stable_sort(named.begin(), named.end(), [](const NamedObject& left, const NamedObject& right)
	                  {
		                  Size leftIndex = 0;
		                  Size rightIndex = 0;
		                  while (leftIndex < left.sortName.size() && rightIndex < right.sortName.size())
		                  {
			                  const unsigned char leftCharacter =
			                      static_cast<unsigned char>(left.sortName[leftIndex]);
			                  const unsigned char rightCharacter =
			                      static_cast<unsigned char>(right.sortName[rightIndex]);
			                  if (std::isdigit(leftCharacter) && std::isdigit(rightCharacter))
			                  {
				                  Size leftDigits = leftIndex;
				                  Size rightDigits = rightIndex;
				                  while (leftDigits < left.sortName.size() && left.sortName[leftDigits] == '0') ++leftDigits;
				                  while (rightDigits < right.sortName.size() && right.sortName[rightDigits] == '0') ++rightDigits;
				                  Size leftEnd = leftDigits;
				                  Size rightEnd = rightDigits;
				                  while (leftEnd < left.sortName.size() && std::isdigit(static_cast<unsigned char>(left.sortName[leftEnd]))) ++leftEnd;
				                  while (rightEnd < right.sortName.size() && std::isdigit(static_cast<unsigned char>(right.sortName[rightEnd]))) ++rightEnd;
				                  const Size leftLength = leftEnd - leftDigits;
				                  const Size rightLength = rightEnd - rightDigits;
				                  if (leftLength != rightLength) return leftLength < rightLength;
				                  const int comparison = left.sortName.compare(leftDigits, leftLength,
				                                                       right.sortName, rightDigits, rightLength);
				                  if (comparison != 0) return comparison < 0;
				                  Size leftFullEnd = leftEnd;
				                  Size rightFullEnd = rightEnd;
				                  while (leftFullEnd < left.sortName.size() && std::isdigit(static_cast<unsigned char>(left.sortName[leftFullEnd]))) ++leftFullEnd;
				                  while (rightFullEnd < right.sortName.size() && std::isdigit(static_cast<unsigned char>(right.sortName[rightFullEnd]))) ++rightFullEnd;
				                  if (leftFullEnd - leftIndex != rightFullEnd - rightIndex)
					                  {
					                  return leftFullEnd - leftIndex < rightFullEnd - rightIndex;
				                  }
				                  leftIndex = leftFullEnd;
				                  rightIndex = rightFullEnd;
			                  continue;
			                  }
			                  const int leftLower = std::tolower(leftCharacter);
			                  const int rightLower = std::tolower(rightCharacter);
			                  if (leftLower != rightLower) return leftLower < rightLower;
			                  ++leftIndex;
			                  ++rightIndex;
	                  }
	                  return left.sortName.size() < right.sortName.size();
	                  });
	Vector<GameObject::Ptr> result;
	result.reserve(named.size());
	for (NamedObject& item : named)
	{
		result.push_back(std::move(item.object));
	}
	return result;
}

void AppendVisibleRows(const Scene& scene, GameObject::Ptr object, ResourceId documentId,
                       Vector<EditorSelectionEntry>& visibleRows, const char* treeNodeId, bool playSession,
                       const Map<String, bool>& searchMatches, bool filtering, bool expandFilteredBranches,
                       bool alphabetical)
{
	const auto id = scene.PersistentId(object);
	const auto children = scene.ChildrenOf(object);
	if (!id || !children)
	{
		return;
	}
	if (filtering)
	{
		const auto match = searchMatches.find(id.value.ToString());
		if (match == searchMatches.end() || !match->second)
		{
			return;
		}
	}
	visibleRows.push_back(GameObjectSelection(documentId, id.value, playSession));
	if (children.value.empty())
	{
		return;
	}
	const String key = id.value.ToString();
	ImGui::PushID(key.c_str());
	const bool open = (filtering && expandFilteredBranches) || ImGui::TreeNodeGetOpen(ImGui::GetID(treeNodeId));
	ImGui::PopID();
	if (open)
	{
		for (const auto& child : OrderedGameObjects(children.value, alphabetical))
		{
			AppendVisibleRows(scene, child, documentId, visibleRows, treeNodeId, playSession, searchMatches, filtering,
			                  expandFilteredBranches, alphabetical);
		}
	}
}

void AppendVisibleRows(const GameObject::Ptr& object, ResourceId documentId, Vector<EditorSelectionEntry>& visibleRows,
                       const char* treeNodeId, const Map<String, bool>& searchMatches, bool filtering,
                       bool expandFilteredBranches)
{
	if (!object)
	{
		return;
	}
	if (filtering)
	{
		const auto match = searchMatches.find(object->Id().ToString());
		if (match == searchMatches.end() || !match->second)
		{
			return;
		}
	}
	visibleRows.push_back(GameObjectSelection(documentId, object->Id()));
	const auto children = object->Children();
	if (children.empty())
	{
		return;
	}
	const String key = object->Id().ToString();
	ImGui::PushID(key.c_str());
	const bool open = (filtering && expandFilteredBranches) || ImGui::TreeNodeGetOpen(ImGui::GetID(treeNodeId));
	ImGui::PopID();
	if (open)
	{
		for (const auto& child : children)
		{
			AppendVisibleRows(child, documentId, visibleRows, treeNodeId, searchMatches, filtering,
			                  expandFilteredBranches);
		}
	}
}

bool MatchesHierarchySearch(const GameObject::Ptr& object, const ImGuiTextFilter& filter, bool typeOnly)
{
	if (!object)
	{
		return false;
	}
	const auto name = object->Name();
	const bool nameMatches = !typeOnly && name && filter.PassFilter(name.value.c_str());
	const auto components = object->Components();
	const bool typeMatches = std::any_of(components.begin(), components.end(),
	                                    [&](const Component* component)
	                                    {
		                                    return component && filter.PassFilter(component->SerializationType().c_str());
	                                    });
	return nameMatches || typeMatches;
}

bool BuildSceneSearchMatches(const Scene& scene, const GameObject::Ptr& object, const ImGuiTextFilter& filter,
                             bool typeOnly, Map<String, bool>& matches, Map<String, bool>& directMatches)
{
	const auto id = scene.PersistentId(object);
	const auto children = scene.ChildrenOf(object);
	if (!id || !children)
	{
		return false;
	}
	const bool directMatch = MatchesHierarchySearch(object, filter, typeOnly);
	bool matched = directMatch;
	for (const auto& child : children.value)
	{
		matched = BuildSceneSearchMatches(scene, child, filter, typeOnly, matches, directMatches) || matched;
	}
	matches.emplace(id.value.ToString(), matched);
	directMatches.emplace(id.value.ToString(), directMatch);
	return matched;
}

bool BuildPrefabSearchMatches(const GameObject::Ptr& object, const ImGuiTextFilter& filter, bool typeOnly,
                              Map<String, bool>& matches, Map<String, bool>& directMatches)
{
	if (!object)
	{
		return false;
	}
	const bool directMatch = MatchesHierarchySearch(object, filter, typeOnly);
	bool matched = directMatch;
	for (const auto& child : object->Children())
	{
		matched = BuildPrefabSearchMatches(child, filter, typeOnly, matches, directMatches) || matched;
	}
	matches.emplace(object->Id().ToString(), matched);
	directMatches.emplace(object->Id().ToString(), directMatch);
	return matched;
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

Result<EditorDragPayload> BeginHierarchyDragSession(const EditorSelectionContext& selection,
                                                    const EditorSelectionEntry& clicked,
                                                    const Vector<EditorSelectionEntry>& visibleRows)
{
	if (!selection.Contains(clicked))
	{
		return BeginEditorDragSession({clicked}, selection.revision);
	}
	Vector<EditorSelectionEntry> sources;
	sources.reserve(selection.entries.size());
	for (const auto& row : visibleRows)
	{
		if (selection.Contains(row))
		{
			sources.push_back(row);
		}
	}
	for (const auto& entry : selection.entries)
	{
		if (std::none_of(sources.begin(), sources.end(),
		                 [&](const auto& source) { return source.SameIdentity(entry); }))
		{
			sources.push_back(entry);
		}
	}
	return BeginEditorDragSession(sources, selection.revision);
}

bool SelectHierarchyEntry(const EditorSelectionEntry& entry, const Vector<EditorSelectionEntry>& visibleRows,
                          EditorSelectionContext* selection, ResourceId& selected, bool extend,
                          ResourceId& keyboardFocusTarget)
{
	const bool accepted = selection ? selection->SelectClicked(entry, visibleRows, false, extend) : true;
	if (!accepted)
	{
		return false;
	}
	if (selection)
	{
		SetLegacySelected(selection, selected);
	}
	else
	{
		selected = entry.objectId;
	}
	keyboardFocusTarget = entry.objectId;
	return true;
}

bool NavigateHierarchySelectionByOffset(const Vector<EditorSelectionEntry>& visibleRows,
                                        EditorSelectionContext* selection, ResourceId& selected, Int32 offset,
                                        bool extend, ResourceId& keyboardFocusTarget)
{
	if (visibleRows.empty() || offset == 0)
	{
		return false;
	}
	const EditorSelectionEntry* primary = selection ? selection->PrimaryEntry() : nullptr;
	const ResourceId currentId =
	    primary && primary->kind == EditorSelectionKind::GameObject ? primary->objectId : selected;
	const auto current = std::find_if(visibleRows.begin(), visibleRows.end(),
	                                  [&](const EditorSelectionEntry& entry) { return entry.objectId == currentId; });
	const Int64 currentIndex =
	    current == visibleRows.end() ? -1 : static_cast<Int64>(std::distance(visibleRows.begin(), current));
	const Int64 lastIndex = static_cast<Int64>(visibleRows.size() - 1);
	const Int64 target = std::clamp<Int64>(currentIndex + offset, 0, lastIndex);
	if (target == currentIndex)
	{
		return false;
	}
	const Size targetIndex = static_cast<Size>(target);
	return SelectHierarchyEntry(visibleRows[targetIndex], visibleRows, selection, selected, extend,
	                            keyboardFocusTarget);
}

bool NavigateHierarchySelection(const Vector<EditorSelectionEntry>& visibleRows, EditorSelectionContext* selection,
                                ResourceId& selected, Int32 direction, bool extend, ResourceId& keyboardFocusTarget)
{
	return NavigateHierarchySelectionByOffset(visibleRows, selection, selected, direction, extend, keyboardFocusTarget);
}

bool NavigateHierarchySelectionToEdge(const Vector<EditorSelectionEntry>& visibleRows,
                                      EditorSelectionContext* selection, ResourceId& selected, bool last, bool extend,
                                      ResourceId& keyboardFocusTarget)
{
	if (visibleRows.empty())
	{
		return false;
	}
	const Size targetIndex = last ? visibleRows.size() - 1 : 0;
	return SelectHierarchyEntry(visibleRows[targetIndex], visibleRows, selection, selected, extend,
	                            keyboardFocusTarget);
}

bool EnsureHierarchySelectionVisible(const Vector<EditorSelectionEntry>& visibleRows,
                                     EditorSelectionContext* selection, ResourceId documentId, ResourceId& selected,
                                     ResourceId& keyboardFocusTarget)
{
	if (visibleRows.empty())
	{
		return false;
	}
	const auto* primary = selection ? selection->PrimaryEntry() : nullptr;
	const ResourceId activeId =
	    primary && primary->kind == EditorSelectionKind::GameObject && primary->documentId == documentId
	        ? primary->objectId
	        : selected;
	const auto active = std::find_if(visibleRows.begin(), visibleRows.end(),
	                                 [&](const EditorSelectionEntry& entry)
	                                 { return entry.objectId == activeId && entry.documentId == documentId; });
	if (active != visibleRows.end())
	{
		keyboardFocusTarget = active->objectId;
		return true;
	}
	return SelectHierarchyEntry(visibleRows.front(), visibleRows, selection, selected, false, keyboardFocusTarget);
}

bool IsHierarchyNodeOpen(const String& key, const char* nodeId)
{
	ImGui::PushID(key.c_str());
	const bool open = ImGui::TreeNodeGetOpen(ImGui::GetID(nodeId));
	ImGui::PopID();
	return open;
}

void SetHierarchyNodeOpen(const String& key, const char* nodeId, bool open)
{
	ImGui::PushID(key.c_str());
	ImGui::TreeNodeSetOpen(ImGui::GetID(nodeId), open);
	ImGui::PopID();
}

void SetHierarchyDescendantsOpen(const GameObject::Ptr& parent, const char* nodeId, bool open,
                                 const std::function<String(const GameObject::Ptr&)>& keyForObject)
{
	if (!parent)
	{
		return;
	}
	for (const GameObject::Ptr& child : parent->Children())
	{
		const String key = keyForObject(child);
		ImGui::PushID(key.c_str());
		ImGui::TreeNodeSetOpen(ImGui::GetID(nodeId), open);
		ImGui::PopID();
		SetHierarchyDescendantsOpen(child, nodeId, open, keyForObject);
	}
}

bool DrawHierarchySearchField(std::array<char, 257>& searchText, Float32 trailingWidth = 0.f)
{
	Icons::Inline(Icons::Icon::Search);
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
	{
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(trailingWidth > 0.f ? -trailingWidth : -1.f);
	ImGui::InputTextWithHint("##HierarchySearch", "Search Hierarchy (t:Type)", searchText.data(), searchText.size(),
	                         ImGuiInputTextFlags_EscapeClearsAll);
	return ImGui::IsItemActive();
}

void DrawEyeGlyph(ImDrawList* drawList, ImVec2 center, Float32 size, bool visible, ImU32 color)
{
	const Float32 halfWidth = size * 0.43f;
	const Float32 halfHeight = size * 0.27f;
	drawList->AddBezierCubic({center.x - halfWidth, center.y}, {center.x - halfWidth * 0.5f, center.y - halfHeight},
	                         {center.x + halfWidth * 0.5f, center.y - halfHeight}, {center.x + halfWidth, center.y},
	                         color, 1.3f, 12);
	drawList->AddBezierCubic({center.x + halfWidth, center.y}, {center.x + halfWidth * 0.5f, center.y + halfHeight},
	                         {center.x - halfWidth * 0.5f, center.y + halfHeight}, {center.x - halfWidth, center.y},
	                         color, 1.3f, 12);
	if (visible)
	{
		drawList->AddCircle(center, size * 0.12f, color, 10, 1.2f);
		drawList->AddCircleFilled(center, size * 0.055f, color, 8);
	}
	else
	{
		drawList->AddLine({center.x - size * 0.42f, center.y + size * 0.39f},
		                  {center.x + size * 0.42f, center.y - size * 0.39f}, color, 1.6f);
	}
}

void DrawPickGlyph(ImDrawList* drawList, ImVec2 center, Float32 size, bool pickable, ImU32 color)
{
	const ImVec2 points[]{
	    {center.x - size * 0.23f, center.y + size * 0.42f}, {center.x - size * 0.18f, center.y - size * 0.42f},
	    {center.x - size * 0.04f, center.y - size * 0.48f}, {center.x + size * 0.02f, center.y - size * 0.05f},
	    {center.x + size * 0.14f, center.y - size * 0.27f}, {center.x + size * 0.27f, center.y - size * 0.19f},
	    {center.x + size * 0.15f, center.y + size * 0.10f}, {center.x + size * 0.29f, center.y + size * 0.02f},
	    {center.x + size * 0.38f, center.y + size * 0.14f}, {center.x + size * 0.12f, center.y + size * 0.43f}};
	drawList->AddPolyline(points, static_cast<Int32>(std::size(points)), color, ImDrawFlags_Closed, 1.35f);
	if (!pickable)
	{
		drawList->AddLine({center.x - size * 0.45f, center.y + size * 0.45f},
		                  {center.x + size * 0.45f, center.y - size * 0.45f}, color, 1.6f);
	}
}

ImRect SceneRowControlRect(const ImRect& row, Size index)
{
	const Float32 size = std::min(15.f, row.GetHeight() - 2.f);
	const Float32 gap = 2.f;
	const Float32 x = row.Max.x - (size + gap) * static_cast<Float32>(2 - index);
	const Float32 y = row.Min.y + (row.GetHeight() - size) * 0.5f;
	return {{x, y}, {x + size, y + size}};
}

bool DrawSceneRowControl(const char* id, const ImRect& rect, bool eye, bool enabled, const char* tooltip)
{
	if (rect.GetWidth() <= 0.f)
	{
		return false;
	}
	ImGui::SetCursorScreenPos(rect.Min);
	ImGui::PushID(id);
	const bool clicked = ImGui::InvisibleButton("##SceneRowControl", rect.GetSize());
	const bool hovered = ImGui::IsItemHovered();
	const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
	const ImVec2 center{(rect.Min.x + rect.Max.x) * 0.5f, (rect.Min.y + rect.Max.y) * 0.5f};
	if (eye)
	{
		DrawEyeGlyph(ImGui::GetWindowDrawList(), center, rect.GetHeight() - 3.f, enabled, color);
	}
	else
	{
		DrawPickGlyph(ImGui::GetWindowDrawList(), center, rect.GetHeight() - 3.f, enabled, color);
	}
	if (hovered)
	{
		ImGui::SetTooltip("%s%s", tooltip,
		                  ImGui::GetIO().KeyAlt ? " (Alt: this object only)" : " (including children)");
	}
	ImGui::PopID();
	return clicked;
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
bool IsHierarchyAssetDropSession(const EditorDragSession& session)
{
	return !session.sources.empty() &&
	       std::all_of(session.sources.begin(), session.sources.end(),
	                   [](const EditorDragSource& source)
	                   {
		                   const auto& entry = source.identity;
		                   return !source.componentId && entry.kind == EditorSelectionKind::Asset &&
		                          entry.assetRole == EditorAssetSelectionRole::Source && !entry.assetLocalFileId &&
		                          entry.assetGuid && !entry.IsMissing();
	                   });
}

std::pair<ResourceId, HierarchyDropZone>
ResolveHierarchyDropAtCursor(const Vector<EditorSelectionEntry>& visibleRows, ResourceId documentId, bool playSession,
                             const std::function<ResourceId(ResourceId)>& parentOf, const EditorDragSession& session,
                             ResourceId targetId, HierarchyDropZone zone, float cursorX, float contentStartX,
                             float indentWidth)
{
	if (zone == HierarchyDropZone::Into || !targetId || visibleRows.empty() || indentWidth <= 0.0f)
	{
		return {targetId, zone};
	}
	const auto target = std::find_if(visibleRows.begin(), visibleRows.end(),
	                                 [&](const EditorSelectionEntry& entry) { return entry.objectId == targetId; });
	if (target == visibleRows.end())
	{
		return {targetId, zone};
	}
	Vector<ResourceId> draggedIds;
	if (auto normalized = NormalizeHierarchyDragSources(session, documentId, playSession, parentOf))
	{
		for (const auto& entry : normalized.value)
		{
			draggedIds.push_back(entry.objectId);
		}
	}
	const Size targetIndex = static_cast<Size>(std::distance(visibleRows.begin(), target));
	Size previousIndex = zone == HierarchyDropZone::Above ? targetIndex : targetIndex + 1;
	Size nextIndex = zone == HierarchyDropZone::Above ? targetIndex : targetIndex + 1;
	std::optional<Size> previous;
	std::optional<Size> next;
	while (previousIndex > 0)
	{
		--previousIndex;
		if (std::find(draggedIds.begin(), draggedIds.end(), visibleRows[previousIndex].objectId) == draggedIds.end())
		{
			previous = previousIndex;
			break;
		}
	}
	while (nextIndex < visibleRows.size())
	{
		if (std::find(draggedIds.begin(), draggedIds.end(), visibleRows[nextIndex].objectId) == draggedIds.end())
		{
			next = nextIndex;
			break;
		}
		++nextIndex;
	}
	if (!previous)
	{
		return {targetId, zone};
	}
	auto depthOf = [&](ResourceId objectId)
	{
		Size depth = 0;
		ResourceId ancestor = parentOf(objectId);
		while (ancestor && depth <= visibleRows.size())
		{
			++depth;
			const ResourceId parent = parentOf(ancestor);
			if (parent == ancestor)
			{
				break;
			}
			ancestor = parent;
		}
		return depth;
	};
	const ResourceId previousId = visibleRows[*previous].objectId;
	const Size previousDepth = depthOf(previousId);
	const Size nextDepth = next ? depthOf(visibleRows[*next].objectId) : 0;
	const bool previousExpanded =
	    std::any_of(visibleRows.begin(), visibleRows.end(),
	                [&](const EditorSelectionEntry& entry) { return parentOf(entry.objectId) == previousId; });
	const bool betweenParentAndFirstChild =
	    previousExpanded && next && parentOf(visibleRows[*next].objectId) == previousId;
	const Size minimumDepth = next ? nextDepth : 0;
	const Size maximumDepth = previousDepth + (betweenParentAndFirstChild ? 1 : 0);
	ResourceId resolvedTarget = previousId;
	HierarchyDropZone resolvedZone = HierarchyDropZone::Below;
	const float requestedDepth = std::floor((cursorX - contentStartX) / indentWidth);
	if (maximumDepth <= minimumDepth || requestedDepth >= static_cast<float>(maximumDepth))
	{
		if (betweenParentAndFirstChild)
		{
			resolvedTarget = visibleRows[*next].objectId;
			resolvedZone = HierarchyDropZone::Above;
		}
		return {resolvedTarget, resolvedZone};
	}
	ResourceId ancestor = previousId;
	Size ancestorDepth = previousDepth;
	while (ancestorDepth > minimumDepth && static_cast<float>(ancestorDepth) != requestedDepth)
	{
		ancestor = parentOf(ancestor);
		if (!ancestor)
		{
			break;
		}
		--ancestorDepth;
	}
	return {ancestor ? ancestor : previousId, resolvedZone};
}

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
                             const std::function<void(const EditorDragSession&, HierarchyDropZone)>& onDrop,
                             bool allowReorder = true)
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
		if (session && (NormalizeHierarchyDragSources(session.value, documentId, playSession, parentOf) ||
		                (onDrop && IsHierarchyAssetDropSession(session.value))))
		{
			const HierarchyDropZone classified =
			    ClassifyHierarchyDropZone(ImGui::GetIO().MousePos.y, rect.Min.y, rect.Max.y);
			if (allowReorder || classified == HierarchyDropZone::Into)
			{
				zone = classified;
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
		if (session && (NormalizeHierarchyDragSources(session.value, documentId, playSession, parentOf) ||
		                (onDrop && IsHierarchyAssetDropSession(session.value))))
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

Result<void> SetSceneGameObjectWorldTransform(Scene& scene, const GameObject::Ptr& object,
                                               const Mat4& worldTransform)
{
	if (!object || !scene.Contains(object))
	{
		return Result<void>::Failure("Cannot set the world transform of a missing GameObject");
	}
	auto parent = scene.ParentOf(object);
	if (!parent)
	{
		return Result<void>::Failure(parent.error);
	}
	Mat4 localMatrix = worldTransform;
	if (parent.value)
	{
		auto parentWorld = scene.WorldTransform(parent.value);
		if (!parentWorld)
		{
			return Result<void>::Failure(parentWorld.error);
		}
		try
		{
			localMatrix = parentWorld.value.InverseAffine() * worldTransform;
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(String("Cannot preserve GameObject world transform: ") + ToUrsaString(error.what()));
		}
	}
	auto local = DecomposeHierarchyLocal(localMatrix);
	return local ? scene.SetLocalTransform(object, local.value) : Result<void>::Failure(local.error);
}

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

Result<ResourceId> CreateEmptyParent(Scene& scene, const Vector<EditorSelectionEntry>& selection, ResourceId documentId)
{
	if (selection.empty())
	{
		return Result<ResourceId>::Failure("Select one or more GameObjects to create a parent");
	}
	Vector<GameObject::Ptr> children;
	GameObject::Ptr commonParent;
	for (const EditorSelectionEntry& entry : selection)
	{
		if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != documentId || entry.playSession ||
		    entry.IsMissing())
		{
			return Result<ResourceId>::Failure("Create Empty Parent requires editable GameObjects from one scene");
		}
		auto resolved = scene.GameObjectById(entry.objectId);
		if (!resolved)
		{
			return Result<ResourceId>::Failure("A selected GameObject no longer exists");
		}
		if (children.empty())
		{
			commonParent = resolved.value->Parent();
		}
		else if (resolved.value->Parent() != commonParent)
		{
			return Result<ResourceId>::Failure("Create Empty Parent requires selected siblings");
		}
		children.push_back(resolved.value);
	}
	Vector<GameObject::Ptr> siblings = commonParent ? scene.ChildrenOf(commonParent).value : scene.RootGameObjects();
	std::sort(children.begin(), children.end(), [&](const auto& left, const auto& right)
	          { return std::find(siblings.begin(), siblings.end(), left) < std::find(siblings.begin(), siblings.end(), right); });
	struct SavedTransform
	{
		GameObject::Ptr object;
		LocalTransform local;
		Mat4 world;
	};
	Vector<SavedTransform> saved;
	for (const auto& child : children)
	{
		auto local = scene.GetLocalTransform(child);
		auto world = scene.WorldTransform(child);
		if (!local || !world)
		{
			return Result<ResourceId>::Failure(!local ? local.error : world.error);
		}
		saved.push_back({child, local.value, world.value});
	}
	const auto insertion = std::find(siblings.begin(), siblings.end(), children.front());
	if (insertion == siblings.end())
	{
		return Result<ResourceId>::Failure("A selected GameObject is missing from its sibling list");
	}
	auto created = scene.CreateGameObject("GameObject", commonParent);
	if (!created)
	{
		return Result<ResourceId>::Failure(created.error);
	}
	auto newParentId = scene.PersistentId(created.value);
	if (!newParentId)
	{
		scene.DestroyGameObject(created.value);
		return Result<ResourceId>::Failure(newParentId.error);
	}
	auto parentWorld = scene.WorldTransform(created.value);
	if (!parentWorld)
	{
		scene.DestroyGameObject(created.value);
		return Result<ResourceId>::Failure(parentWorld.error);
	}
	Vector<LocalTransform> childLocals;
	for (const SavedTransform& state : saved)
	{
		Result<LocalTransform> local = Result<LocalTransform>::Failure("Could not decompose child transform");
		try
		{
			local = DecomposeHierarchyLocal(parentWorld.value.InverseAffine() * state.world);
		}
		catch (const std::exception& error)
		{
			scene.DestroyGameObject(created.value);
			return Result<ResourceId>::Failure(String("Could not preserve child world transform: ") + error.what());
		}
		if (!local)
		{
			scene.DestroyGameObject(created.value);
			return Result<ResourceId>::Failure(local.error);
		}
		childLocals.push_back(local.value);
	}
	auto rollback = [&]()
	{
		for (const SavedTransform& state : saved)
		{
			scene.SetParent(state.object, commonParent);
			scene.SetLocalTransform(state.object, state.local);
		}
		for (Ursa::Size index = 0; index < siblings.size(); ++index)
		{
			scene.ReorderGameObject(siblings[index], index);
		}
		scene.DestroyGameObject(created.value);
	};
	for (Ursa::Size index = 0; index < saved.size(); ++index)
	{
		auto reparented = scene.SetParent(saved[index].object, created.value);
		if (!reparented)
		{
			rollback();
			return Result<ResourceId>::Failure(reparented.error);
		}
		auto transformed = scene.SetLocalTransform(saved[index].object, childLocals[index]);
		if (!transformed)
		{
			rollback();
			return Result<ResourceId>::Failure(transformed.error);
		}
		auto reordered = scene.ReorderGameObject(saved[index].object, index);
		if (!reordered)
		{
			rollback();
			return Result<ResourceId>::Failure(reordered.error);
		}
	}
	const Ursa::Size insertionIndex = static_cast<Ursa::Size>(std::distance(siblings.begin(), insertion));
	auto reorderedParent = scene.ReorderGameObject(created.value, insertionIndex);
	if (!reorderedParent)
	{
		rollback();
		return Result<ResourceId>::Failure(reorderedParent.error);
	}
	return Result<ResourceId>::Success(newParentId.value);
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

Result<void> EditorHierarchy::SelectChildren(const Scene& scene, EditorSelectionContext& selection,
                                             ResourceId documentId, bool playSession)
{
	if (selection.entries.empty())
	{
		return Result<void>::Failure("Select one or more GameObjects first");
	}
	Vector<EditorSelectionEntry> children = selection.entries;
	std::function<Result<void>(const GameObject::Ptr&)> visit = [&](const GameObject::Ptr& object) -> Result<void>
	{
		for (const auto& child : object->Children())
		{
			auto id = scene.PersistentId(child);
			if (!id)
			{
				return Result<void>::Failure(id.error);
			}
			EditorSelectionEntry selectedChild;
			selectedChild.kind = EditorSelectionKind::GameObject;
			selectedChild.documentId = documentId;
			selectedChild.objectId = id.value;
			selectedChild.playSession = playSession;
			if (std::none_of(children.begin(), children.end(),
			                 [&](const auto& existing) { return existing.SameIdentity(selectedChild); }))
			{
				children.push_back(std::move(selectedChild));
			}
			auto descendants = visit(child);
			if (!descendants)
			{
				return descendants;
			}
		}
		return {};
	};
	for (const auto& entry : selection.entries)
	{
		if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != documentId ||
		    entry.playSession != playSession || entry.IsMissing())
		{
			return Result<void>::Failure("Select GameObjects in the active Hierarchy first");
		}
		auto object = scene.GameObjectById(entry.objectId);
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		auto descendants = visit(object.value);
		if (!descendants)
		{
			return descendants;
		}
	}
	if (!selection.SelectEntries(std::move(children)))
	{
		return Result<void>::Failure("The current selection cannot be changed yet");
	}
	selected = selection.PrimaryEntry() ? selection.PrimaryEntry()->objectId : ResourceId{};
	return {};
}

Result<void> EditorHierarchy::SelectChildren(const PrefabAsset& asset, EditorSelectionContext& selection,
                                             ResourceId documentId)
{
	if (selection.entries.empty() || !asset.Root())
	{
		return Result<void>::Failure("Select one or more GameObjects in the Prefab Hierarchy first");
	}
	Vector<EditorSelectionEntry> children = selection.entries;
	std::function<void(const GameObject::Ptr&)> visit = [&](const GameObject::Ptr& object)
	{
		for (const auto& child : object->Children())
		{
			EditorSelectionEntry selectedChild;
			selectedChild.kind = EditorSelectionKind::GameObject;
			selectedChild.documentId = documentId;
			selectedChild.objectId = child->Id();
			if (std::none_of(children.begin(), children.end(),
			                 [&](const auto& existing) { return existing.SameIdentity(selectedChild); }))
			{
				children.push_back(std::move(selectedChild));
			}
			visit(child);
		}
	};
	for (const auto& entry : selection.entries)
	{
		if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != documentId || entry.playSession ||
		    entry.IsMissing())
		{
			return Result<void>::Failure("Select GameObjects in the active Prefab Hierarchy first");
		}
		GameObject::Ptr parent;
		std::function<void(const GameObject::Ptr&)> find = [&](const GameObject::Ptr& object)
		{
			if (!object || parent)
			{
				return;
			}
			if (object->Id() == entry.objectId)
			{
				parent = object;
				return;
			}
			for (const auto& child : object->Children())
			{
				find(child);
			}
		};
		find(asset.Root());
		if (!parent)
		{
			return Result<void>::Failure("A selected GameObject no longer exists in the Prefab");
		}
		visit(parent);
	}
	if (!selection.SelectEntries(std::move(children)))
	{
		return Result<void>::Failure("The current selection cannot be changed yet");
	}
	selected = selection.PrimaryEntry() ? selection.PrimaryEntry()->objectId : ResourceId{};
	return {};
}

Result<void> EditorHierarchy::InvertSelection(const Scene& scene, EditorSelectionContext& selection,
                                              ResourceId documentId, bool playSession)
{
	Vector<EditorSelectionEntry> inverted;
	std::function<Result<void>(const GameObject::Ptr&)> visit = [&](const GameObject::Ptr& object) -> Result<void>
	{
		if (!object)
		{
			return {};
		}
		auto id = scene.PersistentId(object);
		if (!id)
		{
			return Result<void>::Failure(id.error);
		}
		EditorSelectionEntry entry;
		entry.kind = EditorSelectionKind::GameObject;
		entry.documentId = documentId;
		entry.objectId = id.value;
		entry.playSession = playSession;
		if (!selection.Contains(entry))
		{
			inverted.push_back(entry);
		}
		for (const auto& child : object->Children())
		{
			auto visited = visit(child);
			if (!visited)
			{
				return visited;
			}
		}
		return {};
	};
	for (const auto& root : scene.RootGameObjects())
	{
		auto visited = visit(root);
		if (!visited)
		{
			return visited;
		}
	}
	if (!selection.SelectEntries(std::move(inverted)))
	{
		return Result<void>::Failure("The current selection cannot be changed yet");
	}
	selected = selection.PrimaryEntry() ? selection.PrimaryEntry()->objectId : ResourceId{};
	return {};
}

Result<void> EditorHierarchy::InvertSelection(const PrefabAsset& asset, EditorSelectionContext& selection,
                                              ResourceId documentId)
{
	if (!asset.Root())
	{
		return Result<void>::Failure("The active Prefab has no root GameObject");
	}
	Vector<EditorSelectionEntry> inverted;
	std::function<void(const GameObject::Ptr&)> visit = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		EditorSelectionEntry entry;
		entry.kind = EditorSelectionKind::GameObject;
		entry.documentId = documentId;
		entry.objectId = object->Id();
		if (!selection.Contains(entry))
		{
			inverted.push_back(entry);
		}
		for (const auto& child : object->Children())
		{
			visit(child);
		}
	};
	visit(asset.Root());
	if (!selection.SelectEntries(std::move(inverted)))
	{
		return Result<void>::Failure("The current selection cannot be changed yet");
	}
	selected = selection.PrimaryEntry() ? selection.PrimaryEntry()->objectId : ResourceId{};
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

bool EditorHierarchy::SceneObjectVisible(ResourceId id) const
{
	const auto found = std::find_if(sceneVisibilityOverrides.begin(), sceneVisibilityOverrides.end(),
	                                [&](const auto& entry) { return entry.first == id; });
	return found == sceneVisibilityOverrides.end() || found->second;
}

bool EditorHierarchy::SceneObjectPickable(ResourceId id) const
{
	const auto found = std::find_if(scenePickabilityOverrides.begin(), scenePickabilityOverrides.end(),
	                                [&](const auto& entry) { return entry.first == id; });
	return found == scenePickabilityOverrides.end() || found->second;
}

void EditorHierarchy::SetDefaultParent(ResourceId documentId, ResourceId objectId)
{
	defaultParentDocument = documentId;
	defaultParent = objectId;
}

void EditorHierarchy::ClearDefaultParent()
{
	defaultParent = {};
	defaultParentDocument = {};
}

ResourceId EditorHierarchy::DefaultParent(const Scene& scene, ResourceId documentId) const
{
	if (!defaultParent || defaultParentDocument != documentId || !scene.GameObjectById(defaultParent))
	{
		return {};
	}
	return defaultParent;
}

void EditorHierarchy::ToggleSceneObjectVisibility(Scene& scene, const GameObject::Ptr& object, bool onlyThisObject)
{
	ToggleSceneObjectsVisibility(scene, Vector<GameObject::Ptr>{object}, onlyThisObject);
}

void EditorHierarchy::ToggleSceneObjectsVisibility(Scene& scene, const Vector<GameObject::Ptr>& objects,
                                                   bool onlyThisObject)
{
	Vector<ResourceId> affected;
	const auto addId = [&](ResourceId id)
	{
		if (id && std::find(affected.begin(), affected.end(), id) == affected.end())
		{
			affected.push_back(id);
		}
	};
	std::function<void(const GameObject::Ptr&)> appendTree = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		auto id = scene.PersistentId(object);
		if (id)
		{
			addId(id.value);
		}
		if (onlyThisObject)
		{
			return;
		}
		auto children = scene.ChildrenOf(object);
		if (children)
		{
			for (const auto& child : children.value)
			{
				appendTree(child);
			}
		}
	};
	for (const auto& object : objects)
	{
		appendTree(object);
	}
	if (affected.empty())
	{
		return;
	}
	const bool allVisible =
	    std::all_of(affected.begin(), affected.end(), [&](ResourceId id) { return SceneObjectVisible(id); });
	const bool visible = !allVisible;
	for (const ResourceId id : affected)
	{
		const auto found = std::find_if(sceneVisibilityOverrides.begin(), sceneVisibilityOverrides.end(),
		                                [&](const auto& entry) { return entry.first == id; });
		if (found == sceneVisibilityOverrides.end())
		{
			sceneVisibilityOverrides.emplace_back(id, visible);
		}
		else
		{
			found->second = visible;
		}
	}
	++sceneViewOverridesRevision;
}

void EditorHierarchy::ToggleSceneObjectPickability(Scene& scene, const GameObject::Ptr& object, bool onlyThisObject)
{
	ToggleSceneObjectsPickability(scene, Vector<GameObject::Ptr>{object}, onlyThisObject);
}

void EditorHierarchy::ToggleSceneObjectsPickability(Scene& scene, const Vector<GameObject::Ptr>& objects,
                                                    bool onlyThisObject)
{
	Vector<ResourceId> affected;
	const auto addId = [&](ResourceId id)
	{
		if (id && std::find(affected.begin(), affected.end(), id) == affected.end())
		{
			affected.push_back(id);
		}
	};
	std::function<void(const GameObject::Ptr&)> appendTree = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		auto id = scene.PersistentId(object);
		if (id)
		{
			addId(id.value);
		}
		if (onlyThisObject)
		{
			return;
		}
		auto children = scene.ChildrenOf(object);
		if (children)
		{
			for (const auto& child : children.value)
			{
				appendTree(child);
			}
		}
	};
	for (const auto& object : objects)
	{
		appendTree(object);
	}
	if (affected.empty())
	{
		return;
	}
	const bool allPickable =
	    std::all_of(affected.begin(), affected.end(), [&](ResourceId id) { return SceneObjectPickable(id); });
	const bool pickable = !allPickable;
	for (const ResourceId id : affected)
	{
		const auto found = std::find_if(scenePickabilityOverrides.begin(), scenePickabilityOverrides.end(),
		                                [&](const auto& entry) { return entry.first == id; });
		if (found == scenePickabilityOverrides.end())
		{
			scenePickabilityOverrides.emplace_back(id, pickable);
		}
		else
		{
			found->second = pickable;
		}
	}
	++sceneViewOverridesRevision;
}

Vector<ResourceId> EditorHierarchy::SceneVisibilityExclusions(const Scene& scene) const
{
	Vector<ResourceId> excluded;
	std::function<void(const GameObject::Ptr&)> collect = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		auto id = scene.PersistentId(object);
		if (id && !SceneObjectVisible(id.value))
		{
			excluded.push_back(id.value);
		}
		auto children = scene.ChildrenOf(object);
		if (children)
		{
			for (const auto& child : children.value)
			{
				collect(child);
			}
		}
	};
	for (const auto& root : scene.RootGameObjects())
	{
		collect(root);
	}
	return excluded;
}

String EditorHierarchy::SerializeSceneViewOverrides(const Scene& scene) const
{
	Vector<ResourceId> sceneIds;
	std::function<void(const GameObject::Ptr&)> collect = [&](const GameObject::Ptr& object)
	{
		if (!object)
		{
			return;
		}
		const auto id = scene.PersistentId(object);
		if (id)
		{
			sceneIds.push_back(id.value);
		}
		const auto children = scene.ChildrenOf(object);
		if (children)
		{
			for (const auto& child : children.value)
			{
				collect(child);
			}
		}
	};
	for (const auto& root : scene.RootGameObjects())
	{
		collect(root);
	}

	nlohmann::json json = {
	    {"version", 1}, {"visibility", nlohmann::json::array()}, {"pickability", nlohmann::json::array()}};
	for (const auto& entry : sceneVisibilityOverrides)
	{
		if (std::find(sceneIds.begin(), sceneIds.end(), entry.first) != sceneIds.end() && !entry.second)
		{
			json["visibility"].push_back(ToStdString(entry.first.ToString()));
		}
	}
	for (const auto& entry : scenePickabilityOverrides)
	{
		if (std::find(sceneIds.begin(), sceneIds.end(), entry.first) != sceneIds.end() && !entry.second)
		{
			json["pickability"].push_back(ToStdString(entry.first.ToString()));
		}
	}
	return ToUrsaString(json.dump(2));
}

Result<void> EditorHierarchy::RestoreSceneViewOverrides(Scene& scene, const String& text)
{
	try
	{
		const auto json = nlohmann::json::parse(ToStdString(text));
		if (json.at("version").get<int>() != 1 || !json.at("visibility").is_array() ||
		    !json.at("pickability").is_array())
		{
			return Result<void>::Failure("Unsupported Scene view state version");
		}
		Vector<ResourceId> sceneIds;
		std::function<void(const GameObject::Ptr&)> collect = [&](const GameObject::Ptr& object)
		{
			if (!object)
			{
				return;
			}
			const auto id = scene.PersistentId(object);
			if (id)
			{
				sceneIds.push_back(id.value);
			}
			const auto children = scene.ChildrenOf(object);
			if (children)
			{
				for (const auto& child : children.value)
				{
					collect(child);
				}
			}
		};
		for (const auto& root : scene.RootGameObjects())
		{
			collect(root);
		}

		auto restore = [&](const nlohmann::json& values, auto& overrides)
		{
			overrides.erase(
			    std::remove_if(overrides.begin(), overrides.end(), [&](const auto& entry)
			                   { return std::find(sceneIds.begin(), sceneIds.end(), entry.first) != sceneIds.end(); }),
			    overrides.end());
			for (const auto& value : values)
			{
				const auto id = ResourceId::Parse(ToUrsaString(value.get<std::string>()));
				if (id && std::find(sceneIds.begin(), sceneIds.end(), id.value) != sceneIds.end())
				{
					overrides.emplace_back(id.value, false);
				}
			}
		};
		restore(json.at("visibility"), sceneVisibilityOverrides);
		restore(json.at("pickability"), scenePickabilityOverrides);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Cannot parse Scene view state: ") + ToUrsaString(error.what()));
	}
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
	const bool searchFieldActive = DrawHierarchySearchField(searchText);
	const String currentSearch(searchText.data());
	const bool typeOnly = currentSearch.size() > 2 &&
	                      (currentSearch.compare(0, 2, "t:") == 0 || currentSearch.compare(0, 2, "T:") == 0);
	const String filterText = typeOnly ? currentSearch.substr(2)
	                                   : (currentSearch == "t:" || currentSearch == "T:" ? String{} : currentSearch);
	ImGuiTextFilter hierarchyFilter(filterText.c_str());
	const bool filtering = !filterText.empty();
	const bool expandFilteredBranches =
	    filtering && (currentSearch != previousSearchText || documentId != previousSearchDocument);
	previousSearchText = currentSearch;
	previousSearchDocument = documentId;
	Map<String, bool> searchMatches;
	Map<String, bool> directMatches;
	if (filtering)
	{
		BuildPrefabSearchMatches(asset.Root(), hierarchyFilter, typeOnly, searchMatches, directMatches);
	}
	directSearchMatches = std::move(directMatches);
	directSearchQuery = currentSearch;
	directSearchDocument = documentId;
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
	if (renameClickCandidate && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
	    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
	{
		renameClickCandidate = {};
		renameClickReleasedAt = -1.0;
	}
	Vector<EditorSelectionEntry> visibleRows;
	AppendVisibleRows(asset.Root(), documentId, visibleRows, "##gameObject", searchMatches, filtering,
	                  expandFilteredBranches);
	visibleSelectionRows = visibleRows;
	const ImGuiID focusedHierarchyItem = ImGui::GetFocusID();
	const bool hierarchyRowFocused = focusedHierarchyItem != 0 &&
	                                 std::find(hierarchyRowIds.begin(), hierarchyRowIds.end(), focusedHierarchyItem) !=
	                                     hierarchyRowIds.end();
	hierarchyRowIds.clear();
	if (selection && hierarchyRowFocused && !visibleRows.empty() &&
	    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && selection->SelectEntries(visibleRows))
	{
		SetLegacySelected(selection, selected);
	}
	const ImGuiIO& hierarchyInput = ImGui::GetIO();
	const bool searchNavigationKey = searchFieldActive &&
	                                 (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
	                                  ImGui::IsKeyPressed(ImGuiKey_DownArrow, false));
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    (!hierarchyInput.WantTextInput || searchNavigationKey) && (hierarchyRowFocused || searchNavigationKey))
	{
		if (searchNavigationKey)
		{
			EnsureHierarchySelectionVisible(visibleRows, selection, documentId, selected, keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
		{
			NavigateHierarchySelection(visibleRows, selection, selected, 1, hierarchyInput.KeyShift,
			                           keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
		{
			NavigateHierarchySelection(visibleRows, selection, selected, -1, hierarchyInput.KeyShift,
			                           keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Home, false) || ImGui::IsKeyPressed(ImGuiKey_End, false))
		{
			const bool last = ImGui::IsKeyPressed(ImGuiKey_End, false);
			NavigateHierarchySelectionToEdge(visibleRows, selection, selected, last, hierarchyInput.KeyShift,
			                                 keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
		{
			const Int32 pageStep = static_cast<Int32>(
			    std::max(1.f, std::floor(ImGui::GetContentRegionAvail().y / ImGui::GetFrameHeight())));
			const Int32 offset = ImGui::IsKeyPressed(ImGuiKey_PageDown, false) ? pageStep : -pageStep;
			NavigateHierarchySelectionByOffset(visibleRows, selection, selected, offset, hierarchyInput.KeyShift,
			                                   keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
		{
			const bool expand = ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
			const auto* primary = selection ? selection->PrimaryEntry() : nullptr;
			const ResourceId activeId =
			    primary && primary->kind == EditorSelectionKind::GameObject && primary->documentId == documentId
			        ? primary->objectId
			        : selected;
			std::function<GameObject::Ptr(const GameObject::Ptr&)> findObject =
			    [&](const GameObject::Ptr& current) -> GameObject::Ptr
			{
				if (!current)
				{
					return {};
				}
				if (current->Id() == activeId)
				{
					return current;
				}
				for (const auto& child : current->Children())
				{
					auto match = findObject(child);
					if (match)
					{
						return match;
					}
				}
				return {};
			};
			auto object = findObject(asset.Root());
			if (object)
			{
				const String key = object->Id().ToString();
				const auto children = object->Children();
				const bool nodeOpen = IsHierarchyNodeOpen(key, "##gameObject");
				if (expand && !children.empty())
				{
					if (!nodeOpen && !filtering)
					{
						SetHierarchyNodeOpen(key, "##gameObject", true);
					}
					else
					{
						NavigateHierarchySelection(visibleRows, selection, selected, 1, false, keyboardFocusTarget);
					}
				}
				else if (!expand)
				{
					if (!children.empty() && nodeOpen && !filtering)
					{
						SetHierarchyNodeOpen(key, "##gameObject", false);
					}
					else if (auto parent = object->Parent())
					{
						const EditorSelectionEntry parentEntry = GameObjectSelection(documentId, parent->Id());
						SelectHierarchyEntry(parentEntry, visibleRows, selection, selected, false, keyboardFocusTarget);
					}
				}
			}
		}
	}
	std::function<void(const GameObject::Ptr&)> draw = [&](const GameObject::Ptr& object)
	{
		const ResourceId id = object->Id();
		const String key = id.ToString();
		if (filtering)
		{
			const auto match = searchMatches.find(key);
			if (match == searchMatches.end() || !match->second)
			{
				return;
			}
		}
		const auto name = object->Name();
		const auto children = object->Children();
		ImGui::PushID(key.c_str());
		const String label = name && !name.value.empty() ? name.value : String("GameObject ") + key.substr(0, 8);
		const bool hasContent = !children.empty();
		const bool hasVisibleChildren =
		    filtering ? std::any_of(children.begin(), children.end(),
		                            [&](const auto& child)
		                            {
			                            const auto match = searchMatches.find(child->Id().ToString());
			                            return match != searchMatches.end() && match->second;
		                            })
		              : hasContent;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
		                           ImGuiTreeNodeFlags_OpenOnDoubleClick;
		if (!hasVisibleChildren)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		const auto entry = GameObjectSelection(documentId, id);
		if (selection ? selection->Contains(entry) : selected == id)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		const auto active = object->ActiveInHierarchy();
		const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const ImVec2 rowStart = ImGui::GetCursorScreenPos();
	const bool hoveringIndentedContent = mousePosition.x >= rowStart.x + ImGui::GetTreeNodeToLabelSpacing();
		const bool autoExpand = hasContent && dragHoverTarget == id &&
		                        hoveringIndentedContent &&
		                        mousePosition.x == dragHoverPosition.x && mousePosition.y == dragHoverPosition.y &&
		                        ImGui::GetTime() - dragHoverStartedAt > 0.7;
		if (autoExpand || (filtering && expandFilteredBranches && hasVisibleChildren))
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}
		if (active && !active.value)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
		}
		const bool open =
		    Icons::TreeNode("##gameObject", flags, Icons::ForObject(object, object == asset.Root()), label.c_str());
		hierarchyRowIds.push_back(ImGui::GetItemID());
		const bool recursivelyToggleDescendants = ImGui::IsItemToggledOpen() && ImGui::GetIO().KeyAlt;
		const ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		if (keyboardFocusTarget == id)
		{
			ImGui::SetScrollHereY(0.5f);
			keyboardFocusTarget = {};
		}
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
			const auto drag = selection ? BeginHierarchyDragSession(*selection, entry, visibleRows)
			                            : BeginEditorDragSession({entry}, 0);
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
		if (dropZone == HierarchyDropZone::Into && hasContent && hoveringIndentedContent &&
		    ImGui::GetDragDropPayload())
		{
			if (dragHoverTarget != id || mousePosition.x != dragHoverPosition.x ||
			    mousePosition.y != dragHoverPosition.y)
			{
				dragHoverTarget = id;
				dragHoverStartedAt = ImGui::GetTime();
				dragHoverPosition = mousePosition;
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
				if (!filtering || searchMatches[child->Id().ToString()])
				{
					draw(child);
				}
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		if (recursivelyToggleDescendants)
		{
			SetHierarchyDescendantsOpen(object, "##gameObject", open,
			                            [](const GameObject::Ptr& child) { return child->Id().ToString(); });
		}
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

void EditorHierarchy::RefreshSceneSearchMatches(const Scene& scene, ResourceId documentId)
{
	const String currentSearch(searchText.data());
	if (currentSearch == directSearchQuery && documentId == directSearchDocument)
	{
		return;
	}
	const bool typeOnly = currentSearch.size() > 2 &&
	                      (currentSearch.compare(0, 2, "t:") == 0 || currentSearch.compare(0, 2, "T:") == 0);
	const String filterText = typeOnly ? currentSearch.substr(2)
	                                   : (currentSearch == "t:" || currentSearch == "T:" ? String{} : currentSearch);
	directSearchMatches.clear();
	directSearchQuery = currentSearch;
	directSearchDocument = documentId;
	if (filterText.empty())
	{
		return;
	}
	const ImGuiTextFilter filter(filterText.c_str());
	Map<String, bool> searchMatches;
	for (const auto& root : scene.RootGameObjects())
	{
		BuildSceneSearchMatches(scene, root, filter, typeOnly, searchMatches, directSearchMatches);
	}
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
                                             UInt64 documentRevision, bool editableScene,
                                             const Map<String, bool>& searchMatches, bool filtering,
                                             bool expandFilteredBranches)
{
	auto id = scene.PersistentId(node);
	if (filtering && id)
	{
		const auto match = searchMatches.find(id.value.ToString());
		if (match == searchMatches.end() || !match->second)
		{
			return Result<bool>::Success(false);
		}
	}
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
	const bool isDefaultParent = DefaultParent(scene, documentId) == id.value;
	if (isDefaultParent)
	{
		display += "  [Default Parent]";
	}
	const bool hasChildren = !children.value.empty();
	const Vector<GameObject::Ptr> orderedChildren = OrderedGameObjects(children.value, hierarchyAlphabeticalSort);
	const bool hasVisibleChildren =
	    filtering ? std::any_of(orderedChildren.begin(), orderedChildren.end(),
	                            [&](const auto& child)
	                            {
		                            const auto childId = scene.PersistentId(child);
		                            const auto match =
		                                childId ? searchMatches.find(childId.value.ToString()) : searchMatches.end();
		                            return match != searchMatches.end() && match->second;
	                            })
	              : hasChildren;
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
	                           ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (!hasVisibleChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	const auto entry = GameObjectSelection(documentId, id.value, playSession);
	const bool selectedBeforeClick = selection ? selection->Contains(entry) : selected == id.value;
	if (selectedBeforeClick)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	const auto active = scene.ActiveInHierarchy(node);
	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const ImVec2 rowStart = ImGui::GetCursorScreenPos();
	const bool hoveringIndentedContent = mousePosition.x >= rowStart.x + ImGui::GetTreeNodeToLabelSpacing();
	const bool autoExpand = hasChildren && dragHoverTarget == id.value &&
	                        hoveringIndentedContent &&
	                        mousePosition.x == dragHoverPosition.x && mousePosition.y == dragHoverPosition.y &&
	                        ImGui::GetTime() - dragHoverStartedAt > 0.7;
	if (autoExpand || (filtering && expandFilteredBranches && hasVisibleChildren))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	if (active && !active.value)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
	}
	if (isDefaultParent)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(125, 195, 255, 255));
	}
	const bool open =
	    Icons::TreeNode("##node", flags, Icons::ForObject(node, prefabObjects.find(key) != prefabObjects.end()),
	                    renameTarget == id.value ? "" : display.c_str());
	hierarchyRowIds.push_back(ImGui::GetItemID());
	if (isDefaultParent)
	{
		ImGui::PopStyleColor();
	}
	const bool recursivelyToggleDescendants = ImGui::IsItemToggledOpen() && ImGui::GetIO().KeyAlt;
	const ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	if (keyboardFocusTarget == id.value)
	{
		ImGui::SetScrollHereY(0.5f);
		keyboardFocusTarget = {};
	}
	const ImVec2 rowCursorAfterTree = ImGui::GetCursorScreenPos();
	pointerOverHierarchyRow |= ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, false);
	const bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	const bool rowHovered = ImGui::IsItemHovered();
	const bool rowLabelDoubleClicked =
	    rowHovered && ImGui::GetIO().MousePos.x >= rowRect.Min.x + ImGui::GetTreeNodeToLabelSpacing() &&
	    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	bool inlineRenameChanged = false;
	if (active && !active.value)
	{
		ImGui::PopStyleVar();
	}
	if (renameTarget == id.value)
	{
		const Float32 renameX = rowRect.Min.x + ImGui::GetTreeNodeToLabelSpacing();
		const Float32 renameY = rowRect.Min.y + (rowRect.GetHeight() - ImGui::GetFrameHeight()) * 0.5f;
		ImGui::SetCursorScreenPos({renameX, renameY});
		ImGui::SetNextItemWidth(std::max(40.f, rowRect.Max.x - renameX - 44.f));
		if (renameFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			renameFocusPending = false;
		}
		const bool submit = ImGui::InputText("##HierarchyRename", renameBuffer.data(), renameBuffer.size(),
		                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		const bool finish = submit || ImGui::IsItemDeactivated();
		if (cancel)
		{
			renameTarget = {};
			renameFocusPending = false;
		}
		else if (finish)
		{
			if (renameBuffer[0] != '\0')
			{
				auto renamed = scene.SetGameObjectName(node, renameBuffer.data());
				inlineRenameChanged = static_cast<bool>(renamed);
			}
			renameTarget = {};
			renameFocusPending = false;
		}
	}
	const bool visibleInScene = SceneObjectVisible(id.value);
	const bool pickableInScene = SceneObjectPickable(id.value);
	const bool showSceneControls = rowHovered || !visibleInScene || !pickableInScene;
	bool sceneControlHovered = false;
	if (showSceneControls)
	{
		const ImRect visibilityRect = SceneRowControlRect(rowRect, 0);
		const ImRect pickabilityRect = SceneRowControlRect(rowRect, 1);
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		sceneControlHovered = visibilityRect.Contains(mouse) || pickabilityRect.Contains(mouse);
		if (DrawSceneRowControl("SceneVisibility", visibilityRect, true, visibleInScene, "Toggle Scene visibility"))
		{
			ToggleSceneObjectVisibility(scene, node, ImGui::GetIO().KeyAlt);
		}
		if (DrawSceneRowControl("ScenePickability", pickabilityRect, false, pickableInScene,
		                        "Toggle Scene pickability"))
		{
			ToggleSceneObjectPickability(scene, node, ImGui::GetIO().KeyAlt);
		}
		ImGui::SetCursorScreenPos(rowCursorAfterTree);
	}
	if (rowClicked && !sceneControlHovered)
	{
		const ImGuiIO& clickInput = ImGui::GetIO();
		const bool labelClicked = clickInput.MousePos.x >= rowRect.Min.x + ImGui::GetTreeNodeToLabelSpacing();
		const bool singleUnmodifiedClick = !clickInput.KeyCtrl && !clickInput.KeySuper && !clickInput.KeyShift &&
		                                   !clickInput.KeyAlt && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		if (renameClickCandidate)
		{
			renameClickCandidate = {};
			renameClickReleasedAt = -1.0;
		}
		if (selectedBeforeClick && labelClicked && singleUnmodifiedClick && editableScene && !playSession &&
		    (!selection || selection->entries.size() == 1))
		{
			renameClickCandidate = id.value;
			renameClickReleasedAt = -1.0;
			renameClickDragged = false;
		}
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
	if (renameClickCandidate == id.value)
	{
		renameClickDragged |= ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f);
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (rowHovered && !renameClickDragged)
			{
				renameClickReleasedAt = ImGui::GetTime();
			}
			else
			{
				renameClickCandidate = {};
				renameClickReleasedAt = -1.0;
			}
		}
	}
	if (rowLabelDoubleClicked && !sceneControlHovered)
	{
		renameClickCandidate = {};
		renameClickReleasedAt = -1.0;
		focusSelectionRequested = true;
	}
	if (!sceneControlHovered && ImGui::BeginDragDropSource())
	{
		const auto drag =
		    selection ? BeginHierarchyDragSession(*selection, entry, visibleRows) : BeginEditorDragSession({entry}, 0);
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
	bool changed = inlineRenameChanged;
	const auto targetPrefabInstance = prefabInstanceByObject.find(key);
	const bool allowHierarchyDrop =
	    !playSession && (editableScene || targetPrefabInstance != prefabInstanceByObject.end());
	const auto dropZone = DrawHierarchyRowDropFeedback(
	    rowRect, documentId, playSession, parentOf, allowHierarchyDrop,
	    [this, &scene, &visibleRows, &parentOf, rowRect, documentId, targetId = id.value, &changed, editableScene,
	     targetInstance = targetPrefabInstance == prefabInstanceByObject.end()
	                          ? String{}
	                          : targetPrefabInstance->second](const EditorDragSession& drag, HierarchyDropZone zone)
	    {
		    const auto [dropTargetId, dropZone] = ResolveHierarchyDropAtCursor(
		        visibleRows, documentId, false, parentOf, drag, targetId, zone, ImGui::GetIO().MousePos.x,
		        rowRect.Min.x + ImGui::GetTreeNodeToLabelSpacing(), ImGui::GetStyle().IndentSpacing);
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
		                      ? assetDropAction(scene, drag, dropTargetId, dropZone, documentId)
		                      : ApplyHierarchyDrop(scene, drag, dropTargetId, dropZone, documentId);
		    changed |= static_cast<bool>(result);
		    commandDiagnostic = result ? String{} : result.error;
	    },
	    !hierarchyAlphabeticalSort);
	if (dropZone == HierarchyDropZone::Into && hasChildren && hoveringIndentedContent &&
	    ImGui::GetDragDropPayload())
	{
		if (dragHoverTarget != id.value || mousePosition.x != dragHoverPosition.x ||
		    mousePosition.y != dragHoverPosition.y)
		{
			dragHoverTarget = id.value;
			dragHoverStartedAt = ImGui::GetTime();
			dragHoverPosition = mousePosition;
		}
	}
	else if (dragHoverTarget == id.value)
	{
		dragHoverTarget = {};
	}
	if (rowRightClicked && !sceneControlHovered)
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
			                   if (renameNewObjects)
			                   {
				                   renameTarget = identity.value;
				                   std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
				                   std::copy_n("GameObject", 10, renameBuffer.data());
				                   renameFocusPending = true;
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
			                   renameFocusPending = true;
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
		commands.Register({"hierarchy.select-children",
		                   "Select Children",
		                   {},
		                   [editableScene, documentId, playSession, &scene](const EditorCommandContext& context)
		                   {
			                   if (!editableScene || context.selection.entries.empty())
			                   {
				                   return Result<void>::Failure("Select editable GameObjects with descendants");
			                   }
			                   bool hasDescendants = false;
			                   std::function<void(const GameObject::Ptr&)> findDescendants =
			                       [&](const GameObject::Ptr& object)
			                   {
				                   for (const auto& child : object->Children())
				                   {
					                   hasDescendants = true;
					                   findDescendants(child);
				                   }
			                   };
			                   for (const auto& item : context.selection.entries)
			                   {
				                   if (item.kind != EditorSelectionKind::GameObject || item.documentId != documentId ||
				                       item.playSession != playSession || item.IsMissing())
				                   {
					                   return Result<void>::Failure("Select GameObjects in the active Hierarchy first");
				                   }
				                   auto object = scene.GameObjectById(item.objectId);
				                   if (!object)
				                   {
					                   return Result<void>::Failure(object.error);
				                   }
				                   findDescendants(object.value);
			                   }
			                   return hasDescendants ? Result<void>{}
			                                         : Result<void>::Failure("The selection has no child GameObjects");
		                   },
		                   [this, &scene, selection, documentId, playSession](const EditorCommandContext&)
		                   {
			                   if (!selection)
			                   {
				                   return Result<void>::Failure("Hierarchy selection is unavailable");
			                   }
			                   auto selectedChildren = SelectChildren(scene, *selection, documentId, playSession);
			                   if (selectedChildren)
			                   {
				                   SetLegacySelected(selection, selected);
			                   }
			                   return selectedChildren;
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
		if (Icons::MenuItem("Copy", "Ctrl+C", false, canCopySelection && static_cast<bool>(copySelectionAction)))
		{
			auto copied = copySelectionAction();
			commandDiagnostic = copied ? String{} : copied.error;
		}
		if (Icons::MenuItem("Cut", "Ctrl+X", false, canCopySelection && static_cast<bool>(cutSelectionAction)))
		{
			auto cut = cutSelectionAction();
			commandDiagnostic = cut ? String{} : cut.error;
			changed |= static_cast<bool>(cut);
		}
		if (Icons::MenuItem("Paste as Child", "Ctrl+Shift+V", false,
		                    canPasteAsChild && static_cast<bool>(pasteAsChildAction)))
		{
			auto pasted = pasteAsChildAction();
			commandDiagnostic = pasted ? String{} : pasted.error;
			changed |= static_cast<bool>(pasted);
		}
		if (Icons::MenuItem("Select All", nullptr, false, selection && !visibleRows.empty()))
		{
			if (selection->SelectEntries(visibleRows))
			{
				SetLegacySelected(selection, selected);
			}
		}
		if (Icons::MenuItem("Deselect All", nullptr, false, selection && !selection->entries.empty()))
		{
			if (selection->SelectEntries({}))
			{
				SetLegacySelected(selection, selected);
				ClearSelection();
			}
		}
		if (Icons::MenuItem("Invert Selection", nullptr, false, selection && !selection->entries.empty()))
		{
			Vector<EditorSelectionEntry> inverted;
			for (const auto& visibleRow : visibleRows)
			{
				if (!selection->Contains(visibleRow))
				{
					inverted.push_back(visibleRow);
				}
			}
			if (selection->SelectEntries(std::move(inverted)))
			{
				SetLegacySelected(selection, selected);
			}
		}
		if (editableScene)
		{
			const ResourceId currentDefaultParent = DefaultParent(scene, documentId);
			if (currentDefaultParent == id.value)
			{
				if (Icons::MenuItem("Clear Default Parent"))
				{
					ClearDefaultParent();
				}
			}
			else if (Icons::MenuItem("Set as Default Parent"))
			{
				SetDefaultParent(documentId, id.value);
			}
		}
		const auto canDuplicate = contextMenuTarget
		                              ? commands.CanExecute("hierarchy.duplicate", *contextMenuTarget, current)
		                              : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(canDuplicate)))
		{
			auto duplicated = commands.Execute("hierarchy.duplicate", *contextMenuTarget, current);
			changed = static_cast<bool>(duplicated);
		}
		const auto canSelectChildren = contextMenuTarget
		                                  ? commands.CanExecute("hierarchy.select-children", *contextMenuTarget, current)
		                                  : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Select Children", nullptr, false, static_cast<bool>(canSelectChildren)))
		{
			auto selectedChildren = commands.Execute("hierarchy.select-children", *contextMenuTarget, current);
			commandDiagnostic = selectedChildren ? String{} : selectedChildren.error;
		}
		commands.Register({"hierarchy.create-empty-parent",
		                   "Create Empty Parent",
		                   "Ctrl+Shift+G",
		                   [editableScene, documentId](const EditorCommandContext& context)
		                   {
			                   return editableScene && !context.selection.entries.empty() &&
				                              std::all_of(context.selection.entries.begin(), context.selection.entries.end(),
				                                          [documentId](const EditorSelectionEntry& item)
				                                          {
					                                          return item.kind == EditorSelectionKind::GameObject &&
					                                                 item.documentId == documentId && !item.playSession &&
					                                                 !item.IsMissing();
				                                          })
			                   ? Result<void>{}
			                   : Result<void>::Failure("Select editable GameObjects to create a parent");
		                   },
		                   [&scene, selection, documentId, this](const EditorCommandContext& context)
		                   {
			                   auto created = CreateEmptyParent(scene, context.selection.entries, documentId);
			                   if (!created)
			                   {
				                   return Result<void>::Failure(created.error);
			                   }
			                   if (selection)
			                   {
				                   selection->SelectGameObject(documentId, created.value);
				                   SetLegacySelected(selection, selected);
			                   }
			                   else
			                   {
				                   selected = created.value;
			                   }
			                   if (renameNewObjects)
			                   {
				                   renameTarget = created.value;
				                   std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
				                   std::copy_n("GameObject", 10, renameBuffer.data());
				                   renameFocusPending = true;
			                   }
			                   return Result<void>{};
		                   }});
		const auto canCreateParent = contextMenuTarget
		                                 ? commands.CanExecute("hierarchy.create-empty-parent", *contextMenuTarget, current)
		                                 : Result<void>::Failure("The hierarchy menu target is unavailable");
		if (Icons::MenuItem("Create Empty Parent", "Ctrl+Shift+G", false, static_cast<bool>(canCreateParent)))
		{
			auto created = commands.Execute("hierarchy.create-empty-parent", *contextMenuTarget, current);
			changed = static_cast<bool>(created);
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
	if (open && hasVisibleChildren)
	{
		for (GameObject::Ptr child : orderedChildren)
		{
			auto drawn = DrawGameObject(scene, child, visibleRows, selection, documentId, playSession, documentRevision,
			                            editableScene, searchMatches, filtering, expandFilteredBranches);
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
	if (recursivelyToggleDescendants)
	{
		SetHierarchyDescendantsOpen(node, "##node", open,
		                            [&scene](const GameObject::Ptr& child)
		                            {
			                            const auto childId = scene.PersistentId(child);
			                            return childId ? childId.value.ToString() : child->Id().ToString();
		                            });
	}
	return Result<bool>::Success(changed);
}

Result<bool> EditorHierarchy::Draw(Scene& scene, bool* open, const LinkedSceneResource* linked,
                                   EditorSelectionContext* selection, ResourceId documentId, bool playSession,
                                   UInt64 documentRevision, DuplicateObjectsAction duplicateObjects,
                                   DeleteObjectsAction deleteObjects, AssetDropAction assetDrop, String sceneName,
                                   VirtualFileSystem* userFiles, bool copySelectionAvailable,
                                   bool pasteAsChildAvailable, CopySelectionAction copyAction,
                                   CutSelectionAction cutAction, PasteAsChildAction pasteAction)
{
	if (open && !*open)
	{
		return Result<bool>::Success(false);
	}
	if (!hierarchySettingsLoaded && userFiles)
	{
		hierarchySettingsLoaded = true;
		auto path = VirtualPath::Parse("user:Settings/Hierarchy.json");
		if (path)
		{
			auto text = userFiles->ReadText(path.value);
			if (text)
			{
				try
				{
					const auto json = nlohmann::json::parse(ToStdString(text.value));
					if (json.value("version", 0) == 1)
					{
						renameNewObjects = json.value("renameNewObjects", true);
						hierarchyAlphabeticalSort = json.value("alphabeticalSort", false);
					}
				}
				catch (const std::exception&)
				{
					commandDiagnostic = "Cannot parse Hierarchy settings";
				}
			}
		}
	}
	duplicateObjectsAction = std::move(duplicateObjects);
	deleteObjectsAction = std::move(deleteObjects);
	assetDropAction = std::move(assetDrop);
	canCopySelection = copySelectionAvailable;
	canPasteAsChild = pasteAsChildAvailable;
	copySelectionAction = std::move(copyAction);
	cutSelectionAction = std::move(cutAction);
	pasteAsChildAction = std::move(pasteAction);
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
	const bool editableScene = linked == nullptr && !playSession;
	bool createRootRequested = false;
	bool createChildRequested = false;
	if (Icons::Button("CreateRoot", Icons::Icon::Add, "Create"))
	{
		ImGui::OpenPopup("HierarchyCreateRoot");
	}
	if (ImGui::BeginPopup("HierarchyCreateRoot"))
	{
		createRootRequested = Icons::MenuItem("Create Empty", "Ctrl+Shift+N", false, editableScene);
		const auto* primary = selection ? selection->PrimaryEntry() : nullptr;
		const bool canCreateChild = editableScene && primary && primary->kind == EditorSelectionKind::GameObject &&
		                            primary->documentId == documentId && primary->playSession == playSession &&
		                            !primary->IsMissing();
		createChildRequested = Icons::MenuItem("Create Empty Child", "Alt+Shift+N", false, canCreateChild);
		ImGui::EndPopup();
	}
	Icons::SameLine();
	const bool searchFieldActive = DrawHierarchySearchField(searchText, 34.f);
	Icons::SameLine();
	if (Icons::SmallButton("..."))
	{
		ImGui::OpenPopup("HierarchyOptions");
	}
	if (ImGui::BeginPopup("HierarchyOptions"))
	{
		bool settingsChanged = ImGui::Checkbox("Rename New Objects", &renameNewObjects);
		settingsChanged = ImGui::Checkbox("Alphabetical Sorting", &hierarchyAlphabeticalSort) || settingsChanged;
		if (settingsChanged && userFiles)
		{
			auto path = VirtualPath::Parse("user:Settings/Hierarchy.json");
			if (path)
			{
				const String text = ToUrsaString(nlohmann::json{{"version", 1},
				                                               {"renameNewObjects", renameNewObjects},
				                                               {"alphabeticalSort", hierarchyAlphabeticalSort}}
				                                    .dump(2));
				Vector<UInt8> bytes(text.begin(), text.end());
				auto written = userFiles->WriteAtomic(path.value, bytes);
				commandDiagnostic = written ? String{} : written.error;
			}
		}
		ImGui::EndPopup();
	}
	const String currentSearch(searchText.data());
	const bool typeOnly = currentSearch.size() > 2 &&
	                      (currentSearch.compare(0, 2, "t:") == 0 || currentSearch.compare(0, 2, "T:") == 0);
	const String filterText = typeOnly ? currentSearch.substr(2)
	                                   : (currentSearch == "t:" || currentSearch == "T:" ? String{} : currentSearch);
	ImGuiTextFilter hierarchyFilter(filterText.c_str());
	const bool filtering = !filterText.empty();
	const bool expandFilteredBranches =
	    filtering && (currentSearch != previousSearchText || documentId != previousSearchDocument);
	previousSearchText = currentSearch;
	previousSearchDocument = documentId;
	Map<String, bool> searchMatches;
	Map<String, bool> directMatches;
	if (filtering)
	{
		for (const auto& root : scene.RootGameObjects())
		{
			BuildSceneSearchMatches(scene, root, hierarchyFilter, typeOnly, searchMatches, directMatches);
		}
	}
	directSearchMatches = std::move(directMatches);
	directSearchQuery = currentSearch;
	directSearchDocument = documentId;
	const String groupKey = documentId.ToString();
	if (sceneName.empty())
	{
		sceneName = "Scene";
	}
	const Size extension = sceneName.rfind('.');
	if (extension != String::npos)
	{
		sceneName.resize(extension);
	}
	auto groupState = sceneGroupOpenByDocument.emplace(groupKey, true).first;
	if (filtering)
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}
	ImGui::PushID(groupKey.c_str());
	const bool sceneRootsOpen = Icons::TreeNode("##sceneGroup", ImGuiTreeNodeFlags_SpanAvailWidth |
	                                                        ImGuiTreeNodeFlags_OpenOnArrow |
	                                                        ImGuiTreeNodeFlags_OpenOnDoubleClick,
	                                           Icons::Icon::Scene, sceneName.c_str());
	ImGui::PopID();
	if (ImGui::IsItemToggledOpen())
	{
		groupState->second = sceneRootsOpen;
	}
	const bool showSceneRoots = filtering || groupState->second;
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
	if (renameClickCandidate && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
	    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
	{
		renameClickCandidate = {};
		renameClickReleasedAt = -1.0;
	}
	Vector<EditorSelectionEntry> visibleRows;
	if (showSceneRoots)
	{
		for (const auto& root : OrderedGameObjects(scene.RootGameObjects(), hierarchyAlphabeticalSort))
		{
			AppendVisibleRows(scene, root, documentId, visibleRows, "##node", playSession, searchMatches, filtering,
			                  expandFilteredBranches, hierarchyAlphabeticalSort);
		}
	}
	visibleSelectionRows = visibleRows;
	const ImGuiID focusedHierarchyItem = ImGui::GetFocusID();
	const bool hierarchyRowFocused = focusedHierarchyItem != 0 &&
	                                 std::find(hierarchyRowIds.begin(), hierarchyRowIds.end(), focusedHierarchyItem) !=
	                                     hierarchyRowIds.end();
	hierarchyRowIds.clear();
	if (selection && hierarchyRowFocused && !visibleRows.empty() &&
	    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && selection->SelectEntries(visibleRows))
	{
		SetLegacySelected(selection, selected);
	}
	bool changed = false;
	auto beginRename = [this](ResourceId id)
	{
		if (!renameNewObjects)
		{
			return;
		}
		renameTarget = id;
		std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
		std::copy_n("GameObject", 10, renameBuffer.data());
		renameFocusPending = true;
	};
	auto createEmptyRoot = [&]() -> Result<void>
	{
		if (!editableScene)
		{
			return Result<void>::Failure("The current hierarchy is read-only");
		}
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
		beginRename(identity.value);
		searchText.fill(0);
		return {};
	};
	auto createEmptyChild = [&]() -> Result<void>
	{
		if (!editableScene || !selection)
		{
			return Result<void>::Failure("Create Empty Child requires an editable Scene selection");
		}
		const auto* primary = selection->PrimaryEntry();
		if (!primary || primary->kind != EditorSelectionKind::GameObject || primary->documentId != documentId ||
		    primary->playSession != playSession || primary->IsMissing())
		{
			return Result<void>::Failure("Select a GameObject to create an empty child");
		}
		auto parent = scene.GameObjectById(primary->objectId);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
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
		selection->SelectGameObject(documentId, identity.value);
		SetLegacySelected(selection, selected);
		beginRename(identity.value);
		searchText.fill(0);
		return {};
	};
	const ImGuiIO& hierarchyInput = ImGui::GetIO();
	const bool searchNavigationKey = searchFieldActive &&
	                                 (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
	                                  ImGui::IsKeyPressed(ImGuiKey_DownArrow, false));
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    (!hierarchyInput.WantTextInput || searchNavigationKey) && (hierarchyRowFocused || searchNavigationKey))
	{
		const bool toggleVisibility = !searchNavigationKey && !hierarchyInput.KeyCtrl && !hierarchyInput.KeyAlt &&
		                              !hierarchyInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_H, false);
		const bool togglePickability = !searchNavigationKey && !hierarchyInput.KeyCtrl && !hierarchyInput.KeyAlt &&
		                               !hierarchyInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false);
		if (searchNavigationKey)
		{
			EnsureHierarchySelectionVisible(visibleRows, selection, documentId, selected, keyboardFocusTarget);
		}
		else if ((toggleVisibility || togglePickability) && !playSession)
		{
			Vector<GameObject::Ptr> selectedObjects;
			bool validSelection = selection && !selection->entries.empty();
			if (validSelection)
			{
				for (const EditorSelectionEntry& entry : selection->entries)
				{
					if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != documentId ||
					    entry.playSession || entry.IsMissing())
					{
						validSelection = false;
						break;
					}
					auto object = scene.GameObjectById(entry.objectId);
					if (!object)
					{
						validSelection = false;
						break;
					}
					selectedObjects.push_back(object.value);
				}
			}
			else if (!selection && selected)
			{
				auto object = scene.GameObjectById(selected);
				if (object)
				{
					selectedObjects.push_back(object.value);
					validSelection = true;
				}
			}
			if (validSelection && !selectedObjects.empty())
			{
				if (toggleVisibility)
				{
					ToggleSceneObjectsVisibility(scene, selectedObjects);
				}
				else
				{
					ToggleSceneObjectsPickability(scene, selectedObjects);
				}
				changed = true;
			}
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
		{
			NavigateHierarchySelection(visibleRows, selection, selected, 1, hierarchyInput.KeyShift,
			                           keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
		{
			NavigateHierarchySelection(visibleRows, selection, selected, -1, hierarchyInput.KeyShift,
			                           keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Home, false) || ImGui::IsKeyPressed(ImGuiKey_End, false))
		{
			const bool last = ImGui::IsKeyPressed(ImGuiKey_End, false);
			NavigateHierarchySelectionToEdge(visibleRows, selection, selected, last, hierarchyInput.KeyShift,
			                                 keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
		{
			const Int32 pageStep = static_cast<Int32>(
			    std::max(1.f, std::floor(ImGui::GetContentRegionAvail().y / ImGui::GetFrameHeight())));
			const Int32 offset = ImGui::IsKeyPressed(ImGuiKey_PageDown, false) ? pageStep : -pageStep;
			NavigateHierarchySelectionByOffset(visibleRows, selection, selected, offset, hierarchyInput.KeyShift,
			                                   keyboardFocusTarget);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
		{
			const bool expand = ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
			const auto* primary = selection ? selection->PrimaryEntry() : nullptr;
			const ResourceId activeId = primary && primary->kind == EditorSelectionKind::GameObject &&
			                                    primary->documentId == documentId && primary->playSession == playSession
			                                ? primary->objectId
			                                : selected;
			auto object = scene.GameObjectById(activeId);
			if (object)
			{
				auto persistentId = scene.PersistentId(object.value);
				auto children = scene.ChildrenOf(object.value);
				if (persistentId && children)
				{
					const String key = persistentId.value.ToString();
					const bool nodeOpen = IsHierarchyNodeOpen(key, "##node");
					if (expand && !children.value.empty())
					{
						if (!nodeOpen && !filtering)
						{
							SetHierarchyNodeOpen(key, "##node", true);
						}
						else
						{
							NavigateHierarchySelection(visibleRows, selection, selected, 1, false, keyboardFocusTarget);
						}
					}
					else if (!expand)
					{
						if (!children.value.empty() && nodeOpen && !filtering)
						{
							SetHierarchyNodeOpen(key, "##node", false);
						}
						else
						{
							auto parent = scene.ParentOf(object.value);
							auto parentId = parent && parent.value
							                    ? scene.PersistentId(parent.value)
							                    : Result<ResourceId>::Failure("GameObject has no parent");
							if (parentId)
							{
								const auto parentEntry = GameObjectSelection(documentId, parentId.value, playSession);
								SelectHierarchyEntry(parentEntry, visibleRows, selection, selected, false,
								                     keyboardFocusTarget);
							}
						}
					}
				}
			}
		}
	}
	if (createRootRequested)
	{
		auto created = createEmptyRoot();
		changed |= static_cast<bool>(created);
		commandDiagnostic = created ? String{} : created.error;
	}
	else if (createChildRequested)
	{
		auto created = createEmptyChild();
		changed |= static_cast<bool>(created);
		commandDiagnostic = created ? String{} : created.error;
	}
	if (showSceneRoots)
	{
		for (GameObject::Ptr root : OrderedGameObjects(scene.RootGameObjects(), hierarchyAlphabeticalSort))
		{
			auto drawn =
			    DrawGameObject(scene, root, visibleRows, selection, documentId, playSession, documentRevision,
			                   editableScene, searchMatches, filtering && !createRootRequested && !createChildRequested,
			                   expandFilteredBranches);
			if (!drawn)
			{
				ImGui::End();
				return drawn;
			}
			changed |= drawn.value;
		}
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
	if (selection && !selection->entries.empty() && !pointerOverHierarchyRow && !ImGui::IsAnyItemHovered() &&
	    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
	    ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetDragDropPayload())
	{
		if (selection->SelectEntries({}))
		{
			SetLegacySelected(selection, selected);
			ClearSelection();
		}
	}
	if (!commandDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", commandDiagnostic.c_str());
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && hierarchyRowFocused &&
	    !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_F, false) && selection && selection->PrimaryEntry() &&
	    selection->PrimaryEntry()->kind == EditorSelectionKind::GameObject &&
	    selection->PrimaryEntry()->documentId == documentId &&
	    selection->PrimaryEntry()->playSession == playSession && !selection->PrimaryEntry()->IsMissing())
	{
		focusSelectionRequested = true;
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && hierarchyRowFocused &&
	    !ImGui::GetIO().WantTextInput &&
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
					renameFocusPending = true;
				}
			}
		}
	}
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
	    ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false) &&
	    selection && !selection->entries.empty() && editableScene)
	{
		auto created = CreateEmptyParent(scene, selection->entries, documentId);
		commandDiagnostic = created ? String{} : created.error;
		if (created)
		{
			selection->SelectGameObject(documentId, created.value);
			SetLegacySelected(selection, selected);
			changed = true;
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
		     [&createEmptyRoot](const EditorCommandContext&) { return createEmptyRoot(); }});
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
	if (renameClickCandidate && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		renameClickCandidate = {};
		renameClickReleasedAt = -1.0;
	}
	if (renameClickCandidate && renameClickReleasedAt >= 0.0 && ImGui::GetTime() - renameClickReleasedAt >= 0.5)
	{
		const bool stillSelected = selection ? selection->entries.size() == 1 && selection->PrimaryEntry() &&
		                                           selection->PrimaryEntry()->kind == EditorSelectionKind::GameObject &&
		                                           selection->PrimaryEntry()->documentId == documentId &&
		                                           selection->PrimaryEntry()->objectId == renameClickCandidate &&
		                                           !selection->PrimaryEntry()->IsMissing()
		                                     : selected == renameClickCandidate;
		auto target = scene.GameObjectById(renameClickCandidate);
		if (stillSelected && target)
		{
			auto name = target.value->Name();
			if (name)
			{
				renameTarget = renameClickCandidate;
				std::fill(renameBuffer.begin(), renameBuffer.end(), '\0');
				std::copy_n(name.value.data(), std::min(name.value.size(), renameBuffer.size() - 1),
				            renameBuffer.data());
				renameFocusPending = true;
			}
		}
		renameClickCandidate = {};
		renameClickReleasedAt = -1.0;
	}
	if (renameTarget && !scene.GameObjectById(renameTarget))
	{
		renameTarget = {};
		renameFocusPending = false;
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
