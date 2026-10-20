#include <Ui/EditorIcons.hpp>
#include <Core/Types.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/MaterialInspectors.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/AssetReference.hpp>
#include <Core/StringInterop.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <imgui.h>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
constexpr Array<const char*, 5> slotNames{"Base Color", "Metallic Roughness", "Normal", "Occlusion", "Emissive"};
constexpr Array<const char*, 5> fieldNames{"baseColor", "metallicRoughness", "normal", "occlusion", "emissive"};

bool AllowedSlot(MaterialType type, Ursa::Size index)
{
	return type == MaterialType::PbrMetallicRoughness || index == 0;
}
} // namespace

Result<std::shared_ptr<MaterialDraft>> MaterialDraft::Load(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                           ResourceSession& session)
{
	auto document = ReadJsonResource(filesystem, path, ResourceKind::Material);
	if (!document)
	{
		return Result<std::shared_ptr<MaterialDraft>>::Failure(document.error);
	}
	auto loaded = LoadMaterialResource(filesystem, path, [&session](ResourceId id) { return session.LoadTexture(id); });
	if (!loaded)
	{
		return Result<std::shared_ptr<MaterialDraft>>::Failure(loaded.error);
	}
	auto draft = std::make_shared<MaterialDraft>(loaded.value.material->Type());
	draft->material = std::move(loaded.value.material);
	if (!draft->material->IsShaderBacked())
	{
		const ResourceId builtinId = draft->material->Type() == MaterialType::Unlit ? Shader::BuiltinUnlitShaderId()
		                                                                            : Shader::BuiltinPbrShaderId();
		auto migrated = draft->material->MigrateLegacyToShader(builtinId);
		if (!migrated)
		{
			return Result<std::shared_ptr<MaterialDraft>>::Failure(migrated.error);
		}
	}
	if (draft->material->IsShaderBacked())
	{
		auto shader = session.LoadShader(draft->material->ShaderId());
		if (!shader)
		{
			return Result<std::shared_ptr<MaterialDraft>>::Failure(shader.error);
		}
		auto attachedShader = draft->material->SetShaderAsset(shader.value);
		if (!attachedShader)
		{
			return Result<std::shared_ptr<MaterialDraft>>::Failure(attachedShader.error);
		}
	}
	if (document.value.contains("textures") && document.value["textures"].is_object())
	{
		for (Ursa::Size index = 0; index < fieldNames.size(); ++index)
		{
			if (document.value["textures"].contains(fieldNames[index]))
			{
				auto reference = AssetReferenceFromJson(document.value["textures"][fieldNames[index]],
				                                        "textures." + String(fieldNames[index]));
				if (!reference)
				{
					return Result<std::shared_ptr<MaterialDraft>>::Failure(reference.error);
				}
				draft->textureIds[index] = reference.value.guid;
			}
		}
	}
	return Result<std::shared_ptr<MaterialDraft>>::Success(std::move(draft));
}

ResourceId MaterialDraft::TextureId(MaterialTextureSlot slot) const
{
	const Ursa::Size index = static_cast<Ursa::Size>(slot);
	return index < textureIds.size() ? textureIds[index] : ResourceId{};
}

Result<void> MaterialDraft::SetPropertyValue(const String& identifier, MaterialPropertyValue value)
{
	const auto shader = material->Shader();
	if (!shader)
	{
		return Result<void>::Failure("Material Shader properties are not loaded");
	}
	return material->SetPropertyValue(identifier, *shader, std::move(value));
}

Result<void> MaterialDraft::SetTextureId(MaterialTextureSlot slot, ResourceId id, ResourceSession& session)
{
	const Ursa::Size index = static_cast<Ursa::Size>(slot);
	if (index >= textureIds.size() || !AllowedSlot(material->Type(), index))
	{
		return Result<void>::Failure("Texture slot is not valid for this material type");
	}
	if (!id)
	{
		auto cleared = material->SetTexture(slot, {});
		if (cleared)
		{
			textureIds[index] = {};
		}
		return cleared;
	}
	auto assetPath = session.ResolveAsset(id);
	if (!assetPath)
	{
		return Result<void>::Failure(assetPath.error);
	}
	auto kind = ResourceKindForPath(assetPath.value);
	if (!kind || kind.value != ResourceKind::Texture)
	{
		return Result<void>::Failure("Selected resource is not a texture");
	}
	auto texture = session.LoadTexture(id);
	if (!texture)
	{
		return Result<void>::Failure(texture.error);
	}
	auto assigned = material->SetTexture(slot, texture.value);
	if (assigned)
	{
		auto stableReference = material->SetTextureResourceId(slot, id);
		if (!stableReference)
		{
			return stableReference;
		}
		textureIds[index] = id;
	}
	return assigned;
}

