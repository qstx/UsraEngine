#pragma once

#include <Scene/ComponentEditors.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <optional>

namespace Ursa::Editor
{
using ManagedExtensionDescriptor = Scripting::ManagedEditorExtension;
using ManagedExtensionKind = Scripting::ManagedEditorExtensionKind;

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
	std::optional<AssetReference> assetSelection;
	String assetSelectionName;
};

#if defined(URSA_ENABLE_DOTNET)
Result<bool> InvokeManagedEditorExtension(Scripting::DotNetHost& host, UInt64 sessionId, const String& extensionId,
                                          ManagedEditorContext& context);
#endif
} // namespace Ursa::Editor
