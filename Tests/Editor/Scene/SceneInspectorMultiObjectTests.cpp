#include <Core/StringInterop.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scene/Scene.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class BatchProbeComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "test.BatchProbe";
	}

	Result<void> Awake(const FrameContext&) override
	{
		++awakeCount;
		return {};
	}

	static int awakeCount;
};

int BatchProbeComponent::awakeCount = 0;

void TestBatchComponentTransactions()
{
	ComponentEditors editors;
	int factoryCalls = 0;
	bool failSecond = true;
	bool rejectSecond = false;
	Done(editors.RegisterType({"test.BatchProbe", "Batch Probe", "Tests",
	                           [&](ComponentInspectorContext& context) -> Result<Component*>
	                           {
		                           ++factoryCalls;
		                           auto component = context.object->AddComponent<BatchProbeComponent>();
		                           if (!component)
		                           {
			                           return Result<Component*>::Failure(component.error);
		                           }
		                           if (failSecond && factoryCalls == 2)
		                           {
			                           return Result<Component*>::Failure("injected second factory failure");
		                           }
		                           return Result<Component*>::Success(component.value);
	                           },
	                           [&](const ComponentInspectorContext& context) -> Result<void>
	                           {
		                           if (rejectSecond && context.object->Name().value == "Second")
		                           {
			                           return Result<void>::Failure("second target is ineligible");
		                           }
		                           return {};
	                           }}));
	auto first = GameObject::Create("First");
	auto second = GameObject::Create("Second");
	rejectSecond = true;
	const auto ineligible = AddComponentToSelection({first, second}, editors, "test.BatchProbe");
	Expect(!ineligible && factoryCalls == 0 && first->Components().empty() && second->Components().empty(),
	       "A target eligibility failure ran factories or partially changed the selection");
	rejectSecond = false;
	const auto failed = AddComponentToSelection({first, second}, editors, "test.BatchProbe");
	Expect(!failed && first->Components().empty() && second->Components().empty() &&
	           BatchProbeComponent::awakeCount == 0,
	       "A later component factory failure partially changed the selection or ran Awake");
	failSecond = false;
	factoryCalls = 0;
	Done(AddComponentToSelection({first, second}, editors, "test.BatchProbe"));
	Expect(first->Components().size() == 1 && second->Components().size() == 1 && BatchProbeComponent::awakeCount == 0,
	       "Batch candidates were not committed together without invoking game lifecycle callbacks");
	auto mapping = Take(BuildCommonComponentMapping({first, second}));
	Expect(mapping.size() == 1, "Newly added components were not mapped as common components");
	ComponentTargetGroup invalidMapping = mapping.front();
	invalidMapping.targets[1] = invalidMapping.targets[0];
	const auto refusedRemoval = RemoveCommonComponentFromSelection({first, second}, invalidMapping);
	Expect(!refusedRemoval && first->Components().size() == 1 && second->Components().size() == 1,
	       "An invalid removal target partially removed components before validation completed");
	Done(RemoveCommonComponentFromSelection({first, second}, mapping.front()));
	Expect(first->Components().empty() && second->Components().empty(),
	       "Common component removal did not remove the mapped component from every selected object");
}