Result<void> MaterialDraft::SetTextureProperty(const String& identifier, ResourceId id,
                                               const Shader::ShaderAsset& shader, ResourceSession& session)
{
	if (!id)
	{
		auto removed = material->RemovePropertyOverride(identifier);
		if (removed)
		{
			return {};
		}
		return removed;
	}
	auto assetPath = session.ResolveAsset(id);
	if (!assetPath)
	{
		return Result<void>::Failure(assetPath.error);
	}
	auto kind = ResourceKindForPath(assetPath.value);
	if (!kind || kind.value != ResourceKind::Texture)
	{
		return Result<void>::Failure("Selected resource is not a texture");
	}
	auto texture = session.LoadTexture(id);
	if (!texture)
	{
		return Result<void>::Failure(texture.error);
	}
	return material->SetPropertyValue(identifier, shader, MaterialPropertyValue::Texture2D(id, texture.value));
}

Result<void> MaterialDraft::Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                 const ResourceIndex& index,
                                 const ResourceSession::AssetPathResolver& assetResolver) const
{
	for (const auto& property : material->PropertyOverrides())
	{
		if (property.second.type != Shader::PropertyType::Texture2D || !property.second.textureId)
		{
			continue;
		}
		auto assetPath =
		    assetResolver ? assetResolver(property.second.textureId, 0) : index.ResolveAsset(property.second.textureId);
		if (!assetPath)
		{
			return Result<void>::Failure(assetPath.error);
		}
		auto kind = ResourceKindForPath(assetPath.value);
		if (!kind || kind.value != ResourceKind::Texture)
		{
			return Result<void>::Failure("Material Shader texture property resolves to a non-texture resource");
		}
		auto header = ReadResourceHeader(index.Filesystem(), assetPath.value);
		if (!header || header.value.id != property.second.textureId)
		{
			return Result<void>::Failure("Material Shader texture reference changed on disk");
		}
	}
	for (Ursa::Size slot = 0; slot < textureIds.size(); ++slot)
	{
		if (textureIds[slot])
		{
			auto assetPath = assetResolver ? assetResolver(textureIds[slot], 0) : index.ResolveAsset(textureIds[slot]);
			if (!assetPath)
			{
				return Result<void>::Failure(assetPath.error);
			}
			auto kind = ResourceKindForPath(assetPath.value);
			if (!kind || kind.value != ResourceKind::Texture)
			{
				return Result<void>::Failure("Material texture ID resolves to a non-texture resource");
			}
			auto header = ReadResourceHeader(index.Filesystem(), assetPath.value);
			if (!header || header.value.id != textureIds[slot])
			{
				return Result<void>::Failure("Material texture reference changed on disk");
			}
		}
	}
	return SaveMaterialResource(filesystem, path, id, *material,
	                            [this](MaterialTexturePtr texture)
	                            {
		                            for (const auto& property : material->PropertyOverrides())
		                            {
			                            if (property.second.type == Shader::PropertyType::Texture2D &&
			                                property.second.texture == texture && property.second.textureId)
			                            {
				                            return Result<ResourceId>::Success(property.second.textureId);
			                            }
		                            }
		                            for (Ursa::Size slot = 0; slot < textureIds.size(); ++slot)
		                            {
			                            if (material->Texture(static_cast<MaterialTextureSlot>(slot)) == texture &&
			                                textureIds[slot])
			                            {
				                            return Result<ResourceId>::Success(textureIds[slot]);
			                            }
		                            }
		                            return Result<ResourceId>::Failure("Material texture has no saved resource ID");
	                            });
}

Result<std::unique_ptr<EditorDocument>> NewMaterialDocument(const VirtualPath& path, MaterialType type,
                                                            ProjectWorkspace& workspace)
{
	auto draft = std::make_shared<MaterialDraft>(type);
	auto session = ResourceSession::Open(workspace.Index().Mounts(), workspace.AssetResolver());
	if (!session)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(session.error);
	}
	const ResourceId shaderId =
	    type == MaterialType::Unlit ? Shader::BuiltinUnlitShaderId() : Shader::BuiltinPbrShaderId();
	auto shader = session.value->LoadShader(shaderId);
	if (!shader)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(shader.error);
	}
	auto attached = draft->Material().SetShaderAsset(shader.value);
	if (!attached)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(attached.error);
	}
	auto document = EditorDocument::Create(
	    ResourceKind::Material, ResourceId::Generate(), path,
	    [draft, &workspace](VirtualFileSystem& filesystem, const VirtualPath& target, ResourceId id)
	    { return draft->Save(filesystem, target, id, workspace.Index(), workspace.AssetResolver()); });
	if (document)
	{
		document.value->SetPayload(draft);
		document.value->MarkDirty();
	}
	return document;
}

