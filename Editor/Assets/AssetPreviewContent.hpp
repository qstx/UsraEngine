#pragma once

#include <Assets/AssetPreviewState.hpp>
#include <Assets/AssetPreviewTexture.hpp>
#include <Assets/SceneDraft.hpp>
#include <Animation/AnimatorComponent.hpp>

namespace Ursa::Editor
{
struct AssetPreviewClip
{
	UInt64 localFileId = 0;
	String name;
	Float64 duration = 0;
};

// Owns only private preview objects. No document switching, saving, or lifecycle synchronization.
class AssetPreviewContent final
{
public:
	static Result<std::unique_ptr<AssetPreviewContent>> Load(const AssetPreviewTarget& target,
	                                                         ProjectWorkspace& workspace, ComponentRegistry& registry,
	                                                         const AssetPreviewTextureInput* textureInput = nullptr);
	Result<void> SelectClip(UInt64 localFileId);
	Result<void> Sample(Float64 time);
	Scene* RenderScene() const;
	Result<Bounds> GeometryBounds() const;

	const AssetPreviewTarget& Target() const
	{
		return target;
	}

	const std::shared_ptr<MaterialInstance>& Material() const
	{
		return material;
	}

	const MaterialTexturePtr& Texture() const
	{
		return texture;
	}

	const MaterialTexturePtr& SourceTexture() const
	{
		return sourceTexture;
	}

	const String& SourceDiagnostic() const
	{
		return sourceDiagnostic;
	}

	const String& Diagnostic() const
	{
		return diagnostic;
	}

	const String& ImportVersion() const
	{
		return importVersion;
	}

	const Vector<AssetPreviewClip>& Clips() const
	{
		return clips;
	}

	UInt64 SelectedClip() const
	{
		return selectedClip;
	}

	ResourceSession& Resources() const
	{
		return *resources;
	}

	MeshPtr MeshAsset() const
	{
		return mesh;
	}

private:
	AssetPreviewTarget target;
	std::unique_ptr<ResourceSession> resources;
	std::shared_ptr<MaterialInstance> material;
	MaterialTexturePtr texture;
	MaterialTexturePtr sourceTexture;
	String sourceDiagnostic;
	String diagnostic;
	String importVersion;
	MeshPtr mesh;
	std::shared_ptr<SceneDraft> model;
	std::unique_ptr<Scene> scene;
	GameObject::Ptr root;
	Vector<AssetPreviewClip> clips;
	UInt64 skeletonId = 0;
	UInt64 selectedClip = 0;
	Vector<std::pair<GameObject::Ptr, LocalTransform>> bindTransforms;
};
} // namespace Ursa::Editor
