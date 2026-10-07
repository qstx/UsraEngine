#include <Core/Types.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Core/StringInterop.hpp>
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
	if (document.value.contains("textures") && document.value["textures"].is_object())
	{
		for (Ursa::Size index = 0; index < fieldNames.size(); ++index)
		{
			if (document.value["textures"].contains(fieldNames[index]))
			{
				auto id = ResourceId::Parse(document.value["textures"][fieldNames[index]].get<String>());
				if (!id)
				{
					return Result<std::shared_ptr<MaterialDraft>>::Failure(id.error);
				}
				draft->textureIds[index] = id.value;
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
	auto location = session.Index().Resolve(id, "material editor");
	if (!location)
	{
		return Result<void>::Failure(location.error);
	}
	if (location.value.kind != ResourceKind::Texture)
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
		textureIds[index] = id;
	}
	return assigned;
}

Result<void> MaterialDraft::Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                 const ResourceIndex& index) const
{
	for (Ursa::Size slot = 0; slot < textureIds.size(); ++slot)
	{
		if (textureIds[slot])
		{
			auto location = index.Resolve(textureIds[slot], "material " + id.ToString());
			if (!location)
			{
				return Result<void>::Failure(location.error);
			}
			if (location.value.kind != ResourceKind::Texture)
			{
				return Result<void>::Failure("Material texture ID resolves to a non-texture resource");
			}
			auto header = ReadResourceHeader(index.Filesystem(), location.value.path);
			if (!header || header.value.id != textureIds[slot])
			{
				return Result<void>::Failure("Material texture reference changed on disk");
			}
		}
	}
	return SaveMaterialResource(filesystem, path, id, *material,
	                            [this](MaterialTexturePtr texture)
	                            {
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
	auto document = EditorDocument::Create(
	    ResourceKind::Material, ResourceId::Generate(), path,
	    [draft, &workspace](VirtualFileSystem& filesystem, const VirtualPath& target, ResourceId id)
	    { return draft->Save(filesystem, target, id, workspace.Index()); });
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
	    { return draft->Save(filesystem, target, id, workspace.Index()); });
	if (document)
	{
		document.value->SetPayload(draft.value);
	}
	return document;
}

Result<bool> DrawMaterialInspector(EditorDocument& document, EditorDocuments& documents,
                                   const ProjectWorkspace& workspace, String& diagnostic)
{
	auto draft = document.Payload<MaterialDraft>();
	if (!draft)
	{
		return Result<bool>::Failure("Material document has no material draft");
	}
	auto& material = draft->Material();
	bool changed = false;
	ImGui::Begin("Inspector");
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
			if (ImGui::Selectable("None", !selected))
			{
				auto set = draft->SetTextureId(kind, {}, documents.Session());
				diagnostic = set ? String{} : set.error;
				changed |= static_cast<bool>(set);
			}
			for (const auto& entry : workspace.Resources())
			{
				if (entry.location.kind != ResourceKind::Texture)
				{
					continue;
				}
				ImGui::PushID(entry.id.ToString().c_str());
				const auto label = entry.location.path.FileName();
				if (ImGui::Selectable(label.c_str(), selected == entry.id))
				{
					auto set = draft->SetTextureId(kind, entry.id, documents.Session());
					diagnostic = set ? String{} : set.error;
					changed |= static_cast<bool>(set);
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	ImGui::End();
	if (changed)
	{
		document.MarkDirty();
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
