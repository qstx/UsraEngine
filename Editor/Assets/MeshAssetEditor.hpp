#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Serialization/ResourceSession.hpp>

namespace Ursa::Editor
{
Result<MeshPtr> SaveMeshAs(AssetDatabase& database, ResourceSession& session, const VirtualPath& destination,
                           const MeshPtr& mesh);
} // namespace Ursa::Editor
