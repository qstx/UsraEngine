#include <Assets/AssetPreviewState.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Ursa::Editor
{
namespace
{
AssetPreviewKind PreviewKind(std::optional<ResourceKind> kind, const VirtualPath& path)
{
	if (kind)
	{
		switch (*kind)
		{
			case ResourceKind::Material:
				return AssetPreviewKind::Material;
			case ResourceKind::Texture:
				return AssetPreviewKind::Texture;
			case ResourceKind::Mesh:
				return AssetPreviewKind::Mesh;
			case ResourceKind::Prefab:
				return AssetPreviewKind::Prefab;
			default:
				return AssetPreviewKind::None;
		}
	}
	auto extension = PathFromUrsaString(path.relative).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
	               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	if (extension == ".fbx" || extension == ".obj")
	{
		return AssetPreviewKind::Model;
	}
	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
	{
		return AssetPreviewKind::Texture;
	}
	return AssetPreviewKind::None;
}
} // namespace

bool AssetPreviewTarget::Supported() const
{
	return kind != AssetPreviewKind::None && static_cast<bool>(identity.guid);
}

bool AssetPreviewTarget::SameIdentity(const AssetPreviewTarget& other) const
{
	return projectSession == other.projectSession && kind == other.kind && identity.guid == other.identity.guid &&
	       identity.localFileId == other.identity.localFileId;
}

AssetPreviewTarget ResolveAssetPreviewTarget(const EditorSelectionContext& selection, const ProjectBrowserEntry* entry,
                                             const EditorDocument* document, UInt64 contentVersion, UInt64 localFileId,
                                             std::optional<ResourceKind> subAssetKind)
{
	if (selection.kind != EditorSelectionKind::Asset || !entry || entry->folder || entry->path != selection.assetPath)
	{
		return {};
	}
	AssetPreviewTarget target;
	target.kind = PreviewKind(subAssetKind ? subAssetKind : entry->kind, entry->path);
	target.identity = {entry->guid, localFileId};
	target.path = entry->path;
	target.contentVersion = contentVersion;
	if (document && !localFileId && document->Id() == entry->guid && document->Path() == entry->path &&
	    PreviewKind(document->Kind(), document->Path()) == target.kind)
	{
		target.draft = document;
	}
	return target;
}

bool AssetPreviewState::Select(AssetPreviewTarget selected)
{
	const bool identityChanged = !target.SameIdentity(selected);
	const bool changed = identityChanged || target.contentVersion != selected.contentVersion ||
	                     target.pipelineVersion != selected.pipelineVersion || target.draft != selected.draft;
	target = std::move(selected);
	if (identityChanged)
	{
		hasImage = false;
		displayedTarget = {};
		displayedImportVersion.clear();
	}
	if (changed)
	{
		++generation;
		needsRender = target.Supported();
		diagnostic.clear();
	}
	return changed;
}

bool AssetPreviewState::Complete(UInt64 completedGeneration, const String& importVersion)
{
	if (completedGeneration != generation || !target.Supported())
	{
		return false;
	}
	hasImage = true;
	displayedTarget = target;
	displayedTarget.draft = nullptr;
	displayedImportVersion = importVersion;
	displayedGeneration = generation;
	needsRender = false;
	diagnostic.clear();
	return true;
}

bool AssetPreviewState::Fail(UInt64 failedGeneration, String message)
{
	if (failedGeneration != generation)
	{
		return false;
	}
	needsRender = false;
	diagnostic = std::move(message);
	return true;
}

void AssetPreviewState::Invalidate()
{
	++generation;
	needsRender = target.Supported();
}

Result<void> AssetPreviewClock::SetDuration(Float64 seconds)
{
	if (!std::isfinite(seconds) || seconds < 0)
	{
		return Result<void>::Failure("Preview duration must be finite and nonnegative");
	}
	duration = seconds;
	time = 0;
	playing = false;
	wasVisible = false;
	return {};
}

Result<void> AssetPreviewClock::Seek(Float64 seconds)
{
	if (!std::isfinite(seconds) || seconds < 0 || seconds > duration)
	{
		return Result<void>::Failure("Preview time is outside the clip");
	}
	time = seconds;
	playing = false;
	return {};
}

Result<void> AssetPreviewClock::SetSpeed(Float64 multiplier)
{
	if (!std::isfinite(multiplier) || multiplier <= 0 || multiplier > 1000)
	{
		return Result<void>::Failure("Preview speed must be in (0, 1000]");
	}
	speed = multiplier;
	return {};
}

Result<void> AssetPreviewClock::Play()
{
	if (duration <= 0)
	{
		return Result<void>::Failure("Preview has no playable clip");
	}
	playing = true;
	return {};
}

Result<bool> AssetPreviewClock::Advance(Float64 deltaSeconds, bool visible)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds < 0)
	{
		return Result<bool>::Failure("Preview delta must be finite and nonnegative");
	}
	if (!visible)
	{
		wasVisible = false;
		return Result<bool>::Success(false);
	}
	if (!wasVisible)
	{
		wasVisible = true;
		return Result<bool>::Success(false);
	}
	if (!playing || deltaSeconds == 0)
	{
		return Result<bool>::Success(false);
	}
	const Float64 next = time + deltaSeconds * speed;
	if (!std::isfinite(next))
	{
		return Result<bool>::Failure("Preview time overflow");
	}
	if (next >= duration)
	{
		time = loop ? std::fmod(next, duration) : duration;
		if (!loop)
		{
			playing = false;
		}
	}
	else
	{
		time = next;
	}
	return Result<bool>::Success(true);
}
} // namespace Ursa::Editor
