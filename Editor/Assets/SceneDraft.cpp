#include <Core/Types.hpp>
#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
namespace
{
bool DrawAddComponentBrowser(ComponentEditors& componentEditors, ComponentInspectorContext& context, String& diagnostic)
{
	if (ImGui::Button("Add Component"))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}
	static ImGuiTextFilter filter;
	ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_Appearing);
	bool changed = false;
	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		filter.Draw("Search");
		auto descriptors = componentEditors.Types();
		std::sort(descriptors.begin(), descriptors.end(),
		          [](const ComponentTypeDescriptor& left, const ComponentTypeDescriptor& right)
		          {
			          return left.category == right.category ? left.displayName < right.displayName
			                                                 : left.category < right.category;
		          });
		String currentCategory;
		for (const ComponentTypeDescriptor& descriptor : descriptors)
		{
			if (!filter.PassFilter(descriptor.displayName.c_str()) && !filter.PassFilter(descriptor.category.c_str()))
			{
				continue;
			}
			if (descriptor.category != currentCategory)
			{
				currentCategory = descriptor.category;
				ImGui::SeparatorText(currentCategory.c_str());
			}
			auto available = componentEditors.CanCreate(descriptor.type, context);
			ImGui::BeginDisabled(!available);
			if (ImGui::Selectable(descriptor.displayName.c_str()))
			{
				auto created = componentEditors.Create(descriptor.type, context);
				diagnostic = created ? String{} : created.error;
				changed = static_cast<bool>(created);
				if (created)
				{
					ImGui::CloseCurrentPopup();
				}
			}
			if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("%s", available.error.c_str());
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}
	return changed;
}
} // namespace

Result<std::shared_ptr<SceneDraft>> SceneDraft::Create(ResourceKind resourceKind, ComponentRegistry& componentRegistry,
                                                       ResourceSession& resourceSession,
                                                       const EditContextSetup& contextSetup)
{
	if (resourceKind != ResourceKind::Scene && resourceKind != ResourceKind::Prefab)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure("Scene draft kind must be Scene or Prefab");
	}
	auto draft =
	    std::shared_ptr<SceneDraft>(new SceneDraft(resourceKind, componentRegistry, resourceSession, contextSetup));
	if (resourceKind == ResourceKind::Prefab)
	{
		draft->prefabAsset = std::make_unique<PrefabAsset>(ResourceId::Generate(), GameObject::Create("Prefab"));
		auto refreshed = draft->RefreshPrefabPreview();
		if (!refreshed)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(refreshed.error);
		}
	}
	else
	{
		draft->scene = std::make_unique<Scene>();
		auto root = draft->scene->CreateGameObject("Root");
		if (!root)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(root.error);
		}
	}
	return Result<std::shared_ptr<SceneDraft>>::Success(std::move(draft));
}

Result<std::shared_ptr<SceneDraft>> SceneDraft::Load(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                     ResourceKind resourceKind, ComponentRegistry& componentRegistry,
                                                     ResourceSession& resourceSession,
                                                     const EditContextSetup& contextSetup)
{
	if (resourceKind != ResourceKind::Scene && resourceKind != ResourceKind::Prefab)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure("Scene draft kind must be Scene or Prefab");
	}
	ComponentLoadContext context(resourceSession);
	if (contextSetup)
	{
		auto configured = contextSetup(context);
		if (!configured)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(configured.error);
		}
	}
	auto draft =
	    std::shared_ptr<SceneDraft>(new SceneDraft(resourceKind, componentRegistry, resourceSession, contextSetup));
	if (resourceKind == ResourceKind::Prefab)
	{
		auto asset = LoadPrefabAssetResource(filesystem, path, componentRegistry, context);
		if (!asset)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(asset.error);
		}
		draft->prefabAsset = std::make_unique<PrefabAsset>(std::move(asset.value));
		auto refreshed = draft->RefreshPrefabPreview();
		if (!refreshed)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(refreshed.error);
		}
	}
	else
	{
		auto json = ReadJsonResource(filesystem, path, ResourceKind::Scene);
		if (!json)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(json.error);
		}
		if (json.value.contains("instances"))
		{
			auto loaded = LoadLinkedSceneResource(filesystem, path, componentRegistry, context);
			if (!loaded)
			{
				return Result<std::shared_ptr<SceneDraft>>::Failure(loaded.error);
			}
			draft->linked = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
		}
		else
		{
			auto loaded = LoadSceneResource(filesystem, path, componentRegistry, context);
			if (!loaded)
			{
				return Result<std::shared_ptr<SceneDraft>>::Failure(loaded.error);
			}
			draft->scene = std::move(loaded.value.scene);
		}
	}
	return Result<std::shared_ptr<SceneDraft>>::Success(std::move(draft));
}

