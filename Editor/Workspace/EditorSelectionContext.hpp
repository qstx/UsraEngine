#pragma once

#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualPath.hpp>
#include <utility>
#include <functional>

namespace Ursa::Editor
{
enum class EditorSelectionKind
{
	None,
	Asset,
	GameObject
};

enum class EditorAssetSelectionRole
{
	Source,
	ImportedObject
};

struct EditorSelectionContext
{
	EditorSelectionKind kind = EditorSelectionKind::None;
	ResourceId activeDocumentId;
	ResourceId documentId;
	ResourceId objectId;
	VirtualPath assetPath;
	UInt64 assetLocalFileId = 0;
	String assetKind;
	ResourceId assetGuid;
	EditorAssetSelectionRole assetRole = EditorAssetSelectionRole::Source;
	bool assetMissing = false;
	std::function<bool(const EditorSelectionContext&)> beforeChange;

	bool SelectAsset(VirtualPath path, UInt64 localFileId = 0, String resourceKind = {}, ResourceId guid = {})
	{
		auto candidate = *this;
		candidate.kind = EditorSelectionKind::Asset;
		candidate.documentId = {};
		candidate.assetPath = path;
		candidate.assetLocalFileId = localFileId;
		candidate.assetKind = resourceKind;
		candidate.assetGuid = guid;
		candidate.assetRole = localFileId ? EditorAssetSelectionRole::ImportedObject : EditorAssetSelectionRole::Source;
		candidate.assetMissing = false;
		candidate.objectId = {};
		if (beforeChange && !beforeChange(candidate))
		{
			return false;
		}
		kind = EditorSelectionKind::Asset;
		documentId = {};
		assetPath = std::move(path);
		assetLocalFileId = localFileId;
		assetKind = std::move(resourceKind);
		assetGuid = guid;
		assetRole = candidate.assetRole;
		assetMissing = false;
		objectId = {};
		return true;
	}

	bool SelectGameObject(ResourceId document, ResourceId object)
	{
		auto candidate = *this;
		candidate.kind = EditorSelectionKind::GameObject;
		candidate.documentId = document;
		candidate.objectId = object;
		candidate.assetPath = {};
		candidate.assetLocalFileId = 0;
		candidate.assetKind.clear();
		candidate.assetGuid = {};
		candidate.assetRole = EditorAssetSelectionRole::Source;
		candidate.assetMissing = false;
		if (beforeChange && !beforeChange(candidate))
		{
			return false;
		}
		kind = EditorSelectionKind::GameObject;
		documentId = document;
		objectId = object;
		assetPath = {};
		assetLocalFileId = 0;
		assetKind.clear();
		assetGuid = {};
		assetRole = EditorAssetSelectionRole::Source;
		assetMissing = false;
		return true;
	}

	bool Clear(bool force = false)
	{
		auto candidate = *this;
		candidate.kind = EditorSelectionKind::None;
		candidate.documentId = {};
		candidate.objectId = {};
		candidate.assetPath = {};
		candidate.assetLocalFileId = 0;
		candidate.assetKind.clear();
		candidate.assetGuid = {};
		candidate.assetRole = EditorAssetSelectionRole::Source;
		candidate.assetMissing = false;
		if (!force && beforeChange && !beforeChange(candidate))
		{
			return false;
		}
		kind = EditorSelectionKind::None;
		documentId = {};
		objectId = {};
		assetPath = {};
		assetLocalFileId = 0;
		assetKind.clear();
		assetGuid = {};
		assetRole = EditorAssetSelectionRole::Source;
		assetMissing = false;
		return true;
	}

	void SetActiveDocument(ResourceId id)
	{
		activeDocumentId = id;
		if (kind == EditorSelectionKind::GameObject && documentId != id)
		{
			Clear(true);
		}
	}
};
} // namespace Ursa::Editor
