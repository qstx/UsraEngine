#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Workspace/EditorDocuments.hpp>

namespace Ursa::Editor
{
struct EditorToolContext
{
	EditorDocument* document = nullptr;
	Vector<GameObject::Ptr> selection;
	Scene* scene = nullptr;
	Ursa::PrefabAsset* prefab = nullptr;
	ResourceSession* resources = nullptr;

	Result<void> MarkDirty()
	{
		if (!document)
		{
			return Result<void>::Failure("No Editor document is open");
		}
		document->MarkDirty();
		return {};
	}
};

using EditorToolAction = std::function<Result<bool>(EditorToolContext&, InspectorUI&)>;
using EditorToolAvailability = std::function<Result<void>(const EditorToolContext&)>;

struct EditorToolDescriptor
{
	String id;
	String displayName;
	String category;
	EditorToolAction execute;
	EditorToolAvailability available;
};

class EditorTools final
{
public:
	Result<void> Register(EditorToolDescriptor tool);

	Vector<EditorToolDescriptor> Tools() const;

	const EditorToolDescriptor* Find(const String& id) const;

	Result<void> CanExecute(const String& id, const EditorToolContext& context) const;

	Result<bool> Draw(const String& id, EditorToolContext& context, InspectorUI& ui) const;

private:
	Map<String, EditorToolDescriptor> tools;
};
} // namespace Ursa::Editor
