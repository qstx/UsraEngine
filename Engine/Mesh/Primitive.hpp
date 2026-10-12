#pragma once

#include <Scene/GameObject.hpp>

namespace Ursa
{
class Scene;
class ResourceSession;

enum class PrimitiveType
{
	Cube
};

ResourceId BuiltinCubeMeshId();

ResourceId BuiltinDefaultMaterialId();

Result<GameObject::Ptr> CreatePrimitive(Scene& scene, ResourceSession& resources, PrimitiveType type,
                                        String name = "Cube");
} // namespace Ursa