void TestDeletePreflight()
{
	const auto resourceRoot = std::filesystem::temp_directory_path() /
	                          ("ursa_delete_preflight_" + ToStdString(ResourceId::Generate().ToString()));
	std::filesystem::create_directories(resourceRoot);
	auto resources = Take(ResourceSession::Open(resourceRoot));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto draft = Take(SceneDraft::Create(ResourceKind::Scene, registry, *resources));
	Scene& scene = draft->GetScene();
	const auto sceneRoot = scene.RootGameObjects().front();
	const auto parent = Take(scene.CreateGameObject("Parent", sceneRoot));
	const auto child = Take(scene.CreateGameObject("Child", parent));
	const auto external = Take(scene.CreateGameObject("External", sceneRoot));
	const auto externalId = Take(scene.PersistentId(external));
	auto scriptId = Take(scene.AddComponent<Scripting::ScriptBehaviourComponent>(external, String("Test")));
	auto* script = static_cast<Scripting::ScriptBehaviourComponent*>(scene.GetComponent(scriptId));
	nlohmann::json payload = nlohmann::json::object();
	payload["FormatVersion"] = 1u;
	payload["ScriptType"] = "Test";
	payload["Version"] = 1u;
	payload["Fields"] = nlohmann::json::object();
	payload["Fields"]["target"]["$ursaReference"]["Kind"] = "GameObject";
	payload["Fields"]["target"]["$ursaReference"]["Id"] = ToStdString(child->Id().ToString());
	script->SetSerializedPayload(ToUrsaString(payload.dump()));
	const auto blocked = DeleteGameObjectSelection(scene, {parent->Id(), child->Id()}, registry, *draft);
	Expect(!blocked && blocked.error.find("Cannot delete referenced GameObject") != String::npos &&
	           scene.Contains(parent) && scene.Contains(child) && scene.Contains(external),
	       "Delete did not reject an external incoming reference before changing the hierarchy");
	script->SetSerializedPayload("{\"FormatVersion\":1,\"ScriptType\":\"Test\",\"Version\":1,\"Fields\":{}}");
	const auto first = Take(scene.CreateGameObject("First", sceneRoot));
	const auto second = Take(scene.CreateGameObject("Second", sceneRoot));
	auto badScriptId = Take(scene.AddComponent<Scripting::ScriptBehaviourComponent>(second, String("Test")));
	auto* badScript = static_cast<Scripting::ScriptBehaviourComponent*>(scene.GetComponent(badScriptId));
	badScript->SetSerializedPayload("not-json");
	const auto failedBatch = DeleteGameObjectSelection(scene, {first->Id(), second->Id()}, registry, *draft);
	Expect(!failedBatch && scene.Contains(first) && scene.Contains(second),
	       "A batch preflight failure partially deleted earlier objects");
	badScript->SetSerializedPayload("{\"FormatVersion\":1,\"ScriptType\":\"Test\",\"Version\":1,\"Fields\":{}}");
	Done(DeleteGameObjectSelection(scene, {parent->Id(), child->Id(), first->Id(), second->Id()}, registry, *draft));
	Expect(!scene.Contains(parent) && !scene.Contains(child) && !scene.Contains(first) && !scene.Contains(second) &&
	           scene.Contains(external) && external->Id() == externalId,
	       "Parent/child de-duplication failed to delete the subtree exactly once");
	std::filesystem::remove_all(resourceRoot);
}

void TestPrefabDeleteOverrides()
{
	const auto instanceId = Take(ResourceId::Parse("11111111-1111-4111-8111-111111111111"));
	const auto firstTarget = ResourceId::Generate();
	const auto secondTarget = ResourceId::Generate();
	LinkedSceneResource linked;
	linked.document = {
	    {"instances", nlohmann::json::array({{{"id", ToStdString(instanceId.ToString())},
	                                          {"overrides", {{"deletedGameObjects", {"already-deleted"}}}}}})}};
	linked.scopedIds[instanceId.ToString()] = {{"local-first", firstTarget.ToString()},
	                                           {"local-second", secondTarget.ToString()}};
	Done(RecordPrefabDeletionOverrides(linked, {firstTarget, secondTarget}));
	const auto& deleted = linked.document["instances"][0]["overrides"]["deletedGameObjects"];
	Expect(deleted.size() == 3 && deleted[0] == "already-deleted" && deleted[1] == "local-first" &&
	           deleted[2] == "local-second",
	       "Prefab deletion did not preserve and extend the instance's structural override");
	const auto beforeFailure = linked.document;
	const auto thirdTarget = ResourceId::Generate();
	linked.scopedIds["not-a-resource-id"] = {{"local-third", thirdTarget.ToString()}};
	const auto failed = RecordPrefabDeletionOverrides(linked, {firstTarget, thirdTarget});
	Expect(!failed && linked.document == beforeFailure,
	       "A later invalid Prefab instance partially published earlier deletion overrides");
}
} // namespace

