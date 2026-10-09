#pragma once

#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualPath.hpp>
#include <utility>

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

	void SelectAsset(VirtualPath path)
	{
		kind = EditorSelectionKind::Asset;
		documentId = {};
		assetPath = std::move(path);
		objectId = {};
	}

	void SelectGameObject(ResourceId document, ResourceId object)
	{
		kind = EditorSelectionKind::GameObject;
		documentId = document;
		objectId = object;
		assetPath = {};
	}

	void Clear()
	{
		kind = EditorSelectionKind::None;
		documentId = {};
		objectId = {};
		assetPath = {};
	}

	void SetActiveDocument(ResourceId id)
	{
		activeDocumentId = id;
		if (kind == EditorSelectionKind::GameObject && documentId != id)
		{
			Clear();
		}
	}
};
} // namespace Ursa::Editor
