#pragma once

#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <scene/Scene.hpp>
#include <functional>

namespace Ursa::Editor
{
using ComponentEditor = std::function<Result<bool>(Component&, Scene&, NodeId)>;

class ComponentEditors final
{
public:
	Result<void> RegisterEditor(String type, ComponentEditor editor);

	const ComponentEditor* Find(const String& type) const;

private:
	Map<String, ComponentEditor> editors;
};
} // namespace Ursa::Editor
