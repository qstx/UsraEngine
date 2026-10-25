#include <Workspace/EditorCommands.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		const ResourceId document = ResourceId::Generate();
		EditorSelectionEntry first;
		first.kind = EditorSelectionKind::GameObject;
		first.documentId = document;
		first.objectId = ResourceId::Generate();
		EditorSelectionEntry second = first;
		second.objectId = ResourceId::Generate();

		EditorSelectionContext selection;
		selection.activeDocumentId = document;
		Expect(static_cast<bool>(selection.SelectEntries({first, second}, 1)),
		       "Could not capture the initial target set");
		selection.beforeChange = [](const auto&) -> bool
		{ throw std::runtime_error("Command snapshot retained a live callback"); };
		EditorCommandContext captured = EditorCommandContext::Capture(selection, document, 17);
		Expect(!captured.selection.beforeChange, "Command snapshot retained the live selection callback");

		Size executeCount = 0;
		Size transactionCount = 0;
		EditorCommands commands;
		Expect(static_cast<bool>(commands.Register(
		           {"delete", "Delete", "Delete",
		            [](const auto& context)
		            {
			            if (context.selection.entries.empty())
			            {
				            return Result<void>::Failure("Select at least one object");
			            }
			            return std::all_of(
			                       context.selection.entries.begin(), context.selection.entries.end(),
			                       [](const auto& entry)
			                       { return entry.kind == EditorSelectionKind::GameObject && !entry.IsMissing(); })
			                       ? Result<void>{}
			                       : Result<void>::Failure("Every selected item must be a live GameObject");
		            },
		            [&](const auto& context)
		            {
			            ++executeCount;
			            return context.selection.PrimaryEntry()->objectId == second.objectId
			                       ? Result<void>{}
			                       : Result<void>::Failure("Captured primary was not preserved");
		            }})),
		       "Could not register command");
		Expect(static_cast<bool>(
		           commands.Register({"disabled",
		                              "Disabled",
		                              {},
		                              [](const auto&) { return Result<void>::Failure("Operation is unavailable"); },
		                              [](const auto&) { return Result<void>{}; }})),
		       "Could not register disabled command");

		EditorCommandContext current = captured;
		Expect(static_cast<bool>(commands.CanExecute("delete", captured, current)),
		       "A current command target was rejected");
		Expect(static_cast<bool>(commands.Execute("delete", captured, current,
		                                          [&](const auto&, const auto& invoke)
		                                          {
			                                          ++transactionCount;
			                                          return invoke();
		                                          })),
		       "Command did not execute through its transaction entrypoint");
		Expect(executeCount == 1 && transactionCount == 1,
		       "Command or transaction was invoked an incorrect number of times");
		Expect(static_cast<bool>(commands.Execute("delete", captured, current)),
		       "The same captured command could not be invoked through a second trigger");
		Expect(executeCount == 2, "Menu and shortcut triggers did not share the registered command target");
		Expect(!commands.CanExecute("disabled", captured, current), "Unavailable command had no disabled reason");
		EditorCommandContext invalidBatch = captured;
		invalidBatch.selection.entries[1].missing = true;
		Expect(!commands.CanExecute("delete", invalidBatch, invalidBatch),
		       "A batch command stayed enabled when one captured item became invalid");
		Expect(!commands.Execute("delete", invalidBatch, invalidBatch) && executeCount == 2,
		       "Executing an invalid batch ran a partial command");

		current.documentRevision++;
		Expect(!commands.CanExecute("delete", captured, current),
		       "A stale document revision retained a command target");
		Expect(!commands.Execute("delete", captured, current) && executeCount == 2,
		       "Executing a stale command reused a later target or ran the action");
		current = captured;
		Expect(static_cast<bool>(current.selection.SelectEntries({first, second}, 0)),
		       "Could not change the active primary");
		Expect(!commands.Execute("delete", captured, current) && executeCount == 2,
		       "Changing the primary while a menu was open ran the old command target");
		Expect(!commands.Execute("missing", captured, captured), "An unregistered command was accepted");
		std::cout << "Editor command capture tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
