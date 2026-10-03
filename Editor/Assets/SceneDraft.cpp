#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
Result<std::shared_ptr<SceneDraft>> SceneDraft::Create(ResourceKind kind, ComponentRegistry& registry,
                                                       ResourceSession& resources, const EditContextSetup& setup)
{
	if (kind != ResourceKind::scene && kind != ResourceKind::prefab)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure("Scene draft kind must be Scene or Prefab");
	}
	auto draft = std::shared_ptr<SceneDraft>(new SceneDraft(kind, registry, resources, setup));
	draft->scene = std::make_unique<Scene>();
	auto root = draft->scene->CreateNode();
	if (!root)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure(root.error);
	}
	auto labeled = draft->scene->SetNodeLabel(root.value, kind == ResourceKind::scene ? "Root" : "Prefab");
	if (!labeled)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure(labeled.error);
	}
	return Result<std::shared_ptr<SceneDraft>>::Success(std::move(draft));
}

Result<std::shared_ptr<SceneDraft>> SceneDraft::Load(const std::filesystem::path& path, ResourceKind kind,
                                                     ComponentRegistry& registry, ResourceSession& resources,
                                                     const EditContextSetup& setup)
{
	if (kind != ResourceKind::scene && kind != ResourceKind::prefab)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure("Scene draft kind must be Scene or Prefab");
	}
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto configured = setup(context);
		if (!configured)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(configured.error);
		}
	}
	auto draft = std::shared_ptr<SceneDraft>(new SceneDraft(kind, registry, resources, setup));
	if (kind == ResourceKind::prefab)
	{
		auto loaded = LoadPrefabResource(path, registry, context);
		if (!loaded)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(loaded.error);
		}
		draft->scene = std::move(loaded.value.scene);
	}
	else
	{
		auto json = ReadJsonResource(path, ResourceKind::scene);
		if (!json)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(json.error);
		}
		if (json.value.contains("instances"))
		{
			auto loaded = LoadLinkedSceneResource(path, registry, context);
			if (!loaded)
			{
				return Result<std::shared_ptr<SceneDraft>>::Failure(loaded.error);
			}
			draft->linked = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
		}
		else
		{
			auto loaded = LoadSceneResource(path, registry, context);
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

Result<void> SceneDraft::Save(const std::filesystem::path& path, ResourceId id)
{
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	if (kind == ResourceKind::prefab)
	{
		const auto roots = GetScene().RootNodes();
		if (roots.size() != 1)
		{
			return Result<void>::Failure("Prefab document must contain exactly one root node");
		}
		return SavePrefabResource(path, id, GetScene(), roots.front(), registry, resolver);
	}
	if (linked)
	{
		return SaveLinkedSceneResource(path, *linked, registry, resolver);
	}
	return SaveSceneResource(path, id, GetScene(), registry, resolver);
}

Result<void> SceneDraft::AssignMaterial(NodeId node, ResourceId materialId, ResourceSession& resources)
{
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
			return AssignMaterial(id, materialId, resources);
		}
	}
	return Result<void>::Failure("Box renderer component is missing");
}

Result<void> SceneDraft::AssignMaterial(ComponentId component, ResourceId materialId, ResourceSession& resources)
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
	auto material = resources.LoadMaterial(materialId);
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

