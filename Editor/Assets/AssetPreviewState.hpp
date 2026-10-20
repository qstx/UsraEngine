#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Workspace/EditorSelectionContext.hpp>

namespace Ursa::Editor
{
enum class AssetPreviewKind
{
	None,
	Material,
	Texture,
	Model,
	Mesh,
	Prefab
};

struct AssetPreviewTarget
{
	AssetPreviewKind kind = AssetPreviewKind::None;
	AssetReference identity;
	VirtualPath path;
	UInt64 contentVersion = 0;
	UInt64 pipelineVersion = 0;
	ResourceId projectSession;
	const EditorDocument* draft = nullptr;

	bool Supported() const;
	bool SameIdentity(const AssetPreviewTarget& other) const;
};

AssetPreviewTarget ResolveAssetPreviewTarget(const EditorSelectionContext& selection, const ProjectBrowserEntry* entry,
                                             const EditorDocument* document, UInt64 contentVersion,
                                             UInt64 localFileId = 0, std::optional<ResourceKind> subAssetKind = {});

// A generation token prevents completion for an earlier selection replacing the current result.
class AssetPreviewState final
{
public:
	bool Select(AssetPreviewTarget selected);
	bool Complete(UInt64 generation, const String& importVersion = {});
	bool Fail(UInt64 generation, String message);
	void Invalidate();

	const AssetPreviewTarget& Target() const
	{
		return target;
	}

	UInt64 Generation() const
	{
		return generation;
	}

	bool NeedsRender() const
	{
		return needsRender;
	}

	bool HasImage() const
	{
		return hasImage;
	}

	const AssetPreviewTarget& DisplayedTarget() const
	{
		return displayedTarget;
	}

	const String& DisplayedImportVersion() const
	{
		return displayedImportVersion;
	}

	bool IsStale() const
	{
		return hasImage && (displayedGeneration != generation || !diagnostic.empty());
	}

	const String& Diagnostic() const
	{
		return diagnostic;
	}

private:
	AssetPreviewTarget target;
	AssetPreviewTarget displayedTarget;
	String displayedImportVersion;
	UInt64 generation = 0;
	UInt64 displayedGeneration = 0;
	bool needsRender = false;
	bool hasImage = false;
	String diagnostic;
};

// Uses explicit frame deltas and never advances the authoring or runtime Scene.
class AssetPreviewClock final
{
public:
	Result<void> SetDuration(Float64 seconds);
	Result<void> Seek(Float64 seconds);
	Result<void> SetSpeed(Float64 multiplier);
	Result<void> Play();

	void Pause()
	{
		playing = false;
	}

	void SetLoop(bool value)
	{
		loop = value;
	}

	Result<bool> Advance(Float64 deltaSeconds, bool visible);

	Float64 Time() const
	{
		return time;
	}

	Float64 Duration() const
	{
		return duration;
	}

	Float64 Speed() const
	{
		return speed;
	}

	bool Playing() const
	{
		return playing;
	}

	bool Loop() const
	{
		return loop;
	}

private:
	Float64 time = 0;
	Float64 duration = 0;
	Float64 speed = 1;
	bool playing = false;
	bool loop = true;
	bool wasVisible = false;
};
} // namespace Ursa::Editor
