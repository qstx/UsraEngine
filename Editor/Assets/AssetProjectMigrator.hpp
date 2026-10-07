#pragma once

#include <Assets/AssetDatabase.hpp>

namespace Ursa::Editor
{
struct AssetMigrationSummary
{
	Ursa::UInt32 seededGuids = 0;
	Ursa::UInt32 convertedReferences = 0;
	String backupRoot;
};

class AssetProjectMigrator final
{
public:
	static Result<AssetMigrationSummary> Migrate(AssetDatabase& database);

	static Result<void> Restore(VirtualFileSystem& filesystem, const String& backupRoot);
};
} // namespace Ursa::Editor
