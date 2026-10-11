#pragma once

#include <Scene/ComponentEditors.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>

namespace Ursa::Editor
{
struct ManagedExtensionDescriptor
{
	String id;
	String kind;
	String path;
	String target;
	String title;
};

struct ManagedEditorContext
{
	EditorDocument* document = nullptr;
	EditorDocuments* documents = nullptr;
	ProjectWorkspace* workspace = nullptr;
	ComponentRegistry* registry = nullptr;
	Component* target = nullptr;
	ComponentInspectorContext selection;
	ComponentSaveContext saveContext;
	std::function<Result<void>(const VirtualPath&)> openAsset;
};

#if defined(URSA_ENABLE_DOTNET)
Result<bool> InvokeManagedEditorExtension(Scripting::DotNetHost& host, UInt64 sessionId, const String& extensionId,
                                          ManagedEditorContext& context);
#endif
} // namespace Ursa::Editor
