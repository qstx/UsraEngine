#pragma once

#include <Assets/AssetImportRules.hpp>
#include <Assets/ProjectAssetImporter.hpp>

#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/DotNet/DotNetHost.hpp>

namespace Ursa::Editor
{
using ImportDispatcher = std::function<Result<AssetImportResult>(const ProjectAssetImporter::ImportFunction&,
                                                                 AssetDatabase&, const VirtualPath&, bool)>;
Result<AssetImportResult> ImportScriptedAsset(AssetDatabase& database, const VirtualPath& source, bool force,
                                              const AssetImportRules& rules, Scripting::DotNetHost& host,
                                              UInt64 session, const std::function<UInt64()>& generationProvider = {});
Result<void> RegisterScriptedAssetImporters(ProjectAssetImporter& importer,
                                            std::shared_ptr<const AssetImportRules> rules, Scripting::DotNetHost& host,
                                            UInt64 session, ImportDispatcher dispatcher = {},
                                            std::function<UInt64()> generationProvider = {});
} // namespace Ursa::Editor
#endif
