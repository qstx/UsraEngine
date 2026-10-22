#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetImportRules.hpp>

namespace Ursa::Editor
{
// Source-controlled capability declaration, usable by tools built without a CLR.
Result<void> SaveManagedImportRequirements(AssetDatabase& database, const AssetImportRules& rules);
Result<void> CheckNativeImportCapability(AssetDatabase& database);
} // namespace Ursa::Editor
