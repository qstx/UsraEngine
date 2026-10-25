#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <functional>

namespace Ursa
{
class PrefabAsset;
}

namespace Ursa::Editor
{
struct ComponentInspectorContext
{
	GameObject::Ptr object;
	Vector<GameObject::Ptr> roots;
	Vector<Component*> targets;
	Size primaryIndex = 0;
	Scene* scene = nullptr;
	EditorSelectionContext* selection = nullptr;
	ResourceId documentId;
	Ursa::PrefabAsset* prefab = nullptr;
	ComponentLoadContext* loadContext = nullptr;
};

struct ComponentTargetGroup
{
	String stableType;
	Size sameTypeIndex = 0;
	Vector<Component*> targets;
};

// Builds only the intersection of the component sets. Identity is the stable component
// serialization/script type plus the occurrence index among components of that type.
Result<Vector<ComponentTargetGroup>> BuildCommonComponentMapping(const Vector<GameObject::Ptr>& targets,
                                                                 Size primaryIndex = 0);

struct InspectorOption
{
	ResourceId id;
	String label;
};

class InspectorUI
{
public:
	virtual ~InspectorUI() = default;

	virtual Result<bool> Checkbox(const String& label, bool& value) = 0;

	virtual Result<bool> Float(const String& label, Float32& value, Float32 minimum, Float32 maximum,
	                           Float32 speed) = 0;

	virtual Result<bool> Color(const String& label, Ursa::Color& value) = 0;

	virtual Result<bool> Select(const String& label, const Vector<InspectorOption>& options, ResourceId& selected) = 0;

	virtual void Text(const String& message) = 0;
};

using ComponentEditor = std::function<Result<bool>(Component&, ComponentInspectorContext&, InspectorUI&)>;

using ComponentFactory = std::function<Result<Component*>(ComponentInspectorContext&)>;

using ComponentAvailability = std::function<Result<void>(const ComponentInspectorContext&)>;

struct ComponentTypeDescriptor
{
	String type;
	String displayName;
	String category;
	ComponentFactory create;
	ComponentAvailability available;
	bool allowMultiple = true;
	UInt32 version = 1;
};

class ComponentEditors final
{
public:
	Result<void> RegisterEditor(String type, ComponentEditor editor);

	Result<void> RegisterType(ComponentTypeDescriptor descriptor, ComponentEditor editor = {});

	void ReplaceEditor(const String& type, ComponentEditor editor, bool supportsMultipleObjects = false);

	void UnregisterType(const String& type);

	const ComponentEditor* Find(const String& type) const;
	bool SupportsMultipleObjects(const String& type) const;

	const ComponentTypeDescriptor* FindType(const String& type) const;

	Vector<ComponentTypeDescriptor> Types() const;

	Result<void> CanCreate(const String& type, const ComponentInspectorContext& context) const;

	Result<Component*> Create(const String& type, ComponentInspectorContext& context) const;

private:
	Map<String, ComponentEditor> editors;
	Map<String, bool> multipleObjectEditors;
	Map<String, ComponentTypeDescriptor> types;
};

Result<void> RegisterBuiltinComponentEditors(ComponentEditors& editors);

Result<void> RegisterScriptComponentType(ComponentEditors& editors, const ScriptTypeDescriptor& scriptType);
} // namespace Ursa::Editor
