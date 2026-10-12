#pragma once

#include <Serialization/ResourceSession.hpp>
#include <Serialization/JsonArchiveInterop.hpp>

namespace Ursa::Editor
{
struct MeshMigrationReport
{
	UInt32 documents = 0;
	String backupRoot;
};

class MeshComponentMigration final
{
public:
	static Result<nlohmann::json> Convert(const nlohmann::json& document, ResourceSession& resources,
	                                      const Map<String, nlohmann::json>& legacyPrefabs = {});

	static Result<MeshMigrationReport> Migrate(VirtualFileSystem& files, ResourceSession& resources,
	                                           bool dryRun = false);
};
} // namespace Ursa::Editor