Result<std::unique_ptr<EditorDocument>> OpenMaterialDocument(const VirtualPath& path, ResourceSession& session,
                                                             ProjectWorkspace& workspace)
{

	auto header = ReadResourceHeader(session.Index().Filesystem(), path);
	if (!header)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(header.error);
	}
	auto draft = MaterialDraft::Load(workspace.Index().Filesystem(), path, session);
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document = EditorDocument::Create(
	    ResourceKind::Material, header.value.id, path,
	    [draft = draft.value, &workspace](VirtualFileSystem& filesystem, const VirtualPath& target, ResourceId id)
	    { return draft->Save(filesystem, target, id, workspace.Index(), workspace.AssetResolver()); });
	if (document)
	{
		document.value->SetPayload(draft.value);
	}
	return document;
}

Result<void> SetShaderMaterialProperty(EditorDocument& document, const String& identifier, MaterialPropertyValue value)
{
	auto draft = document.Payload<MaterialDraft>();
	if (!draft)
	{
		return Result<void>::Failure("Editor document has no material draft");
	}
	auto changed = draft->SetPropertyValue(identifier, std::move(value));
	if (changed)
	{
		document.MarkDirty();
	}
	return changed;
}

Result<void> SetShaderMaterialTexture(EditorDocument& document, const String& identifier, ResourceId textureId,
                                      ResourceSession& session)
{
	auto draft = document.Payload<MaterialDraft>();
	if (!draft || !draft->Material().Shader())
	{
		return Result<void>::Failure("Editor document has no loaded Shader material draft");
	}
	auto changed = draft->SetTextureProperty(identifier, textureId, *draft->Material().Shader(), session);
	if (changed)
	{
		document.MarkDirty();
	}
	return changed;
}