Result<ResourceId> SceneDraft::MaterialId(const std::shared_ptr<MaterialInstance>& material) const
{
	if (!material)
	{
		return Result<ResourceId>::Failure("No material assigned");
	}
	auto assigned = assignedMaterials.find(material.get());
	if (assigned != assignedMaterials.end())
	{
		return Result<ResourceId>::Success(assigned->second);
	}
	return resources.IdentifyMaterial(material);
}

Result<void> SceneDraft::Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id)
{
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	ComponentLoadContext validation(resources);
	if (setup)
	{
		auto prepared = setup(validation);
		if (!prepared)
		{
			return prepared;
		}
	}
	if (kind == ResourceKind::Prefab)
	{
		if (!prefabAsset || !prefabAsset->Root())
		{
			return Result<void>::Failure("Prefab document has no detached asset graph");
		}
		PrefabAsset resource(id, prefabAsset->Root());
		auto saved = SavePrefabAssetResource(filesystem, path, resource, registry, resolver);
		if (!saved)
		{
			return saved;
		}
		auto loaded = LoadPrefabAssetResource(filesystem, path, registry, validation);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	if (linked)
	{
		auto saved = SaveLinkedSceneResource(filesystem, path, *linked, registry, resolver);
		if (!saved)
		{
			return saved;
		}
		auto loaded = LoadLinkedSceneResource(filesystem, path, registry, validation);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	auto saved = SaveSceneResource(filesystem, path, id, GetScene(), registry, resolver);
	if (!saved)
	{
		return saved;
	}
	auto loaded = LoadSceneResource(filesystem, path, registry, validation);
	return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
}

Result<void> SceneDraft::RefreshPrefabPreview()
{
	if (kind != ResourceKind::Prefab || !prefabAsset || !prefabAsset->Root())
	{
		return Result<void>::Failure("Only a PrefabAsset draft can refresh its preview Scene");
	}
	auto preview = std::make_unique<Scene>();
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto prepared = setup(context);
		if (!prepared)
		{
			return prepared;
		}
	}
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	auto instantiated = prefabAsset->Instantiate(*preview, registry, context, resolver);
	if (!instantiated)
	{
		return Result<void>::Failure(instantiated.error);
	}
	previewObjects.clear();
	std::function<Result<void>(const GameObject::Ptr&)> mapPreview = [&](const GameObject::Ptr& source) -> Result<void>
	{
		auto previewObject = context.GameObjectById(source->Id());
		if (!previewObject)
		{
			return Result<void>::Failure(previewObject.error);
		}
		previewObjects[source->Id().ToString()] = previewObject.value;
		for (const auto& child : source->Children())
		{
			auto result = mapPreview(child);
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	auto mapped = mapPreview(prefabAsset->Root());
	if (!mapped)
	{
		return mapped;
	}
	scene = std::move(preview);
	return {};
}

Result<GameObject::Ptr> SceneDraft::PreviewGameObject(ResourceId prefabObjectId) const
{
	const auto found = previewObjects.find(prefabObjectId.ToString());
	return found == previewObjects.end()
	           ? Result<GameObject::Ptr>::Failure("Prefab preview has no mapping for GameObject " +
	                                              prefabObjectId.ToString())
	           : Result<GameObject::Ptr>::Success(found->second);
}

Result<void> SceneDraft::SyncPrefabTransform(ResourceId prefabObjectId)
{
	if (!prefabAsset || !prefabAsset->Root())
	{
		return Result<void>::Failure("Prefab draft has no detached asset graph");
	}
	auto previewObject = PreviewGameObject(prefabObjectId);
	if (!previewObject)
	{
		return Result<void>::Failure(previewObject.error);
	}
	std::function<GameObject::Ptr(const GameObject::Ptr&)> find = [&](const GameObject::Ptr& object)
	{
		if (object->Id() == prefabObjectId)
		{
			return object;
		}
		for (const auto& child : object->Children())
		{
			auto found = find(child);
			if (found)
			{
				return found;
			}
		}
		return GameObject::Ptr{};
	};
	auto assetObject = find(prefabAsset->Root());
	if (!assetObject)
	{
		return Result<void>::Failure("Prefab asset has no GameObject " + prefabObjectId.ToString());
	}
	auto local = previewObject.value->GetLocalTransform();
	return local ? assetObject->SetLocalTransform(local.value) : Result<void>::Failure(local.error);
}

Result<void> SceneDraft::AssignMaterial(GameObject::Ptr node, ResourceId materialId, ResourceSession& resourceSession)
{
	if (kind == ResourceKind::Prefab)
	{
		auto* box = node ? node->FindComponent<BoxRenderComponent>() : nullptr;
		if (!box)
		{
			return Result<void>::Failure("Selected Prefab GameObject has no box renderer component");
		}
		if (!materialId)
		{
			box->material.reset();
			return {};
		}
		auto material = resourceSession.LoadMaterial(materialId);
		if (!material)
		{
			return Result<void>::Failure(material.error);
		}
		box->material = material.value;
		assignedMaterials[material.value.get()] = materialId;
		return {};
	}
	auto* box = GetScene().FindComponent<BoxRenderComponent>(node);
	if (!box)
	{
		return Result<void>::Failure("Selected node has no box renderer component");
	}
	auto components = GetScene().ComponentsOf(node);
	if (!components)
	{
		return Result<void>::Failure(components.error);
	}
	for (ComponentId id : components.value)
	{
		if (GetScene().GetComponent(id) == box)
		{
			return AssignMaterial(id, materialId, resourceSession);
		}
	}
	return Result<void>::Failure("Box renderer component is missing");
}

Result<void> SceneDraft::AssignMaterial(ComponentId component, ResourceId materialId, ResourceSession& resourceSession)
{
	auto* box = dynamic_cast<BoxRenderComponent*>(GetScene().GetComponent(component));
	if (!box)
	{
		return Result<void>::Failure("Selected component is not a box renderer");
	}
	if (!materialId)
	{
		box->material.reset();
		return {};
	}
	auto material = resourceSession.LoadMaterial(materialId);
	if (!material)
	{
		return Result<void>::Failure(material.error);
	}
	box->material = material.value;
	assignedMaterials[material.value.get()] = materialId;
	return {};
}

Result<void> SceneDraft::SetInstanceProperty(ResourceId instanceId, ResourceId sourceObjectId, const String& pointer,
                                             const nlohmann::json& value)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	return SetPrefabPropertyOverride(*linked, instanceId, sourceObjectId, pointer, value);
}

Result<void> SceneDraft::SetInstanceStructure(ResourceId instanceId, const String& operation,
                                              const nlohmann::json& value)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	return SetPrefabStructureOverride(*linked, instanceId, operation, value);
}

Result<ResourceId> SceneDraft::PlacePrefab(ResourceId prefabId, ResourceId parentId, const VirtualPath& scenePath,
                                           ResourceId sceneId)
{
	if (kind != ResourceKind::Scene)
	{
		return Result<ResourceId>::Failure("Prefab instances can only be placed in scenes");
	}
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	nlohmann::json document;
	if (linked)
	{
		const auto previousDocument = linked->document;
		const auto previousSnapshot = linked->snapshot;
		const auto previousScopes = linked->scopedIds;
		auto captured = CaptureLinkedSceneChanges(scenePath.ToString(), *linked, registry, resolver);
		if (!captured)
		{
			return Result<ResourceId>::Failure(captured.error);
		}
		document = linked->document;
		linked->document = previousDocument;
		linked->snapshot = previousSnapshot;
		linked->scopedIds = previousScopes;
	}
	else
	{
		auto encoded = EncodeSceneDocument(scenePath.ToString(), sceneId, *scene, registry, resolver);
		if (!encoded)
		{
			return Result<ResourceId>::Failure(encoded.error);
		}
		document = std::move(encoded.value);
	}
	const ResourceId instanceId = ResourceId::Generate();
	auto placed = AddPrefabInstance(document, prefabId, instanceId, parentId);
	if (!placed)
	{
		return Result<ResourceId>::Failure(placed.error);
	}
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto prepared = setup(context);
		if (!prepared)
		{
			return Result<ResourceId>::Failure(prepared.error);
		}
	}
	auto loaded = DecodeLinkedSceneDocument(scenePath.ToString(), std::move(document), registry, context);
	if (!loaded)
	{
		return Result<ResourceId>::Failure(loaded.error);
	}
	linked = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
	scene.reset();
	return Result<ResourceId>::Success(instanceId);
}

