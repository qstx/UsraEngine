#pragma once

#include <Assets/AssetImporterInspector.hpp>
#include <Workspace/EditorDocuments.hpp>

namespace Ursa::Editor
{
class NativeAssetInspector final : public std::enable_shared_from_this<NativeAssetInspector>
{
public:
	using DefaultDraw = std::function<std::optional<Result<bool>>(EditorDocument&, EditorDocuments&)>;
	static Result<std::shared_ptr<NativeAssetInspector>> Open(ProjectWorkspace& workspace, const VirtualPath& source,
	                                                          ResourceKind kind,
	                                                          const ComponentRegistry* registry = nullptr,
	                                                          EditorDocuments* existingDocuments = nullptr);
	Result<void> Save();
	Result<bool> Draw(const std::function<void(const VirtualPath&)>& requestImport,
	                  const AssetImporterInspector::CustomDraw& custom = {}, const DefaultDraw& fallback = {});

	EditorDocument& Document() const
	{
		return *documents->Current();
	}

	SerializedTargetAdapter Properties();

	bool OwnsDocument() const
	{
		return ownedDocuments != nullptr;
	}

	bool Active() const
	{
		return documents && documents->Current() == sourceDocument;
	}

private:
	Result<void> ValidateSource() const;
	ProjectWorkspace* workspace = nullptr;
	ComponentRegistry components;
	std::unique_ptr<EditorDocuments> ownedDocuments;
	EditorDocuments* documents = nullptr;
	EditorDocument* sourceDocument = nullptr;
	std::unique_ptr<ComponentLoadContext> loadContext;
	Vector<UInt8> baselineBytes;
	std::optional<Vector<UInt8>> baselineMetadata;
	String diagnostic;
};
} // namespace Ursa::Editor
