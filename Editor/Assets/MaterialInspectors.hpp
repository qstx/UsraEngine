#pragma once

#include <Assets/MaterialDraft.hpp>
#include <Scene/ComponentEditors.hpp>
#include <functional>

namespace Ursa::Editor
{
struct MaterialInspectorContext
{
	EditorDocument& document;
	MaterialDraft& draft;
	ResourceSession& resources;
};

using MaterialInspectorHandler = std::function<Result<bool>(MaterialInspectorContext&, InspectorUI&)>;

class MaterialInspectors final
{
public:
	Result<void> Register(const String& identifier, MaterialInspectorHandler inspector);

	const MaterialInspectorHandler* Find(const String& identifier) const;

	Result<bool> Draw(const String& identifier, MaterialInspectorContext& context, InspectorUI& ui) const;

private:
	Map<String, MaterialInspectorHandler> inspectors;
};
} // namespace Ursa::Editor