Result<ResourceId> SceneDraft::PlacePrefab(ResourceId prefabId, NodeId parent, const std::filesystem::path& scenePath,
                                           ResourceId sceneId)
{
	if (kind != ResourceKind::scene)
	{
		return Result<ResourceId>::Failure("Prefab instances can only be placed in scenes");
	}
	ResourceId parentId;
	if (parent)
	{
		auto resolved = GetScene().NodePersistentId(parent);
		if (!resolved)
		{
			return Result<ResourceId>::Failure(resolved.error);
		}
		parentId = resolved.value;
	}
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	nlohmann::json document;
	if (linked)
	{
		const auto previousDocument = linked->document;
		const auto previousSnapshot = linked->snapshot;
		const auto previousScopes = linked->scopedIds;
		auto captured = CaptureLinkedSceneChanges(scenePath, *linked, registry, resolver);
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
		auto encoded = EncodeSceneDocument(scenePath, sceneId, *scene, registry, resolver);
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
	auto loaded = DecodeLinkedSceneDocument(scenePath, std::move(document), registry, context);
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
	auto document = EditorDocument::Create(kind, ResourceId::Generate(), path,
	                                       [draft = draft.value](const std::filesystem::path& target, ResourceId id)
	                                       { return draft->Save(target, id); });
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
	auto host = resources.Index().Locate(ResourceLocation{kind, path});
	if (!host)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(host.error);
	}
	auto header = ReadResourceHeader(host.value);
	if (!header)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(header.error);
	}
	if (header.value.kind != kind)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure("Scene document kind does not match file");
	}
	auto draft = SceneDraft::Load(host.value, kind, registry, resources, setup);
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document = EditorDocument::Create(kind, header.value.id, path,
	                                       [draft = draft.value](const std::filesystem::path& target, ResourceId id)
	                                       { return draft->Save(target, id); });
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
	Scene& scene = draft->GetScene();
	auto hierarchyChanged = hierarchy.Draw(scene, registry);
	if (!hierarchyChanged)
	{
		return hierarchyChanged;
	}
	bool changed = hierarchyChanged.value;
	ResourceId prefabToPlace;
	NodeId placementParent;
	ImGui::Begin("Inspector");
	auto selected = hierarchy.SelectedNode(scene);
	if (!selected)
	{
		ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
	}
	if (draft->Kind() == ResourceKind::scene && ImGui::BeginCombo("Place Prefab", "Select source"))
	{
		for (const auto& entry : workspace.Resources())
		{
			if (entry.location.kind == ResourceKind::prefab)
			{
				ImGui::PushID(entry.id.ToString().c_str());
				const auto name = entry.location.path.FileName();
				if (ImGui::Selectable(name.c_str()))
				{
					prefabToPlace = entry.id;
					placementParent = selected ? selected.value : NodeId{};
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (selected)
	{
		NodeId node = selected.value;
		if (draft->Linked())
		{
			auto selectedId = scene.NodePersistentId(node);
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
								auto label = scene.NodeLabel(node);
								if (instanceId && sourceId && label)
								{
									auto pinned = draft->SetInstanceProperty(instanceId.value, sourceId.value, "/label",
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
			auto added = scene.CreateNode(node);
			diagnostic = added ? String{} : added.error;
			changed |= static_cast<bool>(added);
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete node"))
		{
			auto removed = scene.DestroyNode(node);
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
				Vector<NodeId> siblings;
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
					siblings = scene.RootNodes();
				}
				const auto position = std::find(siblings.begin(), siblings.end(), node);
				if (position != siblings.end())
				{
					const size_t index = static_cast<size_t>(position - siblings.begin());
					ImGui::BeginDisabled(index == 0);
					if (ImGui::Button("Move up"))
					{
						auto moved = scene.ReorderNode(node, index - 1);
						diagnostic = moved ? String{} : moved.error;
						changed |= static_cast<bool>(moved);
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(index + 1 >= siblings.size());
					if (ImGui::Button("Move down"))
					{
						auto moved = scene.ReorderNode(node, index + 1);
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
					std::function<void(NodeId)> offer = [&](NodeId candidate)
					{
						if (candidate != node)
						{
							auto label = scene.NodeLabel(candidate);
							auto id = scene.NodePersistentId(candidate);
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
							for (NodeId child : children.value)
							{
								offer(child);
							}
						}
					};
					for (NodeId root : scene.RootNodes())
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
				float position[3]{value.position.x, value.position.y, value.position.z};
				float scale[3]{value.scale.x, value.scale.y, value.scale.z};
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
			if (ImGui::Button("Add Box"))
			{
				auto added = scene.AddComponent<BoxRenderComponent>(node);
				diagnostic = added ? String{} : added.error;
				changed |= static_cast<bool>(added);
			}
			ImGui::SameLine();
			if (ImGui::Button("Add Camera"))
			{
				auto added = scene.AddComponent<CameraComponent>(node);
				diagnostic = added ? String{} : added.error;
				changed |= static_cast<bool>(added);
			}
			ImGui::SameLine();
			if (ImGui::Button("Add Light"))
			{
				auto added = scene.AddComponent<DirectionalLightComponent>(node);
				diagnostic = added ? String{} : added.error;
				changed |= static_cast<bool>(added);
			}
			ImGui::SameLine();
			if (ImGui::Button("Add Point Light"))
			{
				auto added = scene.AddComponent<PointLightComponent>(node);
				diagnostic = added ? String{} : added.error;
				changed |= static_cast<bool>(added);
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
				ImGui::SeparatorText(codec.value->type.c_str());
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
					float color[4]{box->color.red, box->color.green, box->color.blue, box->color.alpha};
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
							if (entry.location.kind == ResourceKind::material)
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
					float fov = camera->verticalFovRadians * 57.2957795f;
					float nearPlane = camera->nearPlane;
					float farPlane = camera->farPlane;
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
					float color[4]{value.red, value.green, value.blue, value.alpha};
					if (ImGui::ColorEdit4("Color", color))
					{
						auto set = directional->SetColor({color[0], color[1], color[2], color[3]});
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
					float intensity = directional->Intensity();
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
					float color[4]{value.red, value.green, value.blue, value.alpha};
					if (ImGui::ColorEdit4("Color", color))
					{
						auto set = point->SetColor({color[0], color[1], color[2], color[3]});
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
					float intensity = point->Intensity();
					float range = point->Range();
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
					auto edited = (*editor)(*object, scene, node);
					diagnostic = edited ? String{} : edited.error;
					changed |= edited && edited.value;
				}
				else
				{
					ImGui::TextDisabled("No property editor registered; serialized data is preserved");
				}
				ImGui::PopID();
			}
		}
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	ImGui::End();
	if (prefabToPlace)
	{
		auto sceneHost = workspace.Index().Locate(ResourceLocation{document.Kind(), document.Path()});
		if (!sceneHost)
		{
			diagnostic = sceneHost.error;
		}
		else
		{
			auto placed = draft->PlacePrefab(prefabToPlace, placementParent, sceneHost.value, document.Id());
			diagnostic = placed ? String{} : placed.error;
			changed |= static_cast<bool>(placed);
		}
	}
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
