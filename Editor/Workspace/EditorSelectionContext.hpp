#pragma once

#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualPath.hpp>
#include <algorithm>
#include <optional>
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

struct EditorSelectionEntry
{
	EditorSelectionKind kind = EditorSelectionKind::None;
	ResourceId documentId;
	ResourceId objectId;
	bool playSession = false;
	VirtualPath assetPath;
	UInt64 assetLocalFileId = 0;
	String assetKind;
	ResourceId assetGuid;
	EditorAssetSelectionRole assetRole = EditorAssetSelectionRole::Source;
	bool assetMissing = false;
	bool missing = false;

	bool IsMissing() const
	{
		return missing || assetMissing;
	}

	bool SameIdentity(const EditorSelectionEntry& other) const
	{
		if (kind != other.kind)
		{
			return false;
		}
		if (kind == EditorSelectionKind::GameObject)
		{
			return documentId == other.documentId && objectId == other.objectId && playSession == other.playSession;
		}
		if (kind == EditorSelectionKind::Asset)
		{
			const bool sameSource =
			    assetGuid && other.assetGuid ? assetGuid == other.assetGuid : assetPath == other.assetPath;
			return sameSource && assetRole == other.assetRole && assetLocalFileId == other.assetLocalFileId;
		}
		return true;
	}
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
	Vector<EditorSelectionEntry> entries;
	Size primaryIndex = 0;
	std::optional<EditorSelectionEntry> rangeAnchor;
	UInt64 revision = 0;
	std::function<bool(const EditorSelectionContext&)> beforeChange;

	const EditorSelectionEntry* PrimaryEntry() const
	{
		return primaryIndex < entries.size() ? &entries[primaryIndex] : nullptr;
	}

	bool Contains(const EditorSelectionEntry& entry) const
	{
		return std::any_of(entries.begin(), entries.end(), [&](const auto& item) { return item.SameIdentity(entry); });
	}

	Size MissingCount() const
	{
		return static_cast<Size>(
		    std::count_if(entries.begin(), entries.end(), [](const auto& item) { return item.IsMissing(); }));
	}

	bool SelectEntries(Vector<EditorSelectionEntry> nextEntries, Size requestedPrimary = static_cast<Size>(-1),
	                   std::optional<EditorSelectionEntry> anchor = {})
	{
		Vector<EditorSelectionEntry> unique;
		unique.reserve(nextEntries.size());
		Size requestedIndex = static_cast<Size>(-1);
		EditorSelectionKind domain = EditorSelectionKind::None;
		ResourceId domainDocument;
		bool domainPlaySession = false;
		for (Size index = 0; index < nextEntries.size(); ++index)
		{
			const auto& entry = nextEntries[index];
			if (entry.kind == EditorSelectionKind::None)
			{
				continue;
			}
			if ((entry.kind == EditorSelectionKind::GameObject && (!entry.documentId || !entry.objectId)) ||
			    (entry.kind == EditorSelectionKind::Asset && !entry.assetGuid && entry.assetPath.relative.empty()) ||
			    entry.IsMissing())
			{
				return false;
			}
			if (domain == EditorSelectionKind::None)
			{
				domain = entry.kind;
				domainDocument = entry.documentId;
				domainPlaySession = entry.playSession;
			}
			else if (domain != entry.kind ||
			         (domain == EditorSelectionKind::GameObject &&
			          (domainDocument != entry.documentId || domainPlaySession != entry.playSession)))
			{
				return false;
			}
			const auto existing =
			    std::find_if(unique.begin(), unique.end(), [&](const auto& item) { return item.SameIdentity(entry); });
			if (existing != unique.end())
			{
				if (index == requestedPrimary)
				{
					requestedIndex = static_cast<Size>(std::distance(unique.begin(), existing));
				}
				continue;
			}
			unique.push_back(entry);
			if (index == requestedPrimary)
			{
				requestedIndex = unique.size() - 1;
			}
		}

		EditorSelectionContext candidate = *this;
		candidate.entries = std::move(unique);
		candidate.primaryIndex = candidate.entries.empty()                   ? 0
		                         : requestedIndex < candidate.entries.size() ? requestedIndex
		                                                                     : candidate.entries.size() - 1;
		candidate.rangeAnchor = std::move(anchor);
		if (!candidate.rangeAnchor && !candidate.entries.empty())
		{
			candidate.rangeAnchor = candidate.entries[candidate.primaryIndex];
		}
		candidate.SyncLegacyPrimary();
		if (beforeChange && !beforeChange(candidate))
		{
			return false;
		}
		CommitSelection(std::move(candidate));
		return true;
	}