Result<bool> DrawMaterialInspector(EditorDocument& document, EditorDocuments& documents,
                                   const ProjectWorkspace& workspace, String& diagnostic,
                                   const MaterialInspectors* customInspectors, InspectorUI* inspectorUI, bool embedded)
{
	auto draft = document.Payload<MaterialDraft>();
	if (!draft)
	{
		return Result<bool>::Failure("Material document has no material draft");
	}
	auto& material = draft->Material();
	bool changed = false;
	if (!embedded)
	{
		ImGui::Begin("Inspector");
	}
	Icons::Section(Icons::Icon::Material, "Material");
	if (const auto shader = material.Shader())
	{
		ImGui::Text("Shader: %s", shader->name.c_str());
		String customInspectorDiagnostic;
		if (!shader->customEditor.empty())
		{
			if (customInspectors && inspectorUI)
			{
				MaterialInspectorContext context{document, *draft, documents.Session()};
				auto customResult = customInspectors->Draw(shader->customEditor, context, *inspectorUI);
				if (customResult)
				{
					if (customResult.value)
					{
						document.MarkDirty();
					}
					if (!embedded)
					{
						ImGui::End();
					}
					return customResult;
				}
				customInspectorDiagnostic = customResult.error + "; using the Shader property Inspector";
			}
			else
			{
				customInspectorDiagnostic = "Custom Material Inspector '" + shader->customEditor +
				                            "' is unavailable; using the Shader property Inspector";
			}
		}
		for (const Shader::PropertyDeclaration& property : shader->properties)
		{
			if (std::find(property.attributes.begin(), property.attributes.end(), "HideInInspector") !=
			    property.attributes.end())
			{
				continue;
			}
			auto current = material.PropertyValue(property.identifier, *shader);
			if (!current)
			{
				diagnostic = current.error;
				ImGui::TextWrapped("%s", diagnostic.c_str());
				continue;
			}
			const String label = property.displayName.empty() ? property.identifier : property.displayName;
			const auto setValue = [&](MaterialPropertyValue value)
			{
				auto set = SetShaderMaterialProperty(document, property.identifier, std::move(value));
				diagnostic = set ? String{} : set.error;
				changed |= static_cast<bool>(set);
			};
			switch (property.type)
			{
				case Shader::PropertyType::Float:
				{
					Ursa::Float32 value = current.value.floatValue;
					if (ImGui::DragFloat(label.c_str(), &value, 0.01f))
					{
						setValue(MaterialPropertyValue::Float(value));
					}
					break;
				}
				case Shader::PropertyType::Range:
				{
					Ursa::Float32 value = current.value.floatValue;
					if (ImGui::SliderFloat(label.c_str(), &value, property.rangeMinimum, property.rangeMaximum))
					{
						setValue(MaterialPropertyValue::Range(value));
					}
					break;
				}
				case Shader::PropertyType::Int:
				{
					Ursa::Int32 value = current.value.intValue;
					const bool changedValue = property.identifier == "_AlphaMode"
					                              ? ImGui::Combo(label.c_str(), &value, "Opaque\0Mask\0Blend\0")
					                              : ImGui::InputInt(label.c_str(), &value);
					if (changedValue)
					{
						setValue(MaterialPropertyValue::Int(value));
					}
					break;
				}
				case Shader::PropertyType::Color:
				case Shader::PropertyType::Vector:
				{
					Ursa::Float32 value[4];
					std::copy(current.value.vectorValue.begin(), current.value.vectorValue.end(), value);
					const bool hdr = std::find(property.attributes.begin(), property.attributes.end(), "HDR") !=
					                 property.attributes.end();
					const ImGuiColorEditFlags flags = hdr ? ImGuiColorEditFlags_HDR : ImGuiColorEditFlags_None;
					if (ImGui::ColorEdit4(label.c_str(), value, flags))
					{
						const Array<Ursa::Float32, 4> vector{value[0], value[1], value[2], value[3]};
						setValue(property.type == Shader::PropertyType::Color
						             ? MaterialPropertyValue::ColorValue({value[0], value[1], value[2], value[3]})
						             : MaterialPropertyValue::VectorValue(vector));
					}
					break;
				}
				case Shader::PropertyType::Texture2D:
				{
					const bool overridden =
					    material.PropertyOverrides().find(property.identifier) != material.PropertyOverrides().end();
					String preview = current.value.textureId ? current.value.textureId.ToString()
					                 : overridden            ? String("Missing texture")
					                                         : "Default (" + current.value.textureDefault + ")";
					if (ImGui::BeginCombo(label.c_str(), preview.c_str()))
					{
						if (Icons::Selectable(Icons::Icon::Revert, "Use Shader Default", !overridden))
						{
							auto set = SetShaderMaterialTexture(document, property.identifier, {}, documents.Session());
							diagnostic = set ? String{} : set.error;
							changed |= static_cast<bool>(set);
						}
						for (const ResourceEntry& entry : workspace.Resources())
						{
							if (entry.location.kind != ResourceKind::Texture)
							{
								continue;
							}
							ImGui::PushID(entry.id.ToString().c_str());
							const String textureName = entry.location.path.FileName();
							if (Icons::Selectable(Icons::Icon::Texture, textureName.c_str(),
							                      current.value.textureId == entry.id))
							{
								auto set = SetShaderMaterialTexture(document, property.identifier, entry.id,
								                                    documents.Session());
								diagnostic = set ? String{} : set.error;
								changed |= static_cast<bool>(set);
							}
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
					break;
				}
			}
		}
		if (!customInspectorDiagnostic.empty())
		{
			ImGui::TextWrapped("%s", customInspectorDiagnostic.c_str());
		}
		if (!diagnostic.empty())
		{
			ImGui::TextWrapped("%s", diagnostic.c_str());
		}
		if (!embedded)
		{
			ImGui::End();
		}
		if (changed)
		{
			document.MarkDirty();
		}
		return Result<bool>::Success(changed);
	}
	ImGui::TextUnformatted(material.Type() == MaterialType::Unlit ? "Unlit" : "PBR Metallic Roughness");
	auto params = material.Parameters();
	Ursa::Float32 color[4]{params.baseColor.red, params.baseColor.green, params.baseColor.blue, params.baseColor.alpha};
	if (ImGui::ColorEdit4("Base color", color))
	{
		auto set = material.SetBaseColor({color[0], color[1], color[2], color[3]});
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	if (material.Type() == MaterialType::PbrMetallicRoughness)
	{
		Ursa::Float32 metallic = params.metallic;
		Ursa::Float32 roughness = params.roughness;
		Ursa::Float32 normalScale = params.normalScale;
		Ursa::Float32 occlusion = params.occlusionStrength;
		if (ImGui::SliderFloat("Metallic", &metallic, 0.f, 1.f))
		{
			auto set = material.SetMetallic(metallic);
			diagnostic = set ? String{} : set.error;
			changed |= static_cast<bool>(set);
		}
		if (ImGui::SliderFloat("Roughness", &roughness, 0.f, 1.f))
		{
			auto set = material.SetRoughness(roughness);
			diagnostic = set ? String{} : set.error;
			changed |= static_cast<bool>(set);
		}
		if (ImGui::DragFloat("Normal scale", &normalScale, 0.02f, 0.f, 10.f))
		{
			auto set = material.SetNormalScale(normalScale);
			diagnostic = set ? String{} : set.error;
			changed |= static_cast<bool>(set);
		}
		if (ImGui::SliderFloat("Occlusion", &occlusion, 0.f, 1.f))
		{
			auto set = material.SetOcclusionStrength(occlusion);
			diagnostic = set ? String{} : set.error;
			changed |= static_cast<bool>(set);
		}
		Ursa::Float32 emissive[4]{params.emissive.red, params.emissive.green, params.emissive.blue,
		                          params.emissive.alpha};
		if (ImGui::ColorEdit4("Emissive", emissive))
		{
			auto set = material.SetEmissive({emissive[0], emissive[1], emissive[2], emissive[3]});
			diagnostic = set ? String{} : set.error;
			changed |= static_cast<bool>(set);
		}
	}
	int mode = static_cast<int>(params.alphaMode);
	Ursa::Float32 cutoff = params.alphaCutoff;
	if (ImGui::Combo("Alpha mode", &mode, "Opaque\0Mask\0Blend\0"))
	{
		auto set = material.SetAlphaMode(static_cast<AlphaMode>(mode), cutoff);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	if (ImGui::SliderFloat("Alpha cutoff", &cutoff, 0.f, 1.f))
	{
		auto set = material.SetAlphaMode(static_cast<AlphaMode>(mode), cutoff);
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	bool linear = params.sampler.linear;
	bool repeat = params.sampler.repeat;
	const bool linearChanged = ImGui::Checkbox("Linear filtering", &linear);
	const bool repeatChanged = ImGui::Checkbox("Repeat", &repeat);
	if (linearChanged || repeatChanged)
	{
		auto set = material.SetSampler({linear, repeat});
		diagnostic = set ? String{} : set.error;
		changed |= static_cast<bool>(set);
	}
	for (Ursa::Size slot = 0; slot < slotNames.size(); ++slot)
	{
		if (!AllowedSlot(material.Type(), slot))
		{
			continue;
		}
		const auto kind = static_cast<MaterialTextureSlot>(slot);
		const auto selected = draft->TextureId(kind);
		String preview = selected ? selected.ToString() : String("None");
		if (ImGui::BeginCombo(slotNames[slot], preview.c_str()))
		{
			if (Icons::Selectable(Icons::Icon::Close, "None", !selected))
			{
				auto set = draft->SetTextureId(kind, {}, documents.Session());
				diagnostic = set ? String{} : set.error;
				changed |= static_cast<bool>(set);
			}
			Set<String> shownTextureIds;
			for (const auto& entry : workspace.Resources())
			{
				if (entry.location.kind != ResourceKind::Texture)
				{
					continue;
				}
				shownTextureIds.insert(entry.id.ToString());
				ImGui::PushID(entry.id.ToString().c_str());
				const auto label = entry.location.path.FileName();
				if (Icons::Selectable(Icons::Icon::Texture, label.c_str(), selected == entry.id))
				{
					auto set = draft->SetTextureId(kind, entry.id, documents.Session());
					diagnostic = set ? String{} : set.error;
					changed |= static_cast<bool>(set);
				}
				ImGui::PopID();
			}
			if (const AssetDatabase* database = workspace.Database())
			{
				for (const AssetRecord& asset : database->Assets())
				{
					if (asset.metadata.folder || shownTextureIds.count(asset.metadata.guid.ToString()))
					{
						continue;
					}
					auto resolvedPath = database->Resolve(asset.metadata.guid, 0);
					if (!resolvedPath)
					{
						continue;
					}
					auto resourceKind = ResourceKindForPath(resolvedPath.value);
					if (!resourceKind || resourceKind.value != ResourceKind::Texture)
					{
						continue;
					}
					const String id = asset.metadata.guid.ToString();
					ImGui::PushID(id.c_str());
					const String label = asset.path.FileName();
					if (Icons::Selectable(Icons::Icon::Texture, label.c_str(), selected == asset.metadata.guid))
					{
						auto set = draft->SetTextureId(kind, asset.metadata.guid, documents.Session());
						diagnostic = set ? String{} : set.error;
						changed |= static_cast<bool>(set);
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	if (!embedded)
	{
		ImGui::End();
	}
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
