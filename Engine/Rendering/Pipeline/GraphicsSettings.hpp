#pragma once

#include <Serialization/AssetReference.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Serialization/ResourceSession.hpp>
#include <functional>

namespace Ursa::Rendering
{
struct GraphicsSettings
{
	AssetReference renderPipeline;
};

inline constexpr const char* graphicsSettingsSource = "settings:Graphics.json";
inline constexpr const char* graphicsSettingsRuntime = "prj:__UrsaSettings/Graphics.json";

using PipelineAssetValidator = std::function<Result<void>(AssetReference)>;

Result<GraphicsSettings> ReadGraphicsSettings(const VirtualFileSystem& files, bool runtime = false);
Result<Vector<UInt8>> EncodeGraphicsSettings(const GraphicsSettings& settings);
Result<void> ValidatePipelineAsset(const VirtualFileSystem& files, const ResourceSession::AssetPathResolver& resolver,
                                   const Vector<ScriptTypeDescriptor>& types, AssetReference reference);
// Validate a nonempty selection before replacing the source file.
Result<void> SaveGraphicsSettings(VirtualFileSystem& files, const GraphicsSettings& settings,
                                  const PipelineAssetValidator& validator = {});
} // namespace Ursa::Rendering
