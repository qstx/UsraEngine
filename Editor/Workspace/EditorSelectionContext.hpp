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

struct EditorSelectionContext
{
	EditorSelectionKind kind = EditorSelectionKind::None;
	ResourceId activeDocumentId;
	ResourceId documentId;
	ResourceId objectId;
	VirtualPath assetPath;
	UInt64 assetLocalFileId = 0;
	String assetKind;
	std::function<bool(const EditorSelectionContext&)> beforeChange;

	bool SelectAsset(VirtualPath path, UInt64 localFileId = 0, String resourceKind = {})
	{
		auto candidate = *this;
		candidate.kind = EditorSelectionKind::Asset;
		candidate.documentId = {};
		candidate.assetPath = path;
		candidate.assetLocalFileId = localFileId;
		candidate.assetKind = resourceKind;
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
