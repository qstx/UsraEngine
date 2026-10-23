#pragma once

#include <Assets/AssetImporterInspector.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>

namespace Ursa::Editor
{
class ScriptableAssetInspector final : public std::enable_shared_from_this<ScriptableAssetInspector>
{
public:
	static Result<std::shared_ptr<ScriptableAssetInspector>>
	Open(ProjectWorkspace& workspace, const VirtualPath& source, const ScriptTypeDescriptor& type);
	SerializedTargetAdapter Properties();
	Result<void> Save();
	Result<bool> Draw(const std::function<void(const VirtualPath&)>& requestImport,
	                  const AssetImporterInspector::CustomDraw& custom = {},
	                  const Vector<ScriptTypeDescriptor>* scriptTypes = nullptr);

	bool Dirty() const
	{
		return payload != baselinePayload;
	}

	const ScriptableAsset& Asset() const
	{
		return asset;
	}

	const VirtualPath& SourcePath() const
	{
		return source;
	}

private:
	Result<void> ValidateSource() const;
	ProjectWorkspace* workspace = nullptr;
	std::unique_ptr<AssetDatabase> database;
	VirtualPath source;
	ScriptableAsset asset;
	nlohmann::json payload;
	nlohmann::json baselinePayload;
	Vector<UInt8> baselineBytes;
	std::unique_ptr<ResourceSession> resources;
	String diagnostic;
};
} // namespace Ursa::Editor
