#pragma once

#include <Assets/ImportSettingsDraft.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Workspace/ProjectWorkspace.hpp>

namespace Ursa::Editor
{
struct ManagedEditorContext;

enum class ImporterSelectionResult
{
	Pending,
	Accepted,
	Cancelled
};

// Owns one source's settings draft; callbacks only receive a leased property adapter.
class AssetImporterInspector final : public std::enable_shared_from_this<AssetImporterInspector>
{
public:
	using CustomDraw = std::function<Result<bool>(ManagedEditorContext&)>;
	static Result<std::shared_ptr<AssetImporterInspector>> Open(AssetDatabase& database, const VirtualPath& source);
	static Result<std::shared_ptr<AssetImporterInspector>> Open(ProjectWorkspace& workspace, const VirtualPath& source);
	static Result<std::shared_ptr<AssetImporterInspector>>
	OpenScripted(AssetDatabase& database, const VirtualPath& source, AssetImporterDescriptor descriptor,
	             nlohmann::json defaults, ImportSettingsDraft::Validator validate = {});
	SerializedTargetAdapter Properties();
	Result<bool> Apply();
	Result<void> Revert();
	Result<void> Discard();
	Result<void> SaveRecovery() const;
	void DrawApplyRevert(const std::function<void(const VirtualPath&)>& requestImport, bool canApply = true);
	ImporterSelectionResult DrawSelectionPrompt(const std::function<void(const VirtualPath&)>& requestImport,
	                                            bool canApply = true);

	Result<bool> DrawDefault(ProjectWorkspace& workspace, const std::function<void(const VirtualPath&)>& requestImport,
	                         bool canApply = true)
	{
		return DrawDefault(requestImport, canApply, &workspace);
	}

	Result<bool> DrawDefault(const std::function<void(const VirtualPath&)>& requestImport, bool canApply = true,
	                         ProjectWorkspace* workspace = nullptr, const CustomDraw& custom = {});

	bool HasModified() const
	{
		return draft.HasModified() || recoveryConflict;
	}

	const VirtualPath& SourcePath() const
	{
		return source;
	}

	const String& Identity() const
	{
		return identity;
	}

	const String& Diagnostic() const
	{
		return diagnostic;
	}

	const String& ClrTypeName() const
	{
		return clrTypeName;
	}

	AssetReference Reference() const
	{
		return {guid, 0};
	}

	const nlohmann::json& Settings() const
	{
		return draft.Settings();
	}

private:
	enum class Kind
	{
		Texture,
		Model,
		Scripted
	};
	Result<nlohmann::json> Capture() const;
	Result<nlohmann::json> Edit(const nlohmann::json& payload);
	Result<void> Recover();
	Result<void> ClearRecovery() const;
	VirtualPath RecoveryPath() const;
	AssetDatabase* database = nullptr;
	VirtualPath source;
	ResourceId guid;
	Kind kind = Kind::Scripted;
	String identity;
	UInt32 version = 0;
	String clrTypeName;
	String diagnostic;
	ImportSettingsDraft draft;
	nlohmann::json defaultPayload;
	std::optional<nlohmann::json> pendingPayload;
	bool recoveryConflict = false;
	std::unique_ptr<ResourceSession> resources;
};
} // namespace Ursa::Editor
