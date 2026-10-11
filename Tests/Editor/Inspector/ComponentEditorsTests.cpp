#include <Scene/ComponentEditors.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Tools/EditorTools.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

class TestComponent final : public Ursa::Component
{
public:
	Ursa::String SerializationType() const override
	{
		return "test.Component";
	}
};

class TestInspectorUI final : public Ursa::Editor::InspectorUI
{
public:
	Ursa::Result<bool> Checkbox(const Ursa::String&, bool& value) override
	{
		value = true;
		return Ursa::Result<bool>::Success(true);
	}

	Ursa::Result<bool> Float(const Ursa::String&, Ursa::Float32& value, Ursa::Float32, Ursa::Float32,
	                         Ursa::Float32) override
	{
		value = 4.0f;
		return Ursa::Result<bool>::Success(true);
	}

	Ursa::Result<bool> Color(const Ursa::String&, Ursa::Color&) override
	{
		return Ursa::Result<bool>::Success(false);
	}

	Ursa::Result<bool> Select(const Ursa::String&, const Ursa::Vector<Ursa::Editor::InspectorOption>&,
	                          Ursa::ResourceId&) override
	{
		return Ursa::Result<bool>::Success(false);
	}

	void Text(const Ursa::String&) override
	{
	}
};
} // namespace

