#include <Ui/EditorIcons.hpp>
#include <Inspector/MeshInspector.hpp>
#include <Assets/MeshAssetEditor.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/GameObject.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
struct Candidate
{
	AssetReference reference;
	String label;
};

Vector<Candidate> Candidates(ProjectWorkspace& workspace, ResourceKind kind)
{
	Vector<Candidate> result;
	for (const auto& entry : workspace.Resources())
	{
		if (entry.location.kind == kind)
		{
			result.push_back({{entry.id, 0}, entry.location.path.ToString()});
		}
	}
	for (auto* database : {workspace.Database(), workspace.BuiltinDatabase()})
	{
		if (!database)
		{
			continue;
		}
		const auto artifacts = database->ImportedArtifacts();
		for (const auto& artifact : artifacts)
		{
			if (artifact.localFileId && artifact.kind == (kind == ResourceKind::Mesh      ? "Mesh"
			                                              : kind == ResourceKind::Texture ? "Texture"
			                                                                              : "Material"))
			{
				result.push_back({{artifact.guid, artifact.localFileId},
				                  artifact.guid.ToString() + ":" + ToUrsaString(std::to_string(artifact.localFileId))});
			}
		}
	}
	return result;
}

bool Picker(const char* label, AssetReference current, const Vector<Candidate>& options, AssetReference& selected)
{
	String preview = current.guid ? current.guid.ToString() : String("None / Local");
	for (const auto& option : options)
	{
		if (option.reference.guid == current.guid && option.reference.localFileId == current.localFileId)
		{
			preview = option.label;
			break;
		}
	}
	bool changed = false;
	if (ImGui::BeginCombo(label, preview.c_str()))
	{
		if (Icons::Selectable(Icons::Icon::Close, "None"))
		{
			selected = {};
			changed = true;
		}
		for (const auto& option : options)
		{
			if (Icons::Selectable(Icons::Icon::Link, option.label.c_str(),
			                      option.reference.guid == current.guid &&
			                          option.reference.localFileId == current.localFileId))
			{
				selected = option.reference;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("URSA_ASSET_REFERENCE"))
		{
			if (payload->DataSize == sizeof(AssetReference))
			{
				selected = *static_cast<const AssetReference*>(payload->Data);
				changed = true;
			}
		}
		ImGui::EndDragDropTarget();
	}
	return changed;
}

Result<bool> EditMaterial(MaterialInstance& material, ProjectWorkspace& workspace, ResourceSession& resources)
{
	bool changed = false;
	auto shader = material.Shader();
	if (!shader && material.ShaderId())
	{
		auto loaded = resources.LoadShader(material.ShaderId());
		if (!loaded)
		{
			return Result<bool>::Failure(loaded.error);
		}
		shader = std::move(loaded.value);
	}
	if (!shader)
	{
		const auto color = material.Parameters().baseColor;
		Float32 values[]{color.red, color.green, color.blue, color.alpha};
		if (ImGui::ColorEdit4("Color", values))
		{
			auto result = material.SetBaseColor({values[0], values[1], values[2], values[3]});
			return result ? Result<bool>::Success(true) : Result<bool>::Failure(result.error);
		}
		return Result<bool>::Success(false);
	}
	for (const auto& property : shader->properties)
	{
		ImGui::PushID(property.identifier.c_str());
		auto current = material.PropertyValue(property.identifier, *shader);
		if (!current)
		{
			ImGui::PopID();
			return Result<bool>::Failure(current.error);
		}
		auto value = current.value;
		bool edited = false;
		const char* label = property.displayName.c_str();
		switch (property.type)
		{
			case Shader::PropertyType::Float:
				edited = ImGui::DragFloat(label, &value.floatValue, 0.01f);
				break;
			case Shader::PropertyType::Range:
				edited = ImGui::SliderFloat(label, &value.floatValue, property.rangeMinimum, property.rangeMaximum);
				break;
			case Shader::PropertyType::Int:
				edited = ImGui::InputInt(label, &value.intValue);
				break;
			case Shader::PropertyType::Color:
				edited = ImGui::ColorEdit4(label, value.vectorValue.data(),
				                           ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
				break;
			case Shader::PropertyType::Vector:
				edited = ImGui::DragFloat4(label, value.vectorValue.data(), 0.01f);
				break;
			case Shader::PropertyType::Texture2D:
			{
				AssetReference selected;
				if (Picker(label, {value.textureId, value.textureLocalFileId},
				           Candidates(workspace, ResourceKind::Texture), selected))
				{
					value.textureId = selected.guid;
					value.textureLocalFileId = selected.localFileId;
					if (selected.guid)
					{
						auto loaded = resources.LoadTexture(selected.guid, selected.localFileId);
						if (!loaded)
						{
							ImGui::PopID();
							return Result<bool>::Failure(loaded.error);
						}
						value.texture = std::move(loaded.value);
						value.textureDefault.clear();
					}
					else
					{
						value.texture.reset();
						value.textureDefault = property.defaultTexture;
					}
					edited = true;
				}
				break;
			}
		}
		if (edited)
		{
			auto result = material.SetPropertyValue(property.identifier, *shader, std::move(value));
			if (!result)
			{
				ImGui::PopID();
				return Result<bool>::Failure(result.error);
			}
			changed = true;
		}
		if (Icons::SmallButton("Reset"))
		{
			auto result = material.RemovePropertyOverride(property.identifier);
			if (!result)
			{
				ImGui::PopID();
				return Result<bool>::Failure(result.error);
			}
			changed = true;
		}
		ImGui::PopID();
	}
	return Result<bool>::Success(changed);
}

} // namespace

bool HasMeshInspector(const Component& component)
{
	return dynamic_cast<const MeshFilterComponent*>(&component) || dynamic_cast<const RendererComponent*>(&component);
}

Result<bool> DrawMeshInspector(Component& component, ProjectWorkspace& workspace, ResourceSession& resources)
{
	bool changed = false;
	auto* filter = dynamic_cast<MeshFilterComponent*>(&component);
	auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(&component);
	auto mesh = filter ? filter->SharedMesh() : skin ? skin->SharedMesh() : nullptr;
	if (filter || skin)
	{
		AssetReference selected;
		if (Picker("Mesh", mesh ? mesh->AssetIdentity() : AssetReference{}, Candidates(workspace, ResourceKind::Mesh),
		           selected))
		{
			auto loaded = selected.guid ? resources.LoadMesh(selected.guid, selected.localFileId)
			                            : Result<MeshPtr>::Success(nullptr);
			if (!loaded)
			{
				return Result<bool>::Failure(loaded.error);
			}
			auto assigned = filter ? filter->SetSharedMesh(loaded.value) : skin->SetSharedMesh(loaded.value);
			if (!assigned)
			{
				return Result<bool>::Failure(assigned.error);
			}
			mesh = std::move(loaded.value);
			changed = true;
		}
		if (mesh && mesh->IsValid())
		{
			const auto snapshot = mesh->Snapshot();
			ImGui::Text("Vertices: %llu  Indices: %llu  Submeshes: %llu", UInt64(snapshot->data.positions.size()),
			            UInt64(snapshot->data.indices.size()), UInt64(snapshot->data.subMeshes.size()));
			ImGui::Text("Version: %llu  %s", snapshot->version,
			            mesh->AssetIdentity().guid ? "Asset" : "Local instance");
			ImGui::Text("Bounds: (%.2f, %.2f, %.2f) / (%.2f, %.2f, %.2f)", snapshot->data.bounds.minimum.x,
			            snapshot->data.bounds.minimum.y, snapshot->data.bounds.minimum.z,
			            snapshot->data.bounds.maximum.x, snapshot->data.bounds.maximum.y,
			            snapshot->data.bounds.maximum.z);
			if (Icons::Button("Save Mesh As..."))
			{
				ImGui::OpenPopup("Save Mesh As");
			}
			if (ImGui::BeginPopup("Save Mesh As"))
			{
				static char path[512] = "prj:Meshes/NewMesh.ursamesh";
				static bool replace = true;
				ImGui::InputText("Asset path", path, sizeof(path));
				ImGui::Checkbox("Replace this binding", &replace);
				if (Icons::Button("Save"))
				{
					auto parsed = VirtualPath::Parse(path);
					if (!parsed)
					{
						ImGui::EndPopup();
						return Result<bool>::Failure(parsed.error);
					}
					if (!workspace.Database())
					{
						ImGui::EndPopup();
						return Result<bool>::Failure("Project AssetDatabase is unavailable");
					}
					auto saved = SaveMeshAs(*workspace.Database(), resources, parsed.value, mesh);
					if (!saved)
					{
						ImGui::EndPopup();
						return Result<bool>::Failure(saved.error);
					}
					if (replace)
					{
						auto assigned = filter ? filter->SetSharedMesh(saved.value) : skin->SetSharedMesh(saved.value);
						if (!assigned)
						{
							ImGui::EndPopup();
							return Result<bool>::Failure(assigned.error);
						}
						changed = true;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
	}
	if (auto* renderer = dynamic_cast<RendererComponent*>(&component))
	{
		bool castsShadows = renderer->CastShadows();
		if (ImGui::Checkbox("Cast Shadows", &castsShadows))
		{
			renderer->SetCastShadows(castsShadows);
			changed = true;
		}
		bool receiveShadows = renderer->ReceiveShadows();
		if (ImGui::Checkbox("Receive Shadows", &receiveShadows))
		{
			renderer->SetReceiveShadows(receiveShadows);
			changed = true;
		}
		auto materials = renderer->SharedMaterials();
		int size = int(materials.size());
		if (ImGui::InputInt("Materials Size", &size) && size >= 0 && size <= int(Mesh::maxSubMeshes))
		{
			materials.resize(Size(size));
			changed = true;
		}
		if (!mesh)
		{
			auto owner = renderer->GetGameObject();
			auto* ownerFilter = owner ? owner->FindComponent<MeshFilterComponent>() : nullptr;
			mesh = ownerFilter ? ownerFilter->SharedMesh() : nullptr;
		}
		const auto subMeshes = mesh && mesh->IsValid() ? mesh->Snapshot()->data.subMeshes.size() : 0;
		if (subMeshes && materials.size() < subMeshes)
		{
			ImGui::TextWrapped("Missing slots use the default material.");
		}
		if (subMeshes && materials.size() > subMeshes)
		{
			ImGui::TextWrapped("Extra slots redraw the last submesh in order.");
		}
		const auto options = Candidates(workspace, ResourceKind::Material);
		for (Size i = 0; i < materials.size(); ++i)
		{
			ImGui::PushID(int(i));
			AssetReference selected;
			if (Picker("Element", materials[i] ? materials[i]->AssetIdentity() : AssetReference{}, options, selected))
			{
				auto loaded = selected.guid ? resources.LoadMaterial(selected.guid, selected.localFileId)
				                            : Result<std::shared_ptr<MaterialInstance>>::Success(nullptr);
				if (!loaded)
				{
					ImGui::PopID();
					return Result<bool>::Failure(loaded.error);
				}
				materials[i] = std::move(loaded.value);
				changed = true;
			}
			if (i && Icons::SmallButton("Up"))
			{
				std::swap(materials[i], materials[i - 1]);
				changed = true;
			}
			ImGui::SameLine();
			if (i + 1 < materials.size() && Icons::SmallButton("Down"))
			{
				std::swap(materials[i], materials[i + 1]);
				changed = true;
			}
			ImGui::SameLine();
			if (Icons::SmallButton("Clear"))
			{
				materials[i].reset();
				changed = true;
			}
			if (!materials[i])
			{
				ImGui::TextDisabled("Default material used for this slot");
			}
			else if (materials[i]->IsValid())
			{
				if (materials[i]->AssetIdentity().guid && Icons::SmallButton("Make Local Copy"))
				{
					materials[i] = materials[i]->Clone();
					changed = true;
				}
				ImGui::BeginDisabled(bool(materials[i]->AssetIdentity().guid));
				auto edited = EditMaterial(*materials[i], workspace, resources);
				if (!edited)
				{
					ImGui::EndDisabled();
					ImGui::PopID();
					return Result<bool>::Failure(edited.error);
				}
				changed |= edited.value;
				ImGui::EndDisabled();
			}
			ImGui::PopID();
		}
		if (changed)
		{
			auto assigned = renderer->SetMaterials(std::move(materials));
			if (!assigned)
			{
				return Result<bool>::Failure(assigned.error);
			}
		}
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