	void ReconcileEntries(Vector<EditorSelectionEntry> nextEntries)
	{
		const auto sameState = [](const EditorSelectionEntry& left, const EditorSelectionEntry& right)
		{
			return left.SameIdentity(right) && left.documentId == right.documentId && left.objectId == right.objectId &&
			       left.assetPath == right.assetPath && left.assetLocalFileId == right.assetLocalFileId &&
			       left.assetKind == right.assetKind && left.assetGuid == right.assetGuid &&
			       left.assetRole == right.assetRole && left.assetMissing == right.assetMissing &&
			       left.missing == right.missing;
		};
		bool changed = entries.size() != nextEntries.size();
		for (Size index = 0; !changed && index < entries.size(); ++index)
		{
			changed = !sameState(entries[index], nextEntries[index]);
		}
		const auto* previousPrimary = PrimaryEntry();
		Size nextPrimary = static_cast<Size>(-1);
		if (previousPrimary)
		{
			const auto samePrimary =
			    std::find_if(nextEntries.begin(), nextEntries.end(), [&](const auto& entry)
			                 { return entry.SameIdentity(*previousPrimary) && !entry.IsMissing(); });
			if (samePrimary != nextEntries.end())
			{
				nextPrimary = static_cast<Size>(std::distance(nextEntries.begin(), samePrimary));
			}
		}
		if (nextPrimary == static_cast<Size>(-1))
		{
			for (Size index = nextEntries.size(); index > 0; --index)
			{
				if (!nextEntries[index - 1].IsMissing())
				{
					nextPrimary = index - 1;
					break;
				}
			}
		}
		if (nextPrimary == static_cast<Size>(-1) && !nextEntries.empty())
		{
			nextPrimary = std::min(primaryIndex, nextEntries.size() - 1);
		}
		if (nextEntries.empty())
		{
			nextPrimary = 0;
		}
		changed |= primaryIndex != nextPrimary;
		const bool anchorExists =
		    rangeAnchor && std::any_of(nextEntries.begin(), nextEntries.end(), [&](const auto& entry)
		                               { return entry.SameIdentity(*rangeAnchor) && !entry.IsMissing(); });
		if (!anchorExists)
		{
			const auto nextAnchor = nextPrimary < nextEntries.size() && !nextEntries[nextPrimary].IsMissing()
			                            ? std::optional<EditorSelectionEntry>(nextEntries[nextPrimary])
			                            : std::nullopt;
			if (rangeAnchor.has_value() != nextAnchor.has_value() ||
			    (rangeAnchor && nextAnchor && !rangeAnchor->SameIdentity(*nextAnchor)))
			{
				rangeAnchor = nextAnchor;
				changed = true;
			}
		}
		if (changed)
		{
			entries = std::move(nextEntries);
			primaryIndex = nextPrimary;
			++revision;
			SyncLegacyPrimary();
		}
	}

	bool RemoveEntry(const EditorSelectionEntry& entry)
	{
		Vector<EditorSelectionEntry> remaining;
		remaining.reserve(entries.size());
		for (const auto& item : entries)
		{
			if (!item.SameIdentity(entry))
			{
				remaining.push_back(item);
			}
		}
		if (remaining.size() == entries.size())
		{
			return true;
		}
		return SelectEntries(std::move(remaining), static_cast<Size>(-1), rangeAnchor);
	}