int main()
{
	try
	{
		using namespace Ursa;
		using namespace Ursa::Editor;
		ComponentEditors registry;
		Done(RegisterBuiltinComponentEditors(registry));
		const auto types = registry.Types();
		Expect(types.size() == 4 && registry.FindType("ursa.BoxRender") &&
		           registry.FindType("ursa.BoxRender")->displayName == "Box Renderer" &&
		           registry.FindType("ursa.BoxRender")->category == "Rendering",
		       "Built-in component metadata is incomplete");
		auto files = std::make_shared<VirtualFileSystem>();
		Done(files->MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		auto resources = Take(ResourceSession::Open(*files));
		ComponentLoadContext loadContext(*resources);
		ComponentInspectorContext context;
		context.object = GameObject::Create("Catalog target");
		context.roots = {context.object};
		context.loadContext = &loadContext;
		auto box = Take(registry.Create("ursa.BoxRender", context));
		Expect(box->GetGameObject() == context.object && box->SerializationType() == "ursa.BoxRender",
		       "Component factory attached the wrong type or owner");
		Expect(static_cast<bool>(registry.Create("ursa.BoxRender", context)), "Default component multiplicity changed");
		bool enabled = false;
		Float32 scalar = 0.0f;
		TestInspectorUI ui;
		Done(registry.RegisterType({"test.Component",
		                            "Test Component",
		                            "Test",
		                            [](ComponentInspectorContext& target) -> Result<Component*>
		                            {
			                            auto added = target.object->AddComponent<TestComponent>();
			                            return added ? Result<Component*>::Success(added.value)
			                                         : Result<Component*>::Failure(added.error);
		                            },
		                            {}},
		                           [&](Component&, ComponentInspectorContext&, InspectorUI& controls)
		                           {
			                           auto toggled = controls.Checkbox("Enabled", enabled);
			                           auto changed = controls.Float("Value", scalar, 0.0f, 5.0f, 0.1f);
			                           return toggled && changed
			                                      ? Result<bool>::Success(toggled.value || changed.value)
			                                      : Result<bool>::Failure(!toggled ? toggled.error : changed.error);
		                           }));
		Expect(!registry.CanCreate("missing.Component", context), "Unknown component type was available");
		auto component = Take(registry.Create("test.Component", context));
		const auto* inspector = registry.Find("test.Component");
		Expect(inspector && Take((*inspector)(*component, context, ui)) && enabled && scalar == 4.0f,
		       "Provider-neutral Inspector controls did not edit the component");
		ScriptTypeDescriptor scriptType;
		scriptType.scriptType = "test.editor-script";
		scriptType.version = 1;
		scriptType.clrTypeName = "Tests.EditorBehaviour, Ursa.Game";
		scriptType.fields.push_back({"step", "FloatingPoint", "Unsupported", "System.Single", "1.25", {}, {}});
		Done(RegisterScriptComponentType(registry, scriptType));
		auto scriptComponent = Take(registry.Create(scriptType.clrTypeName, context));
		auto* managedScript = dynamic_cast<Scripting::ScriptBehaviourComponent*>(scriptComponent);
		Expect(managedScript && managedScript->TypeName() == scriptType.clrTypeName,
		       "Add Component did not create a managed script with its generated CLR type");
		auto serialized = Take(SerializedObject::Create(*managedScript));
		const auto step = Take(serialized->FindProperty("/Fields/step"));
		Expect(Take(step.Value()).get<double>() == 1.25,
		       "Managed script Add Component did not create its declared default field values");
		Scene scriptScene;
		const auto scriptSceneObject = Take(scriptScene.CreateGameObject("Script scene target"));
		ComponentInspectorContext scriptSceneContext;
		scriptSceneContext.object = scriptSceneObject;
		scriptSceneContext.scene = &scriptScene;
		const auto sceneScript = Take(registry.Create(scriptType.clrTypeName, scriptSceneContext));
		Expect(sceneScript->GetGameObject() == scriptSceneObject &&
		           dynamic_cast<Scripting::ScriptBehaviourComponent*>(sceneScript) != nullptr,
		       "Managed script factory could not attach a script to a Scene GameObject");
		Done(registry.RegisterType({"test.Incomplete",
		                            "Incomplete",
		                            "Test",
		                            [](ComponentInspectorContext& target) -> Result<Component*>
		                            {
			                            auto added = target.object->AddComponent<TestComponent>();
			                            return added ? Result<Component*>::Failure("simulated factory failure")
			                                         : Result<Component*>::Failure(added.error);
		                            },
		                            {}}));
		const Size before = context.object->Components().size();
		Expect(!registry.Create("test.Incomplete", context) && context.object->Components().size() == before,
		       "Failed component factory left a partial attachment");
		auto document = Take(EditorDocument::Create(
		    ResourceKind::Scene, ResourceId::Generate(), Take(VirtualPath::Parse("prj:tool.ursascene")),
		    [](VirtualFileSystem&, const VirtualPath&, ResourceId) { return Result<void>{}; }));
		auto target = GameObject::Create("Before");
		EditorToolContext toolContext;
		toolContext.document = document.get();
		EditorTools tools;
		Done(tools.Register(
		    {"test.Rename", "Rename Selected", "Tests",
		     [](EditorToolContext& selected, InspectorUI&) -> Result<bool>
		     {
			     auto renamed = selected.selection.front()->SetName("After");
			     if (!renamed)
			     {
				     return Result<bool>::Failure(renamed.error);
			     }
			     auto dirty = selected.MarkDirty();
			     return dirty ? Result<bool>::Success(true) : Result<bool>::Failure(dirty.error);
		     },
		     [](const EditorToolContext& selected)
		     { return selected.selection.empty() ? Result<void>::Failure("Select a GameObject") : Result<void>{}; }}));
		Expect(!tools.CanExecute("test.Rename", toolContext), "Editor tool ran without a selection");
		toolContext.selection.clear();
		Expect(!tools.CanExecute("test.Rename", toolContext), "Editor tool accepted an empty selection");
		toolContext.selection.push_back(target);
		const bool edited = Take(tools.Draw("test.Rename", toolContext, ui));
		Expect(edited && document->Dirty() && Take(target->Name()) == "After",
		       "Editor tool did not edit the selected object through its context");
		ComponentInspectorContext sceneContext;
		sceneContext.object = GameObject::Create("Scene target");
		const auto sceneCamera = Take(registry.Create("ursa.Camera", sceneContext));
		const auto prefabCamera = Take(registry.Create("ursa.Camera", context));
		Expect(sceneCamera->GetGameObject() == sceneContext.object && prefabCamera->GetGameObject() == context.object,
		       "Built-in component creation differed for Scene and detached Prefab objects");
		std::cout << "Component catalog and Inspector API tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
