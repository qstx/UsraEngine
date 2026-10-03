#include <Scene/ComponentEditors.hpp>

namespace Ursa::Editor
{
Result<void> ComponentEditors::RegisterEditor(String type, ComponentEditor editor)
{
	if (type.empty() || !editor)
	{
		return Result<void>::Failure("Component editor requires a type and callback");
	}
	if (!editors.emplace(std::move(type), std::move(editor)).second)
	{
		return Result<void>::Failure("Component editor already registered");
	}
	return {};
}

const ComponentEditor* ComponentEditors::Find(const String& type) const
{
	auto found = editors.find(type);
	return found == editors.end() ? nullptr : &found->second;
}
} // namespace Ursa::Editor
