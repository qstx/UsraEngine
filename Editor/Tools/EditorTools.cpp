#include <Tools/EditorTools.hpp>

namespace Ursa::Editor
{
Result<void> EditorTools::Register(EditorToolDescriptor tool)
{
	if (tool.id.empty() || tool.displayName.empty() || tool.category.empty() || !tool.execute)
	{
		return Result<void>::Failure("Editor tool requires an ID, display name, category, and action");
	}
	if (!tools.emplace(tool.id, std::move(tool)).second)
	{
		return Result<void>::Failure("Editor tool ID is already registered");
	}
	return {};
}

Vector<EditorToolDescriptor> EditorTools::Tools() const
{
	Vector<EditorToolDescriptor> result;
	result.reserve(tools.size());
	for (const auto& item : tools)
	{
		result.push_back(item.second);
	}
	return result;
}

const EditorToolDescriptor* EditorTools::Find(const String& id) const
{
	auto found = tools.find(id);
	return found == tools.end() ? nullptr : &found->second;
}

Result<void> EditorTools::CanExecute(const String& id, const EditorToolContext& context) const
{
	auto found = tools.find(id);
	if (found == tools.end())
	{
		return Result<void>::Failure("Unregistered Editor tool " + id);
	}
	return found->second.available ? found->second.available(context) : Result<void>{};
}

Result<bool> EditorTools::Draw(const String& id, EditorToolContext& context, InspectorUI& ui) const
{
	auto found = tools.find(id);
	if (found == tools.end())
	{
		return Result<bool>::Failure("Unregistered Editor tool " + id);
	}
	auto available = CanExecute(id, context);
	return available ? found->second.execute(context, ui) : Result<bool>::Failure(available.error);
}
} // namespace Ursa::Editor
