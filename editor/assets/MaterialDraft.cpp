#include <assets/MaterialDraft.hpp>
#include <serialization/JsonResource.hpp>
#include <serialization/JsonInterop.hpp>
#include <core/StringInterop.hpp>
#include <imgui.h>
#include <algorithm>

namespace ursa::editor {
namespace {
constexpr Array<const char *, 5> slotNames{"Base Color", "Metallic Roughness", "Normal", "Occlusion", "Emissive"};
constexpr Array<const char *, 5> fieldNames{"baseColor", "metallicRoughness", "normal", "occlusion", "emissive"};

bool allowedSlot(MaterialType type, size_t index) {
    return type == MaterialType::PbrMetallicRoughness || index == 0;
}
} // namespace

Result<std::shared_ptr<MaterialDraft>> MaterialDraft::load(const std::filesystem::path &path,
                                                            ResourceSession &session) {
    auto document = readJsonResource(path, ResourceKind::Material);
    if (!document)
        return Result<std::shared_ptr<MaterialDraft>>::failure(document.error);
    auto loaded = loadMaterialResource(path, [&session](ResourceId id) { return session.loadTexture(id); });
    if (!loaded)
        return Result<std::shared_ptr<MaterialDraft>>::failure(loaded.error);
    auto draft = std::make_shared<MaterialDraft>(loaded.value.material->type());
    draft->material_ = std::move(loaded.value.material);
    if (document.value.contains("textures") && document.value["textures"].is_object())
        for (size_t index = 0; index < fieldNames.size(); ++index)
            if (document.value["textures"].contains(fieldNames[index])) {
                auto id = ResourceId::parse(document.value["textures"][fieldNames[index]].get<String>());
                if (!id)
                    return Result<std::shared_ptr<MaterialDraft>>::failure(id.error);
                draft->textureIds_[index] = id.value;
            }
    return Result<std::shared_ptr<MaterialDraft>>::success(std::move(draft));
}

ResourceId MaterialDraft::textureId(MaterialTextureSlot slot) const {
    const size_t index = static_cast<size_t>(slot);
    return index < textureIds_.size() ? textureIds_[index] : ResourceId{};
}

Result<void> MaterialDraft::setTextureId(MaterialTextureSlot slot, ResourceId id, ResourceSession &session) {
    const size_t index = static_cast<size_t>(slot);
    if (index >= textureIds_.size() || !allowedSlot(material_->type(), index))
        return Result<void>::failure("Texture slot is not valid for this material type");
    if (!id) {
        auto cleared = material_->setTexture(slot, {});
        if (cleared)
            textureIds_[index] = {};
        return cleared;
    }
    auto location = session.index().resolve(id, "material editor");
    if (!location)
        return Result<void>::failure(location.error);
    if (location.value.kind != ResourceKind::Texture)
        return Result<void>::failure("Selected resource is not a texture");
    auto texture = session.loadTexture(id);
    if (!texture)
        return Result<void>::failure(texture.error);
    auto assigned = material_->setTexture(slot, texture.value);
    if (assigned)
        textureIds_[index] = id;
    return assigned;
}

Result<void> MaterialDraft::save(const std::filesystem::path &path, ResourceId id, const ResourceIndex &index) const {
    for (size_t slot = 0; slot < textureIds_.size(); ++slot)
        if (textureIds_[slot]) {
            auto location = index.resolve(textureIds_[slot], "material " + id.toString());
            if (!location)
                return Result<void>::failure(location.error);
            if (location.value.kind != ResourceKind::Texture)
                return Result<void>::failure("Material texture ID resolves to a non-texture resource");
            auto header = readResourceHeader(index.filesystem(), location.value.path);
            if (!header || header.value.id != textureIds_[slot])
                return Result<void>::failure("Material texture reference changed on disk");
        }
    return saveMaterialResource(path, id, *material_, [this](MaterialTexturePtr texture) {
        for (size_t slot = 0; slot < textureIds_.size(); ++slot)
            if (material_->texture(static_cast<MaterialTextureSlot>(slot)) == texture && textureIds_[slot])
                return Result<ResourceId>::success(textureIds_[slot]);
        return Result<ResourceId>::failure("Material texture has no saved resource ID");
    });
}

Result<std::unique_ptr<EditorDocument>> newMaterialDocument(const VirtualPath &path,
                                                              MaterialType type, ProjectWorkspace &workspace) {
    auto draft = std::make_shared<MaterialDraft>(type);
    auto document = EditorDocument::create(ResourceKind::Material, ResourceId::generate(), path,
                                            [draft, &workspace](const std::filesystem::path &target, ResourceId id) {
                                                return draft->save(target, id, workspace.index());
                                            });
    if (document) {
        document.value->setPayload(draft);
        document.value->markDirty();
    }
    return document;
}

Result<std::unique_ptr<EditorDocument>> openMaterialDocument(const VirtualPath &path,
                                                               ResourceSession &session, ProjectWorkspace &workspace) {
    auto host = workspace.index().locate(ResourceLocation{ResourceKind::Material, path});
    if (!host)
        return Result<std::unique_ptr<EditorDocument>>::failure(host.error);
    auto header = readResourceHeader(host.value);
    if (!header)
        return Result<std::unique_ptr<EditorDocument>>::failure(header.error);
    auto draft = MaterialDraft::load(host.value, session);
    if (!draft)
        return Result<std::unique_ptr<EditorDocument>>::failure(draft.error);
    auto document = EditorDocument::create(ResourceKind::Material, header.value.id, path,
                                            [draft = draft.value, &workspace](const std::filesystem::path &target,
                                                                              ResourceId id) {
                                                return draft->save(target, id, workspace.index());
                                            });
    if (document)
        document.value->setPayload(draft.value);
    return document;
}

Result<bool> drawMaterialInspector(EditorDocument &document, EditorDocuments &documents,
                                    const ProjectWorkspace &workspace, String &diagnostic) {
    auto draft = document.payload<MaterialDraft>();
    if (!draft)
        return Result<bool>::failure("Material document has no material draft");
    auto &material = draft->material();
    bool changed = false;
    ImGui::Begin("Inspector");
    ImGui::TextUnformatted(material.type() == MaterialType::Unlit ? "Unlit" : "PBR Metallic Roughness");
    auto params = material.parameters();
    float color[4]{params.baseColor.red, params.baseColor.green, params.baseColor.blue, params.baseColor.alpha};
    if (ImGui::ColorEdit4("Base color", color)) {
        auto set = material.setBaseColor({color[0], color[1], color[2], color[3]});
        diagnostic = set ? String{} : set.error;
        changed |= static_cast<bool>(set);
    }
    if (material.type() == MaterialType::PbrMetallicRoughness) {
        float metallic = params.metallic;
        float roughness = params.roughness;
        float normalScale = params.normalScale;
        float occlusion = params.occlusionStrength;
        if (ImGui::SliderFloat("Metallic", &metallic, 0.f, 1.f)) {
            auto set = material.setMetallic(metallic);
            diagnostic = set ? String{} : set.error;
            changed |= static_cast<bool>(set);
        }
        if (ImGui::SliderFloat("Roughness", &roughness, 0.f, 1.f)) {
            auto set = material.setRoughness(roughness);
            diagnostic = set ? String{} : set.error;
            changed |= static_cast<bool>(set);
        }
        if (ImGui::DragFloat("Normal scale", &normalScale, 0.02f, 0.f, 10.f)) {
            auto set = material.setNormalScale(normalScale);
            diagnostic = set ? String{} : set.error;
            changed |= static_cast<bool>(set);
        }
        if (ImGui::SliderFloat("Occlusion", &occlusion, 0.f, 1.f)) {
            auto set = material.setOcclusionStrength(occlusion);
            diagnostic = set ? String{} : set.error;
            changed |= static_cast<bool>(set);
        }
        float emissive[4]{params.emissive.red, params.emissive.green, params.emissive.blue, params.emissive.alpha};
        if (ImGui::ColorEdit4("Emissive", emissive)) {
            auto set = material.setEmissive({emissive[0], emissive[1], emissive[2], emissive[3]});
            diagnostic = set ? String{} : set.error;
            changed |= static_cast<bool>(set);
        }
    }
    int mode = static_cast<int>(params.alphaMode);
    float cutoff = params.alphaCutoff;
    if (ImGui::Combo("Alpha mode", &mode, "Opaque\0Mask\0Blend\0")) {
        auto set = material.setAlphaMode(static_cast<AlphaMode>(mode), cutoff);
        diagnostic = set ? String{} : set.error;
        changed |= static_cast<bool>(set);
    }
    if (ImGui::SliderFloat("Alpha cutoff", &cutoff, 0.f, 1.f)) {
        auto set = material.setAlphaMode(static_cast<AlphaMode>(mode), cutoff);
        diagnostic = set ? String{} : set.error;
        changed |= static_cast<bool>(set);
    }
    bool linear = params.sampler.linear;
    bool repeat = params.sampler.repeat;
    const bool linearChanged = ImGui::Checkbox("Linear filtering", &linear);
    const bool repeatChanged = ImGui::Checkbox("Repeat", &repeat);
    if (linearChanged || repeatChanged) {
        auto set = material.setSampler({linear, repeat});
        diagnostic = set ? String{} : set.error;
        changed |= static_cast<bool>(set);
    }
    for (size_t slot = 0; slot < slotNames.size(); ++slot) {
        if (!allowedSlot(material.type(), slot))
            continue;
        const auto kind = static_cast<MaterialTextureSlot>(slot);
        const auto selected = draft->textureId(kind);
        String preview = selected ? selected.toString() : String("None");
        if (ImGui::BeginCombo(slotNames[slot], preview.c_str())) {
            if (ImGui::Selectable("None", !selected)) {
                auto set = draft->setTextureId(kind, {}, documents.session());
                diagnostic = set ? String{} : set.error;
                changed |= static_cast<bool>(set);
            }
            for (const auto &entry : workspace.resources()) {
                if (entry.location.kind != ResourceKind::Texture)
                    continue;
                ImGui::PushID(entry.id.toString().c_str());
                const auto label = entry.location.path.fileName();
                if (ImGui::Selectable(label.c_str(), selected == entry.id)) {
                    auto set = draft->setTextureId(kind, entry.id, documents.session());
                    diagnostic = set ? String{} : set.error;
                    changed |= static_cast<bool>(set);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    if (!diagnostic.empty())
        ImGui::TextWrapped("%s", diagnostic.c_str());
    ImGui::End();
    if (changed)
        document.markDirty();
    return Result<bool>::success(changed);
}
} // namespace ursa::editor
