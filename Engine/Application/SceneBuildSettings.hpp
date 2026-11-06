#pragma once

#include <Serialization/AssetReference.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa
{
inline constexpr const char* sceneBuildSettingsSource = "settings:BuildSettings.json";
inline constexpr const char* sceneBuildSettingsRuntime = "prj:__UrsaSettings/Scenes.json";

struct BuildScene
{
	AssetReference reference;
	String name;
	VirtualPath path;
	Int32 buildIndex = -1;
};

class SceneBuildSettings final
{
public:
	bool configured = false;
	Vector<BuildScene> scenes;
	Result<BuildScene> Resolve(Int32 buildIndex) const;
	Result<BuildScene> Resolve(const String& nameOrPath, String* diagnostic = nullptr) const;
	Result<BuildScene> Resolve(AssetReference reference) const;
};

Result<SceneBuildSettings> ReadSceneBuildSettings(const VirtualFileSystem& files,
                                                  const ResourceSession::AssetPathResolver& resolver = {},
                                                  bool runtime = false);
Result<Vector<UInt8>> EncodeSceneBuildSettings(const SceneBuildSettings& settings);
} // namespace Ursa