int main()
{
	try
	{
		TestDeletePreflight();
		TestPrefabDeleteOverrides();
		TestBatchComponentTransactions();
		Scene scene;
		const auto first = Take(scene.CreateGameObject("First"));
		const auto second = Take(scene.CreateGameObject("Second"));
		LocalTransform firstTransform;
		firstTransform.position = {1, 2, 3};
		LocalTransform secondTransform;
		secondTransform.position = {4, 5, 6};
		Done(scene.SetLocalTransform(first, firstTransform));
		Done(scene.SetLocalTransform(second, secondTransform));

		auto firstProperties = Take(CreateGameObjectSerializedProperties(first));
		auto secondProperties = Take(CreateGameObjectSerializedProperties(second));
		auto mixed = Take(SerializedObjectGroup::Create({firstProperties.get(), secondProperties.get()}, 1));
		const auto position = Take(mixed.DescribeProperty(ToUrsaString("/Fields/position")));
		Expect(position.value("HasMultipleDifferentValues", false), "Different positions should display as mixed");
		Done(mixed.SetValue(ToUrsaString("/Fields/position/0"), 9.0f));
		Done(mixed.Apply());
		const auto firstAfterX = Take(scene.GetLocalTransform(first));
		const auto secondAfterX = Take(scene.GetLocalTransform(second));
		Expect(firstAfterX.position.x == 9 && firstAfterX.position.y == 2 && firstAfterX.position.z == 3,
		       "Editing X should preserve the first object's Y/Z components");
		Expect(secondAfterX.position.x == 9 && secondAfterX.position.y == 5 && secondAfterX.position.z == 6,
		       "Editing X should preserve the second object's Y/Z components");

		Done(mixed.SetValue(ToUrsaString("/Fields/name"), "Shared"));
		Done(mixed.SetValue(ToUrsaString("/Fields/active"), false));
		Done(mixed.Apply());
		Expect(ToStdString(Take(first->Name())) == "Shared" && ToStdString(Take(second->Name())) == "Shared",
		       "Explicit name assignment should update all selected objects");
		Expect(!Take(first->ActiveSelf()) && !Take(second->ActiveSelf()),
		       "Explicit Active assignment should update all selected objects");

		auto mappingFirst = GameObject::Create("Mapping First");
		auto mappingSecond = GameObject::Create("Mapping Second");
		auto firstCamera0 = Take(mappingFirst->AddComponent<CameraComponent>());
		auto firstCamera1 = Take(mappingFirst->AddComponent<CameraComponent>());
		auto firstScript = Take(mappingFirst->AddComponent<Scripting::ScriptBehaviourComponent>("Game.Mover"));
		Take(mappingFirst->AddComponent<Scripting::ScriptBehaviourComponent>("Missing.Primary"));
		auto secondScript = Take(mappingSecond->AddComponent<Scripting::ScriptBehaviourComponent>("Game.Mover"));
		auto secondCamera = Take(mappingSecond->AddComponent<CameraComponent>());
		auto secondScript2 = Take(mappingSecond->AddComponent<Scripting::ScriptBehaviourComponent>("Game.Mover"));
		Take(mappingSecond->AddComponent<Scripting::ScriptBehaviourComponent>("Missing.Secondary"));
		auto componentGroups = Take(BuildCommonComponentMapping({mappingFirst, mappingSecond}, 1));
		Expect(componentGroups.size() == 2,
		       "Mapping should include only the common Camera and first script occurrence");
		Expect(componentGroups[0].stableType == "script:Game.Mover" && componentGroups[0].sameTypeIndex == 0 &&
		           componentGroups[0].targets == Vector<Component*>{firstScript, secondScript},
		       "Script mapping should use stable script type and primary ordering");
		Expect(componentGroups[1].stableType == "native:ursa.Camera" && componentGroups[1].sameTypeIndex == 0 &&
		           componentGroups[1].targets == Vector<Component*>{firstCamera0, secondCamera},
		       "Native component mapping should ignore component list ordering");
		Expect(firstCamera1 != componentGroups[1].targets[0] && secondScript2 != componentGroups[0].targets[1],
		       "Non-common same-type occurrences must not enter the public mapping");
		ScriptTypeDescriptor moverType;
		moverType.scriptType = "Game.Mover";
		moverType.clrTypeName = "Game.Mover, Game";
		moverType.baseTypeNames = {"Game.MoverBase", "Ursa.Behaviour"};
		const Vector<ScriptTypeDescriptor> scriptTypes{moverType};
		Expect(
		    IsScriptReferenceTypeCompatible(scriptTypes, "Game.Mover", "Game.MoverBase", ScriptTypeKind::Behaviour) &&
		        !IsScriptReferenceTypeCompatible(scriptTypes, "Game.Mover", "Game.MoverBase",
		                                         ScriptTypeKind::ScriptableObject),
		    "Shared script-reference query ignored the declared type kind or inheritance chain");
		ScriptTypeDescriptor settingsType;
		settingsType.scriptType = "Game.Settings";
		settingsType.clrTypeName = "Game.Settings, Game";
		settingsType.kind = ScriptTypeKind::ScriptableObject;
		settingsType.baseTypeNames = {"Game.SettingsBase"};
		Expect(IsScriptReferenceTypeCompatible({settingsType}, "Game.Settings", "Game.SettingsBase",
		                                       ScriptTypeKind::ScriptableObject),
		       "Persistent ScriptableObject reference query rejected a compatible declared base type");
		Expect(CompatibleComponentsForReference(*mappingFirst, "Game.MoverBase", &scriptTypes) ==
		           Vector<Component*>{firstScript},
		       "A script component field did not include a compatible declared base type");
		Expect(CompatibleComponentsForReference(*mappingFirst, "Ursa.CameraComponent", nullptr) ==
		               Vector<Component*>{firstCamera0, firstCamera1} &&
		           CompatibleComponentsForReference(*mappingFirst, "Ursa.LightComponent", nullptr).empty(),
		       "Component references did not distinguish zero, one-class and multiple candidates");
		std::cout << "Scene Inspector multi-object properties test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
