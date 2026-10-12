#pragma once

#include <Scene/Component.hpp>
#include <Workspace/ProjectWorkspace.hpp>

namespace Ursa::Editor
{
bool HasMeshInspector(const Component& component);

Result<bool> DrawMeshInspector(Component& component, ProjectWorkspace& workspace, ResourceSession& resources);
} // namespace Ursa::Editor
