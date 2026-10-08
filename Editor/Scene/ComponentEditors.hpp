#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/Scene.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
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
	Scene* scene = nullptr;
	Ursa::PrefabAsset* prefab = nullptr;
	ComponentLoadContext* loadContext = nullptr;
};

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

	void ReplaceEditor(const String& type, ComponentEditor editor);

	void UnregisterType(const String& type);

	const ComponentEditor* Find(const String& type) const;

	const ComponentTypeDescriptor* FindType(const String& type) const;

	Vector<ComponentTypeDescriptor> Types() const;

	Result<void> CanCreate(const String& type, const ComponentInspectorContext& context) const;

	Result<Component*> Create(const String& type, ComponentInspectorContext& context) const;

private:
	Map<String, ComponentEditor> editors;
	Map<String, ComponentTypeDescriptor> types;
};

Result<void> RegisterBuiltinComponentEditors(ComponentEditors& editors);

Result<void> RegisterScriptComponentType(ComponentEditors& editors, const ScriptTypeDescriptor& scriptType);
} // namespace Ursa::Editor
