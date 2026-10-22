#pragma once

#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/AssetImportRules.hpp>
#include <memory>

namespace Ursa::Editor
{
// Owns its CLR endpoint, project domain and serial import queue on a dedicated CPU thread.
class ManagedImportEnvironment final : public std::enable_shared_from_this<ManagedImportEnvironment>
{
public:
	static Result<std::shared_ptr<ManagedImportEnvironment>> Open(const VirtualFileSystem& files, String configuration,
	                                                              bool buildScripts);
	~ManagedImportEnvironment();
	Result<bool> IsCurrent();
	Result<void> Install(ProjectAssetImporter& importer);
	void Cancel();
	UInt64 Generation() const;
	const String& Fingerprint() const;
	const Vector<ScriptTypeDescriptor>& ScriptTypes() const;
	const std::shared_ptr<const AssetImportRules>& Rules() const;

private:
	struct Impl;
	explicit ManagedImportEnvironment(std::unique_ptr<Impl> implementation);
	std::unique_ptr<Impl> implementation;
};
} // namespace Ursa::Editor