Result<std::unique_ptr<EditorDocument>> NewSceneDocument(const VirtualPath& path, ResourceKind kind,
                                                         ComponentRegistry& registry, ResourceSession& resources,
                                                         const EditContextSetup& setup)
{
	auto draft = SceneDraft::Create(kind, registry, resources, setup);
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document =
	    EditorDocument::Create(kind, ResourceId::Generate(), path,
	                           [draft = draft.value](VirtualFileSystem& filesystem, const VirtualPath& target,
	                                                 ResourceId id) { return draft->Save(filesystem, target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
		document.value->MarkDirty();
	}
	return document;
}

Result<std::unique_ptr<EditorDocument>> OpenSceneDocument(const VirtualPath& path, ResourceKind kind,
                                                          ComponentRegistry& registry, ResourceSession& resources,
                                                          const EditContextSetup& setup)
{

	auto header = ReadResourceHeader(resources.Index().Filesystem(), path);
	if (!header)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(header.error);
	}
	if (header.value.kind != kind)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure("Scene document kind does not match file");
	}
	auto draft = SceneDraft::Load(resources.Index().Filesystem(), path, kind, registry, resources, setup);
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document =
	    EditorDocument::Create(kind, header.value.id, path,
	                           [draft = draft.value](VirtualFileSystem& filesystem, const VirtualPath& target,
	                                                 ResourceId id) { return draft->Save(filesystem, target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
	}
	return document;
}

Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                const ProjectWorkspace& workspace, ComponentRegistry& registry,
                                ComponentEditors& componentEditors, String& diagnostic)
{
	auto draft = document.Payload<SceneDraft>();
	if (!draft)
	{
		return Result<bool>::Failure("Scene document has no scene draft");
	}
	if (draft->Kind() == ResourceKind::Prefab)
	{
		auto* asset = draft->GetPrefabAsset();
		if (!asset)
		{
			return Result<bool>::Failure("Prefab document has no detached PrefabAsset");
		}
		auto hierarchyChanged = hierarchy.Draw(*asset);
		if (!hierarchyChanged)
		{
			return hierarchyChanged;
		}
		bool changed = hierarchyChanged.value;
		auto selected = hierarchy.SelectedGameObject(*asset);
		ImGui::Begin("Inspector");
		if (!selected)
		{
			ImGui::TextDisabled("Select a GameObject in the Prefab hierarchy.");
		}
		else
		{
			GameObject::Ptr object = selected.value;
			ComponentLoadContext loadContext(documents.Session());
			if (draft->ContextSetup())
			{
				auto prepared = draft->ContextSetup()(loadContext);
				if (!prepared)
				{
					diagnostic = prepared.error;
				}
			}
			ComponentInspectorContext inspectorContext;
			inspectorContext.object = object;
			inspectorContext.roots = {asset->Root()};
			inspectorContext.prefab = asset;
			inspectorContext.loadContext = &loadContext;
			String name = object->Name().value;
			char nameBuffer[257]{};
			std::memcpy(nameBuffer, name.data(), std::min<Ursa::Size>(name.size(), sizeof(nameBuffer) - 1));
			if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
			{
				auto renamed = object->SetName(String(nameBuffer));
				diagnostic = renamed ? String{} : renamed.error;
				changed |= static_cast<bool>(renamed);
			}
			bool enabled = object->IsEnabled().value;
			if (ImGui::Checkbox("Enabled", &enabled))
			{
				auto set = object->SetEnabled(enabled);
				diagnostic = set ? String{} : set.error;
				changed |= static_cast<bool>(set);
			}
			if (ImGui::Button("Add child"))
			{
				auto child = GameObject::Create("GameObject");
				auto parented = child->SetParent(object);
				diagnostic = parented ? String{} : parented.error;
				changed |= static_cast<bool>(parented);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!object->Parent());
			if (ImGui::Button("Delete GameObject"))
			{
				auto removed = object->Destroy();
				diagnostic = removed ? String{} : removed.error;
				if (removed)
				{
					hierarchy.ClearSelection();
					selected = Result<GameObject::Ptr>::Failure("Deleted selection");
					changed = true;
				}
			}
			ImGui::EndDisabled();
			auto local = object->GetLocalTransform();
			if (local)
			{
				LocalTransform transform = local.value;
				Ursa::Float32 position[3]{transform.position.x, transform.position.y, transform.position.z};
				Ursa::Float32 scale[3]{transform.scale.x, transform.scale.y, transform.scale.z};
				bool edited = ImGui::DragFloat3("Position", position, 0.05f);
				edited |= ImGui::DragFloat3("Scale", scale, 0.05f, 0.001f, 100.f);
				if (edited)
				{
					transform.position = {position[0], position[1], position[2]};
					transform.scale = {scale[0], scale[1], scale[2]};
					auto set = object->SetLocalTransform(transform);
					diagnostic = set ? String{} : set.error;
					changed |= static_cast<bool>(set);
				}
			}
			for (Component* component : object->Components())
			{
				ImGui::PushID(component->PersistentId().ToString().c_str());
				auto codec = registry.FindFor(*component);
				const auto* descriptor = codec ? componentEditors.FindType(codec.value->type) : nullptr;
				ImGui::SeparatorText(descriptor ? descriptor->displayName.c_str()
				                     : codec    ? codec.value->type.c_str()
				                                : "Component");
				bool componentEnabled = component->IsEnabled();
				if (ImGui::Checkbox("Enabled", &componentEnabled))
				{
					component->SetEnabled(componentEnabled);
					changed = true;
				}
				ImGui::SameLine();
				if (auto* box = dynamic_cast<BoxRenderComponent*>(component))
				{
					Ursa::Float32 color[4]{box->color.red, box->color.green, box->color.blue, box->color.alpha};
					if (ImGui::ColorEdit4("Color", color))
					{
						box->color = {color[0], color[1], color[2], color[3]};
						changed = true;
					}
					if (ImGui::BeginCombo("Material", box->material ? "Assigned" : "None"))
					{
						if (ImGui::Selectable("None", !box->material))
						{
							box->material.reset();
							changed = true;
						}
						for (const auto& entry : workspace.Resources())
						{
							if (entry.location.kind == ResourceKind::Material)
							{
								const auto label = entry.location.path.FileName();
								if (ImGui::Selectable(label.c_str()))
								{
									auto material = documents.Session().LoadMaterial(entry.id);
									diagnostic = material ? String{} : material.error;
									if (material)
									{
										box->material = material.value;
										changed = true;
									}
								}
							}
						}
						ImGui::EndCombo();
					}
				}
				else if (auto* camera = dynamic_cast<CameraComponent*>(component))
				{
					Ursa::Float32 fov = camera->verticalFovRadians * 57.2957795f;
					bool edited = ImGui::DragFloat("FOV degrees", &fov, 0.5f, 1.f, 179.f);
					edited |= ImGui::DragFloat("Near", &camera->nearPlane, 0.01f, 0.001f, 10000.f);
					edited |= ImGui::DragFloat("Far", &camera->farPlane, 0.1f, 0.002f, 100000.f);
					if (edited && fov > 0.f && fov < 180.f)
					{
						camera->verticalFovRadians = fov * 0.0174532925f;
						changed = true;
					}
				}
				else if (auto* light = dynamic_cast<DirectionalLightComponent*>(component))
				{
					Ursa::Float32 intensity = light->Intensity();
					if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f))
					{
						light->SetIntensity(intensity);
						changed = true;
					}
				}
				else if (auto* pointLight = dynamic_cast<PointLightComponent*>(component))
				{
					Ursa::Float32 intensity = pointLight->Intensity();
					Ursa::Float32 range = pointLight->Range();
					bool edited = ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f);
					edited |= ImGui::DragFloat("Range", &range, 0.05f, 0.001f, 10000.f);
					if (edited)
					{
						pointLight->SetIntensity(intensity);
						pointLight->SetRange(range);
						changed = true;
					}
				}
				else if (codec && componentEditors.Find(codec.value->type))
				{
					ImGuiInspectorUI inspectorUi;
					auto edited =
					    (*componentEditors.Find(codec.value->type))(*component, inspectorContext, inspectorUi);
					diagnostic = edited ? String{} : edited.error;
					changed |= edited && edited.value;
				}
				else
				{
					ImGui::TextDisabled("No property editor registered; serialized data is preserved");
				}
				if (ImGui::Button("Remove component"))
				{
					auto removed = object->RemoveComponent(component);
					diagnostic = removed ? String{} : removed.error;
					changed |= static_cast<bool>(removed);
				}
				ImGui::PopID();
			}
			changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic);
		}
		if (!diagnostic.empty())
		{
			ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		ImGui::End();
		if (changed)
		{
			auto refreshed = draft->RefreshPrefabPreview();
			diagnostic = refreshed ? String{} : refreshed.error;
			document.MarkDirty();
		}
		return Result<bool>::Success(changed);
	}
	Scene& scene = draft->GetScene();
	auto hierarchyChanged = hierarchy.Draw(scene);
	if (!hierarchyChanged)
	{
		return hierarchyChanged;
	}
	bool changed = hierarchyChanged.value;
	ResourceId prefabToPlace;
	ResourceId placementParent;
	auto selected = hierarchy.SelectedGameObject(scene);
	ComponentLoadContext loadContext(documents.Session());
	if (selected && draft->ContextSetup())
	{
		auto prepared = draft->ContextSetup()(loadContext);
		if (!prepared)
		{
			diagnostic = prepared.error;
		}
	}
	ComponentInspectorContext inspectorContext;
	if (selected)
	{
		inspectorContext.object = selected.value;
		inspectorContext.roots = scene.RootGameObjects();
		inspectorContext.scene = &scene;
		inspectorContext.loadContext = &loadContext;
	}
	ImGui::Begin("Inspector");
	if (!selected)
	{
		ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
	}
	else
	{
		GameObject::Ptr node = selected.value;
		auto name = scene.GameObjectName(node);
		if (!name)
		{
			ImGui::End();
			return Result<bool>::Failure(name.error);
		}
		char nameBuffer[257]{};
		std::memcpy(nameBuffer, name.value.data(), std::min<Ursa::Size>(name.value.size(), sizeof(nameBuffer) - 1));
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
		{
			auto renamed = scene.SetGameObjectName(node, String(nameBuffer));
			diagnostic = renamed ? String{} : renamed.error;
			changed |= static_cast<bool>(renamed);
		}
		auto enabledState = scene.IsEnabled(node);
		if (!enabledState)
		{
			ImGui::End();
			return Result<bool>::Failure(enabledState.error);
		}
		bool enabled = enabledState.value;
		if (ImGui::Checkbox("Enabled", &enabled))
		{
			auto set = scene.SetEnabled(node, enabled);
			diagnostic = set ? String{} : set.error;
			changed |= static_cast<bool>(set);
		}
	}
	if (draft->Kind() == ResourceKind::Scene && ImGui::BeginCombo("Place Prefab", "Select source"))
	{
		for (const auto& entry : workspace.Resources())
		{
			if (entry.location.kind == ResourceKind::Prefab)
			{
				ImGui::PushID(entry.id.ToString().c_str());
				const auto name = entry.location.path.FileName();
				if (ImGui::Selectable(name.c_str()))
				{
					prefabToPlace = entry.id;
					placementParent = selected ? selected.value->Id() : ResourceId{};
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (selected)
	{
		GameObject::Ptr node = selected.value;
		if (draft->Linked())
		{
			auto selectedId = scene.PersistentId(node);
			if (selectedId)
			{
				for (const auto& instance : draft->LinkedScene()->scopedIds)
				{
					for (const auto& mapping : instance.second)
					{
						if (mapping.second == selectedId.value.ToString())
						{
							ImGui::Text("Prefab instance: %s", instance.first.c_str());
							if (ImGui::Button("Pin label override"))
							{
								auto instanceId = ResourceId::Parse(instance.first);
								auto sourceId = ResourceId::Parse(mapping.first);
								auto label = scene.GameObjectName(node);
								if (instanceId && sourceId && label)
								{
									auto pinned = draft->SetInstanceProperty(instanceId.value, sourceId.value, "/name",
									                                         ToStdString(label.value));
									diagnostic = pinned ? String{} : pinned.error;
									changed |= static_cast<bool>(pinned);
								}
							}
							break;
						}
					}
				}
			}
		}
		if (ImGui::Button("Add child"))
		{
			auto added = scene.CreateGameObject(node);
			diagnostic = added ? String{} : added.error;
			changed |= static_cast<bool>(added);
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete node"))
		{
			auto removed = scene.DestroyGameObject(node);
			diagnostic = removed ? String{} : removed.error;
			if (removed)
			{
				hierarchy.ClearSelection();
				changed = true;
			}
		}
		if (scene.Contains(node))
		{
			auto parent = scene.ParentOf(node);
			if (parent)
			{
				Vector<GameObject::Ptr> siblings;
				if (parent.value)
				{
					auto children = scene.ChildrenOf(parent.value);
					if (children)
					{
						siblings = std::move(children.value);
					}
				}
				else
				{
					siblings = scene.RootGameObjects();
				}
				const auto position = std::find(siblings.begin(), siblings.end(), node);
				if (position != siblings.end())
				{
					const Ursa::Size index = static_cast<Ursa::Size>(position - siblings.begin());
					ImGui::BeginDisabled(index == 0);
					if (ImGui::Button("Move up"))
					{
						auto moved = scene.ReorderGameObject(node, index - 1);
						diagnostic = moved ? String{} : moved.error;
						changed |= static_cast<bool>(moved);
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(index + 1 >= siblings.size());
					if (ImGui::Button("Move down"))
					{
						auto moved = scene.ReorderGameObject(node, index + 1);
						diagnostic = moved ? String{} : moved.error;
						changed |= static_cast<bool>(moved);
					}
					ImGui::EndDisabled();
				}
				if (ImGui::BeginCombo("Parent", parent.value ? "Node" : "Scene root"))
				{
					if (ImGui::Selectable("Scene root", !parent.value))
					{
						auto moved = scene.SetParent(node, {});
						diagnostic = moved ? String{} : moved.error;
						changed |= static_cast<bool>(moved);
					}
					std::function<void(GameObject::Ptr)> offer = [&](GameObject::Ptr candidate)
					{
						if (candidate != node)
						{
							auto label = scene.GameObjectName(candidate);
							auto id = scene.PersistentId(candidate);
							if (label && id)
							{
								ImGui::PushID(id.value.ToString().c_str());
								if (ImGui::Selectable(label.value.c_str(), parent.value == candidate))
								{
									auto moved = scene.SetParent(node, candidate);
									diagnostic = moved ? String{} : moved.error;
									changed |= static_cast<bool>(moved);
								}
								ImGui::PopID();
							}
						}
						auto children = scene.ChildrenOf(candidate);
						if (children)
						{
							for (GameObject::Ptr child : children.value)
							{
								offer(child);
							}
						}
					};
					for (GameObject::Ptr root : scene.RootGameObjects())
					{
						offer(root);
					}
					ImGui::EndCombo();
				}
			}
			const auto local = scene.GetLocalTransform(node);
			if (local)
			{
				LocalTransform value = local.value;
				Ursa::Float32 position[3]{value.position.x, value.position.y, value.position.z};
				Ursa::Float32 scale[3]{value.scale.x, value.scale.y, value.scale.z};
				bool transformChanged = ImGui::DragFloat3("Position", position, 0.05f);
				transformChanged |= ImGui::DragFloat3("Scale", scale, 0.05f, 0.001f, 100.f);
				if (transformChanged)
				{
					value.position = {position[0], position[1], position[2]};
					value.scale = {scale[0], scale[1], scale[2]};
					try
					{
						auto transformParent = scene.ParentOf(node);
						const Mat4 parentWorld = transformParent && transformParent.value
						                             ? scene.WorldTransform(transformParent.value).value
						                             : Mat4::Identity();
						auto applied = ApplyGizmoWorldTransform(scene, node, parentWorld * value.Matrix());
						diagnostic = applied ? String{} : applied.error;
						changed |= static_cast<bool>(applied);
					}
					catch (const std::exception& error)
					{
						diagnostic = error.what();
					}
				}
			}
			auto components = scene.ComponentsOf(node);
			if (!components)
			{
				ImGui::End();
				return Result<bool>::Failure(components.error);
			}
			for (ComponentId componentId : components.value)
			{
				Component* object = scene.GetComponent(componentId);
				if (!object)
				{
					continue;
				}
				auto id = scene.ComponentPersistentId(componentId);
				if (!id)
				{
					ImGui::End();
					return Result<bool>::Failure(id.error);
				}
				ImGui::PushID(id.value.ToString().c_str());
				auto codec = registry.FindFor(*object);
				if (!codec)
				{
					diagnostic = codec.error;
					ImGui::PopID();
					continue;
				}
				const auto* descriptor = componentEditors.FindType(codec.value->type);
				ImGui::SeparatorText(descriptor ? descriptor->displayName.c_str() : codec.value->type.c_str());
				auto enabled = scene.IsComponentEnabled(componentId);
				if (enabled)
				{
					bool value = enabled.value;
					if (ImGui::Checkbox("Enabled", &value))
					{
						auto set = scene.SetComponentEnabled(componentId, value);
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
				}
				ImGui::SameLine();
				const bool remove = ImGui::Button("Remove component");
				if (remove)
				{
					auto removed = scene.RemoveComponent(componentId);
					diagnostic = removed ? String{} : removed.error;
					changed |= static_cast<bool>(removed);
					ImGui::PopID();
					continue;
				}
				if (auto* box = dynamic_cast<BoxRenderComponent*>(object))
				{
					Ursa::Float32 color[4]{box->color.red, box->color.green, box->color.blue, box->color.alpha};
					if (ImGui::ColorEdit4("Color", color))
					{
						box->color = {color[0], color[1], color[2], color[3]};
						changed = true;
					}
					const auto selectedMaterial = draft->MaterialId(box->material);
					String materialName = "None";
					if (selectedMaterial)
					{
						for (const auto& entry : workspace.Resources())
						{
							if (entry.id == selectedMaterial.value)
							{
								materialName = entry.location.path.FileName();
							}
						}
					}
					if (ImGui::BeginCombo("Material", materialName.c_str()))
					{
						if (ImGui::Selectable("None", !box->material))
						{
							auto set = draft->AssignMaterial(componentId, {}, documents.Session());
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
						for (const auto& entry : workspace.Resources())
						{
							if (entry.location.kind == ResourceKind::Material)
							{
								ImGui::PushID(entry.id.ToString().c_str());
								const auto name = entry.location.path.FileName();
								if (ImGui::Selectable(name.c_str(),
								                      selectedMaterial && selectedMaterial.value == entry.id))
								{
									auto set = draft->AssignMaterial(componentId, entry.id, documents.Session());
									diagnostic = set ? String{} : set.error;
									changed |= static_cast<bool>(set);
								}
								ImGui::PopID();
							}
						}
						ImGui::EndCombo();
					}
				}
				else if (auto* camera = dynamic_cast<CameraComponent*>(object))
				{
					Ursa::Float32 fov = camera->verticalFovRadians * 57.2957795f;
					Ursa::Float32 nearPlane = camera->nearPlane;
					Ursa::Float32 farPlane = camera->farPlane;
					bool edited = ImGui::DragFloat("FOV degrees", &fov, 0.5f, 1.f, 179.f);
					edited |= ImGui::DragFloat("Near", &nearPlane, 0.01f, 0.001f, 10000.f);
					edited |= ImGui::DragFloat("Far", &farPlane, 0.1f, 0.002f, 100000.f);
					if (edited)
					{
						if (std::isfinite(fov) && std::isfinite(nearPlane) && std::isfinite(farPlane) && fov > 0.f &&
						    fov < 180.f && nearPlane > 0.f && farPlane > nearPlane)
						{
							camera->verticalFovRadians = fov * 0.0174532925f;
							camera->nearPlane = nearPlane;
							camera->farPlane = farPlane;
							changed = true;
							diagnostic.clear();
						}
						else
						{
							diagnostic = "Camera requires 0 < FOV < 180 and 0 < Near < Far";
						}
					}
					if (ImGui::Button("Use as game camera"))
					{
						auto set = scene.SetActiveCamera(componentId);
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
				}
				else if (auto* directional = dynamic_cast<DirectionalLightComponent*>(object))
				{
					Color value = directional->Color();
					Ursa::Float32 color[4]{value.red, value.green, value.blue, value.alpha};
					if (ImGui::ColorEdit4("Color", color))
					{
						auto set = directional->SetColor({color[0], color[1], color[2], color[3]});
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
					Ursa::Float32 intensity = directional->Intensity();
					if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f))
					{
						auto set = directional->SetIntensity(intensity);
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
				}
				else if (auto* point = dynamic_cast<PointLightComponent*>(object))
				{
					Color value = point->Color();
					Ursa::Float32 color[4]{value.red, value.green, value.blue, value.alpha};
					if (ImGui::ColorEdit4("Color", color))
					{
						auto set = point->SetColor({color[0], color[1], color[2], color[3]});
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
					Ursa::Float32 intensity = point->Intensity();
					Ursa::Float32 range = point->Range();
					if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f))
					{
						auto set = point->SetIntensity(intensity);
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
					if (ImGui::DragFloat("Range", &range, 0.05f, 0.001f, 10000.f))
					{
						auto set = point->SetRange(range);
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
				}
				else if (const auto* editor = componentEditors.Find(codec.value->type))
				{
					inspectorContext.object = node;
					ImGuiInspectorUI inspectorUi;
					auto edited = (*editor)(*object, inspectorContext, inspectorUi);
					diagnostic = edited ? String{} : edited.error;
					changed |= edited && edited.value;
				}
				else
				{
					ImGui::TextDisabled("No property editor registered; serialized data is preserved");
				}
				ImGui::PopID();
			}
			changed |= DrawAddComponentBrowser(componentEditors, inspectorContext, diagnostic);
		}
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	ImGui::End();
	if (prefabToPlace)
	{
		auto placed = draft->PlacePrefab(prefabToPlace, placementParent, document.Path(), document.Id());
		diagnostic = placed ? String{} : placed.error;
		changed |= static_cast<bool>(placed);
	}
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
