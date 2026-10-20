#include <Assets/AssetPreviewContent.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/TextureDraft.hpp>
#include <Assets/TextureImporter.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Take(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

bool ValidBounds(const Bounds& bounds)
{
	for (const auto value :
	     {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y, bounds.maximum.z})
	{
		if (!std::isfinite(value) || std::abs(value) > 1e20f)
		{
			return false;
		}
	}
	return bounds.minimum.x <= bounds.maximum.x && bounds.minimum.y <= bounds.maximum.y &&
	       bounds.minimum.z <= bounds.maximum.z;
}
} // namespace

Result<std::unique_ptr<AssetPreviewContent>> AssetPreviewContent::Load(const AssetPreviewTarget& selected,
                                                                       ProjectWorkspace& workspace,
                                                                       ComponentRegistry& registry,
                                                                       const AssetPreviewTextureInput* textureInput)
{
	if (!selected.Supported())
	{
		return Result<std::unique_ptr<AssetPreviewContent>>::Failure("This asset has no preview");
	}
	try
	{
		auto result = std::make_unique<AssetPreviewContent>();
		result->target = selected;
		result->resources = Take(ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver()));
		const auto& files = workspace.Index().Filesystem();
		const auto guid = selected.identity.guid;
		const auto localId = selected.identity.localFileId;
		const auto artifactVersion = result->resources->ResolveAsset(guid, localId);
		if (artifactVersion && artifactVersion.value.scheme == editorLibraryScheme)
		{
			// Library artifact addresses contain the published content fingerprint.
			// CurrentImport().fingerprint describes the requested source/settings instead.
			result->importVersion = artifactVersion.value.ToString();
		}
		if (auto* database = workspace.Database(); database && selected.path.scheme == projectScheme)
		{
			auto metadata = database->Metadata(selected.path);
			if (metadata && !metadata.value.importer.empty())
			{
				auto current = database->CurrentImport(selected.path);
				if (!current || !current.value.upToDate || current.value.settings != metadata.value.importSettings)
				{
					result->diagnostic =
					    "Last successful import; source, settings or dependencies have unapplied changes.";
				}
			}
		}
		if (selected.kind == AssetPreviewKind::Material)
		{
			auto draft = selected.draft ? selected.draft->Payload<MaterialDraft>() : nullptr;
			result->material = draft ? draft->MaterialPtr() : Take(result->resources->LoadMaterial(guid, localId));
		}
		else if (selected.kind == AssetPreviewKind::Texture)
		{
			auto draft = selected.draft ? selected.draft->Payload<TextureDraft>() : nullptr;
			if (draft)
			{
				result->texture = Take(MaterialTexture::Create(draft->GetWidth(), draft->GetHeight(),
				                                               draft->GetColorSpace(), draft->GetPixels()));
			}
			else if (textureInput && textureInput->path == selected.path && !localId && textureInput->imported)
			{
				result->texture = textureInput->imported;
				result->sourceTexture = textureInput->source;
			}
			else
			{
				auto imported = result->resources->LoadTexture(guid, localId);
				if (imported)
				{
					result->texture = std::move(imported.value);
				}
				else
				{
					result->sourceDiagnostic = imported.error;
				}
			}
			if (!localId && !draft && !result->sourceTexture)
			{
				auto source = TextureImporter::DecodeSourceImage(files, selected.path);
				if (source)
				{
					result->sourceTexture = std::move(source.value);
				}
				else
				{
					if (!result->texture)
					{
						throw std::runtime_error(
						    ("Imported preview: " + result->sourceDiagnostic + "; source: " + source.error).c_str());
					}
					result->sourceDiagnostic = source.error;
				}
			}
			if (!result->texture && !result->sourceTexture)
			{
				throw std::runtime_error("Texture has no readable source or imported result");
			}
		}
		else if (selected.kind == AssetPreviewKind::Model)
		{
			const auto artifact = Take(result->resources->ResolveAsset(guid));
			const auto bytes = Take(result->resources->Index().Filesystem().Read(artifact));
			const auto data = nlohmann::json::from_cbor(bytes.begin(), bytes.end());
			result->model = Take(SceneDraft::Create(ResourceKind::Scene, registry, *result->resources));
			result->root = Take(result->model->InstantiateModel(guid, data, false));
			const auto remember = [&](auto&& self, const GameObject::Ptr& object) -> void
			{
				result->bindTransforms.emplace_back(object, Take(object->GetLocalTransform()));
				for (const auto& child : object->Children())
				{
					self(self, child);
				}
			};
			remember(remember, result->root);
			Take(result->model->GetScene().CommitChanges());
			if (data.contains("skeletonLocalFileIds") && !data["skeletonLocalFileIds"].empty())
			{
				result->skeletonId = data["skeletonLocalFileIds"][0].get<UInt64>();
				for (const auto& clipId : data.value("animationClipLocalFileIds", nlohmann::json::array()))
				{
					const auto clip = Take(result->resources->LoadAnimationClip(guid, clipId.get<UInt64>()));
					result->clips.push_back({clip->localFileId, clip->name, clip->durationSeconds});
				}
			}
		}
		else if (selected.kind == AssetPreviewKind::Mesh)
		{
			result->mesh = Take(result->resources->LoadMesh(guid, localId));
			result->scene = std::make_unique<Scene>();
			result->root = Take(result->scene->CreateGameObject("Mesh preview"));
			RendererComponent* renderer = nullptr;
			if (!result->mesh->Snapshot()->data.skinInfluences.empty())
			{
				auto* skinned = Take(result->root->AddComponent<SkinnedMeshRendererComponent>());
				Take(skinned->SetSharedMesh(result->mesh));
				renderer = skinned;
			}
			else
			{
				Take(Take(result->root->AddComponent<MeshFilterComponent>())->SetSharedMesh(result->mesh));
				renderer = Take(result->root->AddComponent<MeshRendererComponent>());
			}
			const auto neutral = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
			Vector<std::shared_ptr<MaterialInstance>> materials(result->mesh->Snapshot()->data.subMeshes.size(),
			                                                    neutral);
			Take(renderer->SetSharedMaterials(std::move(materials)));
			Take(result->scene->CommitChanges());
		}
		else if (selected.kind == AssetPreviewKind::Prefab)
		{
			ComponentLoadContext context(*result->resources);
			auto draft = selected.draft ? selected.draft->Payload<SceneDraft>() : nullptr;
			if (draft)
			{
				const auto encoded = Take(EncodePrefabAssetDocument(
				    selected.path.relative, *draft->GetPrefabAsset(), registry,
				    [draft](const std::shared_ptr<MaterialInstance>& value) { return draft->MaterialId(value); }));
				auto decoded = Take(DecodePrefabAssetDocument(selected.path.ToString(), encoded, registry, context));
				result->scene = std::make_unique<Scene>();
				result->root = Take(decoded.Instantiate(*result->scene, registry, context));
			}
			else
			{
				auto asset = Take(LoadPrefabAssetResource(files, selected.path, registry, context));
				result->scene = std::make_unique<Scene>();
				result->root = Take(asset.Instantiate(*result->scene, registry, context));
			}
			Take(result->scene->CommitChanges());
		}
		if (auto* previewScene = result->RenderScene())
		{
			Take(previewScene->SetAmbientLight({1, 1, 1, 1}, 0.4f));
			auto lightObject = Take(previewScene->CreateGameObject("Preview light"));
			Take(Take(lightObject->AddComponent<DirectionalLightComponent>())->SetIntensity(2.f));
			Take(previewScene->CommitChanges());
		}
		result->target.draft = nullptr;
		return Result<std::unique_ptr<AssetPreviewContent>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<AssetPreviewContent>>::Failure(ToUrsaString(error.what()));
	}
}

