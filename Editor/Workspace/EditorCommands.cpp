#include <Workspace/EditorCommands.hpp>

namespace Ursa::Editor
{
EditorCommandContext EditorCommandContext::Capture(const EditorSelectionContext& selection, ResourceId activeDocumentId,
                                                   UInt64 documentRevision)
{
	EditorCommandContext result;
	result.selection = selection;
	result.selection.activeDocumentId = activeDocumentId;
	result.selection.beforeChange = {};
	result.activeDocumentId = activeDocumentId;
	result.documentRevision = documentRevision;
	return result;
}

Result<void> EditorCommands::Register(EditorCommandDescriptor command)
{
	if (command.id.empty() || command.label.empty() || !command.execute)
	{
		return Result<void>::Failure("Editor command requires an ID, label, and action");
	}
	if (!commands.emplace(command.id, std::move(command)).second)
	{
		return Result<void>::Failure("Editor command ID is already registered");
	}
	return {};
}

const EditorCommandDescriptor* EditorCommands::Find(const String& id) const
{
	const auto found = commands.find(id);
	return found == commands.end() ? nullptr : &found->second;
}

bool EditorCommands::IsCurrent(const EditorCommandContext& captured, const EditorCommandContext& current)
{
	if (captured.activeDocumentId != current.activeDocumentId ||
	    captured.documentRevision != current.documentRevision ||
	    captured.selection.revision != current.selection.revision ||
	    captured.selection.activeDocumentId != current.selection.activeDocumentId ||
	    captured.selection.primaryIndex != current.selection.primaryIndex ||
	    captured.selection.entries.size() != current.selection.entries.size())
	{
		return false;
	}
	for (Size index = 0; index < captured.selection.entries.size(); ++index)
	{
		if (!captured.selection.entries[index].SameIdentity(current.selection.entries[index]))
		{
			return false;
		}
	}
	return true;
}

Result<void> EditorCommands::CanExecute(const String& id, const EditorCommandContext& captured,
                                        const EditorCommandContext& current) const
{
	const auto* command = Find(id);
	if (!command)
	{
		return Result<void>::Failure("Unregistered Editor command " + id);
	}
	if (!IsCurrent(captured, current))
	{
		return Result<void>::Failure("Editor command target changed while the menu was open");
	}
	return command->canExecute ? command->canExecute(captured) : Result<void>{};
}

Result<void> EditorCommands::Execute(const String& id, const EditorCommandContext& captured,
                                     const EditorCommandContext& current,
                                     const EditorCommandTransaction& transaction) const
{
	const auto available = CanExecute(id, captured, current);
	if (!available)
	{
		return available;
	}
	const auto* command = Find(id);
	const auto invoke = [&] { return command->execute(captured); };
	return transaction ? transaction(captured, invoke) : invoke();
}
} // namespace Ursa::Editor
