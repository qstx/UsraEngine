#include <Assets/MaterialInspectors.hpp>

namespace Ursa::Editor
{
Result<void> MaterialInspectors::Register(const String& identifier, MaterialInspectorHandler inspector)
{
	if (identifier.empty() || !inspector)
	{
		return Result<void>::Failure("Material Inspector registration needs an ID and callback");
	}
	if (inspectors.find(identifier) != inspectors.end())
	{
		return Result<void>::Failure("Material Inspector is already registered: " + identifier);
	}
	inspectors.emplace(identifier, std::move(inspector));
	return {};
}

const MaterialInspectorHandler* MaterialInspectors::Find(const String& identifier) const
{
	const auto found = inspectors.find(identifier);
	return found == inspectors.end() ? nullptr : &found->second;
}

Result<bool> MaterialInspectors::Draw(const String& identifier, MaterialInspectorContext& context,
                                      InspectorUI& ui) const
{
	const auto* inspector = Find(identifier);
	if (!inspector)
	{
		return Result<bool>::Failure("Custom Material Inspector is not registered: " + identifier);
	}
	return (*inspector)(context, ui);
}
} // namespace Ursa::Editor
