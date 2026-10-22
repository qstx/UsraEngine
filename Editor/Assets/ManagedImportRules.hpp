#pragma once

#include <Assets/AssetImportRules.hpp>

#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/DotNet/DotNetHost.hpp>

namespace Ursa::Editor
{
Result<AssetImportRules> DiscoverManagedImportRules(Scripting::DotNetHost& host, UInt64 session,
                                                    String compiledFingerprint, UInt64 generation);
} // namespace Ursa::Editor
#endif
