#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Workspace/EditorSelectionContext.hpp>

namespace Ursa::Editor
{
struct EditorCommandContext
{
	EditorSelectionContext selection;
	ResourceId activeDocumentId;
	UInt64 documentRevision = 0;

	static EditorCommandContext Capture(const EditorSelectionContext& selection, ResourceId activeDocumentId,
	                                    UInt64 documentRevision);
};

using EditorCommandAction = std::function<Result<void>(const EditorCommandContext&)>;
using EditorCommandAvailability = std::function<Result<void>(const EditorCommandContext&)>;
using EditorCommandTransaction =
    std::function<Result<void>(const EditorCommandContext&, const std::function<Result<void>()>&)>;

struct EditorCommandDescriptor
{
	String id;
	String label;
	String shortcut;
	EditorCommandAvailability canExecute;
	EditorCommandAction execute;
};

class EditorCommands final
{
public:
	Result<void> Register(EditorCommandDescriptor command);

	const EditorCommandDescriptor* Find(const String& id) const;

	Result<void> CanExecute(const String& id, const EditorCommandContext& captured,
	                        const EditorCommandContext& current) const;

	Result<void> Execute(const String& id, const EditorCommandContext& captured, const EditorCommandContext& current,
	                     const EditorCommandTransaction& transaction = {}) const;

private:
	static bool IsCurrent(const EditorCommandContext& captured, const EditorCommandContext& current);
	Map<String, EditorCommandDescriptor> commands;
};
} // namespace Ursa::Editor