Scene* AssetPreviewContent::RenderScene() const
{
	return model ? &model->GetScene() : scene.get();
}

Result<void> AssetPreviewContent::SelectClip(UInt64 localFileId)
{
	if (!root || !model)
	{
		return Result<void>::Failure("This preview has no animated model");
	}
	if (!localFileId)
	{
		if (auto* animator = root->FindComponent<AnimatorComponent>())
		{
			auto removed = root->RemoveComponent(animator);
			if (!removed)
			{
				return removed;
			}
		}
		selectedClip = 0;
		for (const auto& [object, transform] : bindTransforms)
		{
			auto restored = object->SetLocalTransform(transform);
			if (!restored)
			{
				return restored;
			}
		}
		return model->GetScene().CommitChanges();
	}
	auto clip = resources->LoadAnimationClip(target.identity.guid, localFileId);
	if (!clip)
	{
		return Result<void>::Failure(clip.error);
	}
	if (!std::isfinite(clip.value->durationSeconds) || clip.value->durationSeconds <= 0)
	{
		return Result<void>::Failure("Clip has no finite positive duration");
	}
	auto* animator = root->FindComponent<AnimatorComponent>();
	if (!animator)
	{
		auto created = root->AddComponent<AnimatorComponent>();
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		animator = created.value;
	}
	auto assigned = animator->SetAnimation(target.identity.guid, skeletonId, localFileId, *resources);
	if (!assigned)
	{
		return assigned;
	}
	animator->Stop();
	animator->SetLoop(false);
	selectedClip = localFileId;
	return model->GetScene().CommitChanges();
}

Result<void> AssetPreviewContent::Sample(Float64 time)
{
	auto* animator = root ? root->FindComponent<AnimatorComponent>() : nullptr;
	return animator ? animator->SetTime(time) : Result<void>::Failure("Select an animation clip before sampling");
}

Result<Bounds> AssetPreviewContent::GeometryBounds() const
{
	if (!RenderScene())
	{
		return Result<Bounds>::Failure("Preview has no geometry");
	}
	Camera camera;
	auto captured = RenderScene()->CollectPreviewRenderData(camera, 1.f);
	if (!captured)
	{
		return Result<Bounds>::Failure(captured.error);
	}
	Bounds bounds;
	for (const auto& object : captured.value.objects)
	{
		if (object.worldBounds.empty)
		{
			continue;
		}
		if (!ValidBounds(object.worldBounds))
		{
			return Result<Bounds>::Failure("Preview geometry exceeds the finite camera range");
		}
		if (bounds.empty)
		{
			bounds = object.worldBounds;
		}
		else
		{
			bounds.minimum = {std::min(bounds.minimum.x, object.worldBounds.minimum.x),
			                  std::min(bounds.minimum.y, object.worldBounds.minimum.y),
			                  std::min(bounds.minimum.z, object.worldBounds.minimum.z)};
			bounds.maximum = {std::max(bounds.maximum.x, object.worldBounds.maximum.x),
			                  std::max(bounds.maximum.y, object.worldBounds.maximum.y),
			                  std::max(bounds.maximum.z, object.worldBounds.maximum.z)};
		}
	}
	return Result<Bounds>::Success(bounds);
}
} // namespace Ursa::Editor
