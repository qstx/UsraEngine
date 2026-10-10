#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Serialization/ResourceIndex.hpp>
#include <Serialization/ResourceSession.hpp>
#include <filesystem>
#include <memory>

namespace Ursa::Editor
{
// The scheme names are a convention this editor registers: project resources
// live under "<project>/Assets", engine resources under the read-only mounts.
inline constexpr const char* projectResourceScheme = projectScheme;
inline constexpr const char* engineResourceScheme = engineScheme;

class ProjectWorkspace final
{
public:
	static Result<std::unique_ptr<ProjectWorkspace>> FromFilesystem(const std::filesystem::path& projectDirectory,
	                                                                const VirtualFileSystem& filesystem);

	static Result<std::unique_ptr<ProjectWorkspace>> Open(const std::filesystem::path& projectDirectory,
	                                                      const Vector<ResourceMount>& builtinMounts = {});

	Result<void> Refresh();

	Result<void> InitializeBuiltinAssets();

	AssetDatabase* BuiltinDatabase()
	{
		return builtinDatabase.get();
	}

	const AssetDatabase* BuiltinDatabase() const
	{
		return builtinDatabase.get();
	}

	// Verify that a virtual path belongs to the writable project mount.
	Result<void> CheckWritable(const VirtualPath& path) const;

	Vector<ResourceEntry> Resources() const
	{
		return index.Entries();
	}

	Vector<AssetRecord> Assets() const
	{
		return assetDatabase ? assetDatabase->Assets() : Vector<AssetRecord>{};
	}

	AssetDatabase* Database()
	{
		return assetDatabase.get();
	}

	const AssetDatabase* Database() const
	{
		return assetDatabase.get();
	}

	ResourceSession::AssetPathResolver AssetResolver() const;

	Result<void> SetRuntimeContentView(const VirtualFileSystem& view);

	const VirtualFileSystem& ContentFiles() const
	{
		return contentFiles ? *contentFiles : index.Filesystem();
	}

	VirtualFileSystem& ContentFiles()
	{
		return contentFiles ? *contentFiles : index.Filesystem();
	}

	const ResourceIndex& Index() const
	{
		return index;
	}

	ResourceIndex& Index()
	{
		return index;
	}

	const std::filesystem::path& ProjectRoot() const
	{
		return projectRoot;
	}

	const std::filesystem::path& AssetsRoot() const
	{
		return assetsRoot;
	}

private:
	std::filesystem::path projectRoot;
	std::filesystem::path assetsRoot;
	Vector<ResourceMount> engineMounts;
	ResourceIndex index;
	std::unique_ptr<AssetDatabase> assetDatabase;
	std::unique_ptr<AssetDatabase> builtinDatabase;
	std::unique_ptr<VirtualFileSystem> contentFiles;
};
} // namespace Ursa::Editor