	bool SelectClicked(const EditorSelectionEntry& clicked, const Vector<EditorSelectionEntry>& visibleRows,
	                   bool control, bool shift)
	{
		if (clicked.kind == EditorSelectionKind::None)
		{
			return false;
		}
		Vector<EditorSelectionEntry> currentEntries;
		for (const auto& item : entries)
		{
			if (!item.IsMissing())
			{
				currentEntries.push_back(item);
			}
		}
		const bool sameDomain =
		    currentEntries.empty() ||
		    std::all_of(currentEntries.begin(), currentEntries.end(),
		                [&](const auto& item)
		                {
			                return item.kind == clicked.kind &&
			                       (clicked.kind != EditorSelectionKind::GameObject ||
			                        (item.documentId == clicked.documentId && item.playSession == clicked.playSession));
		                });
		if (!sameDomain)
		{
			return SelectEntries({clicked}, 0, clicked);
		}
		if (shift)
		{
			const auto anchor = rangeAnchor ? std::find_if(visibleRows.begin(), visibleRows.end(), [&](const auto& item)
			                                               { return item.SameIdentity(*rangeAnchor); })
			                                : visibleRows.end();
			const auto target = std::find_if(visibleRows.begin(), visibleRows.end(),
			                                 [&](const auto& item) { return item.SameIdentity(clicked); });
			if (target == visibleRows.end())
			{
				return false;
			}
			if (anchor == visibleRows.end())
			{
				return SelectEntries({clicked}, 0, clicked);
			}
			const auto first = std::min(anchor, target);
			const auto last = std::max(anchor, target);
			Vector<EditorSelectionEntry> next;
			if (control)
			{
				next = currentEntries;
			}
			for (auto item = first; item != last + 1; ++item)
			{
				if (std::none_of(next.begin(), next.end(),
				                 [&](const auto& current) { return current.SameIdentity(*item); }))
				{
					next.push_back(*item);
				}
			}
			const auto primary =
			    std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.SameIdentity(clicked); });
			const Size primarySelectionIndex =
			    primary == next.end() ? static_cast<Size>(-1) : static_cast<Size>(std::distance(next.begin(), primary));
			return SelectEntries(std::move(next), primarySelectionIndex, rangeAnchor);
		}
		if (control)
		{
			Vector<EditorSelectionEntry> next = currentEntries;
			const auto existing =
			    std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.SameIdentity(clicked); });
			Size primary = static_cast<Size>(-1);
			if (existing == next.end())
			{
				next.push_back(clicked);
				primary = next.size() - 1;
			}
			else
			{
				next.erase(existing);
				if (!next.empty())
				{
					const auto currentPrimary = PrimaryEntry();
					const auto retained = currentPrimary ? std::find_if(next.begin(), next.end(), [&](const auto& item)
					                                                    { return item.SameIdentity(*currentPrimary); })
					                                     : next.end();
					primary = retained != next.end() ? static_cast<Size>(std::distance(next.begin(), retained))
					                                 : next.size() - 1;
				}
			}
			return SelectEntries(std::move(next), primary, clicked);
		}
		return SelectEntries({clicked}, 0, clicked);
	}

	bool SelectAsset(VirtualPath path, UInt64 localFileId = 0, String resourceKind = {}, ResourceId guid = {})
	{
		EditorSelectionEntry entry;
		entry.kind = EditorSelectionKind::Asset;
		entry.assetPath = std::move(path);
		entry.assetLocalFileId = localFileId;
		entry.assetKind = std::move(resourceKind);
		entry.assetGuid = guid;
		entry.assetRole = localFileId ? EditorAssetSelectionRole::ImportedObject : EditorAssetSelectionRole::Source;
		return SelectEntries({std::move(entry)}, 0);
	}

	bool SelectGameObject(ResourceId document, ResourceId object, bool playSession = false)
	{
		EditorSelectionEntry entry;
		entry.kind = EditorSelectionKind::GameObject;
		entry.documentId = document;
		entry.objectId = object;
		entry.playSession = playSession;
		return SelectEntries({std::move(entry)}, 0);
	}

	bool Clear(bool force = false)
	{
		EditorSelectionContext candidate = *this;
		candidate.entries.clear();
		candidate.primaryIndex = 0;
		candidate.rangeAnchor.reset();
		candidate.SyncLegacyPrimary();
		if (!force && beforeChange && !beforeChange(candidate))
		{
			return false;
		}
		CommitSelection(std::move(candidate));
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

private:
	void SyncLegacyPrimary()
	{
		const auto* primary = PrimaryEntry();
		kind = primary ? primary->kind : EditorSelectionKind::None;
		documentId = primary ? primary->documentId : ResourceId{};
		objectId = primary ? primary->objectId : ResourceId{};
		assetPath = primary ? primary->assetPath : VirtualPath{};
		assetLocalFileId = primary ? primary->assetLocalFileId : 0;
		assetKind = primary ? primary->assetKind : String{};
		assetGuid = primary ? primary->assetGuid : ResourceId{};
		assetRole = primary ? primary->assetRole : EditorAssetSelectionRole::Source;
		assetMissing = primary && primary->assetMissing;
	}

	void CommitSelection(EditorSelectionContext candidate)
	{
		candidate.revision = revision + 1;
		entries = std::move(candidate.entries);
		primaryIndex = candidate.primaryIndex;
		rangeAnchor = std::move(candidate.rangeAnchor);
		revision = candidate.revision;
		SyncLegacyPrimary();
	}
};

struct PendingEditorSelectionClick
{
	EditorSelectionEntry entry;
	Vector<EditorSelectionEntry> visibleRows;
	UInt64 selectionRevision = 0;
	bool control = false;
	bool shift = false;
	bool active = false;
	bool dragged = false;

	void Begin(const EditorSelectionEntry& target, const Vector<EditorSelectionEntry>& rows, bool controlDown,
	           bool shiftDown, UInt64 revision)
	{
		entry = target;
		visibleRows = rows;
		selectionRevision = revision;
		control = controlDown;
		shift = shiftDown;
		active = true;
		dragged = false;
	}

	void ObserveDrag(bool dragging)
	{
		dragged |= dragging;
	}

	bool Matches(const EditorSelectionEntry& target) const
	{
		return active && entry.SameIdentity(target);
	}

	void Finish(EditorSelectionContext& selection, bool releasedOverTarget)
	{
		if (active && !dragged && releasedOverTarget && selection.revision == selectionRevision)
		{
			selection.SelectClicked(entry, visibleRows, control, shift);
		}
		active = false;
		visibleRows.clear();
	}

	void Cancel()
	{
		active = false;
		visibleRows.clear();
	}
};
} // namespace Ursa::Editor
