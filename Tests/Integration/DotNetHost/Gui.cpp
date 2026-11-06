#include "TestSupport.hpp"
#include <Application/SceneFrameRunner.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Application/RuntimeSceneManager.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <nlohmann/json.hpp>

namespace Ursa::Test
{
void TestManagedGui(DotNetHost& host, const VirtualPath& content)
{
	Scene scene;
	ManagedSceneApi api(scene);
	const auto session = Take(host.CreateScriptSession(false, &api));
	Done(host.LoadScriptContent(session, content));
	ManagedScriptRuntime runtime(scene, host, api, session);
	auto object = Take(scene.CreateGameObject("GUI probe"));
	const auto component = Take(runtime.CreateAndAdd(object, "Ursa.SessionFixtures.GuiProbe, Ursa.SessionFixtures"));
	auto* script = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(component));
	const auto fields = [&]()
	{ return nlohmann::json::parse(ToStdString(Take(host.SerializeBehaviour(script->InstanceToken()))))["Fields"]; };
	SceneFrameRunner runner;
	Gui::Session gui;
	Gui::Event down;
	down.type = Gui::EventType::MouseDown;
	down.mousePosition = {20, 20};
	Gui::Event up = down;
	up.type = Gui::EventType::MouseUp;
	Gui::Event key;
	key.type = Gui::EventType::KeyDown;
	key.key = Key::W;
	FrameInput input;
	input.keys[static_cast<Size>(Key::W)] = true;
	Done(runner.TickGui(scene, 0.016f, nullptr, input, gui, {down, up, key}, {320, 240}));
	auto state = fields();
	Expect(state["callbacks"] == 8 && state["layouts"] == 4 && state["repaints"] == 1 && state["clicks"] == 1 &&
	           state["updates"] == 1 && state["repaintObservedUpdate"] == 1 && state["staleRejected"] == 7 &&
	           state["consumedW"] == 1,
	       "Managed OnGUI sequence, normal ExitGUI, consumption or stale-wrapper rejection failed");
	Expect(Take(scene.IsComponentEnabled(component)), "Normal managed ExitGUI disabled the behaviour");
	const auto simulationTime = runner.ElapsedSeconds();
	Done(runner.TickGui(scene, 100, nullptr, input, gui, {}, {320, 240}, false));
	state = fields();
	Expect(state["callbacks"] == 10 && state["updates"] == 1 && state["repaints"] == 2 &&
	           runner.ElapsedSeconds() == simulationTime,
	       "Managed paused OnGUI advanced simulation or omitted repaint");
	auto badOwner = Take(scene.CreateGameObject("Expected failing GUI"));
	const auto badComponent =
	    Take(runtime.CreateAndAdd(badOwner, "Ursa.SessionFixtures.GuiProbe, Ursa.SessionFixtures"));
	auto* bad = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(badComponent));
	auto badFields = nlohmann::json::parse(ToStdString(Take(host.SerializeBehaviour(bad->InstanceToken()))));
	badFields["Fields"]["failInGui"] = true;
	Done(host.DeserializeBehaviour(bad->InstanceToken(), ToUrsaString(badFields.dump())));
	Done(runner.TickGui(scene, 0, nullptr, {}, gui, {}, {320, 240}, false));
	Expect(!Take(scene.IsComponentEnabled(badComponent)) && Take(scene.IsComponentEnabled(component)) &&
	           !gui.Commands().empty(),
	       "A real managed GUI exception leaked nested scope state or disabled another behaviour");
	auto reloadPayload = nlohmann::json::parse(ToStdString(Take(host.SerializeBehaviour(script->InstanceToken()))));
	const nlohmann::json assetReference = {{"$ursaReference",
	                                        {{"Kind", "ScriptableObject"},
	                                         {"Id", ""},
	                                         {"Resource", AssetReferenceToJson({ResourceId::Generate(), 1001})}}}};
	reloadPayload["Fields"]["settings"] = assetReference;
	reloadPayload["Fields"]["settingsList"] = nlohmann::json::array({assetReference, nullptr});
	scene.Clear();
	Done(host.CloseScriptSession(session));
	const auto editingSession = Take(host.CreateScriptSession(true, &api));
	Done(host.LoadScriptContent(editingSession, content));
	const auto snapshots =
	    nlohmann::json::array({{{"Id", ToStdString(ResourceId::Generate().ToString())}, {"Payload", reloadPayload}}});
	const auto restored = nlohmann::json::parse(
	    ToStdString(Take(host.PrepareScriptReload(editingSession, ToUrsaString(snapshots.dump())))));
	Expect(restored[0]["Payload"]["Fields"]["settings"] == assetReference &&
	           restored[0]["Payload"]["Fields"]["settingsList"] == reloadPayload["Fields"]["settingsList"],
	       "Editor reload normalization erased deferred ScriptableObject identities or null array slots");
	Done(host.CloseScriptSession(editingSession));

	Scene exitScene;
	ManagedSceneApi exitApi(exitScene);
	const auto exitSession = Take(host.CreateScriptSession(false, &exitApi));
	Done(host.LoadScriptContent(exitSession, content));
	ManagedScriptRuntime exitRuntime(exitScene, host, exitApi, exitSession);
	auto exitOwner = Take(exitScene.CreateGameObject("Swallowed ExitGUI"));
	const auto exitComponent =
	    Take(exitRuntime.CreateAndAdd(exitOwner, "Ursa.SessionFixtures.GuiSwallowedExitProbe, Ursa.SessionFixtures"));
	Gui::Session exitGui;
	SceneFrameRunner exitRunner;
	for (int i = 0; i < 3; ++i)
	{
		Done(exitRunner.TickGui(exitScene, 0.016f, nullptr, {}, exitGui, {}, {320, 240}));
	}
	Expect(Take(exitScene.IsComponentEnabled(exitComponent)) && exitGui.Commands().empty(),
	       "Swallowed ExitGUI resumed drawing or disabled a script");
	exitScene.Clear();
	Done(host.CloseScriptSession(exitSession));

	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	auto resources = Take(ResourceSession::Open(files));
	Scene switching;
	ManagedSceneApi switchingApi(switching, resources.get());
	const auto switchingSession = Take(host.CreateScriptSession(false, &switchingApi));
	Done(host.LoadScriptContent(switchingSession, content));
	ManagedScriptRuntime switchingRuntime(switching, host, switchingApi, switchingSession);
	auto firstOwner = Take(switching.CreateGameObject("Menu"));
	const auto firstComponent =
	    Take(switchingRuntime.CreateAndAdd(firstOwner, "Ursa.SessionFixtures.SceneSwitchProbe, Ursa.SessionFixtures"));
	const auto serialize = [&](Scene& graph)
	{
		auto* behaviour = graph.RootGameObjects()[0]->FindComponent<ScriptBehaviourComponent>();
		return nlohmann::json::parse(ToStdString(Take(host.SerializeBehaviour(behaviour->InstanceToken()))));
	};
	auto payload = serialize(switching);
	SceneBuildSettings builds;
	builds.configured = true;
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	for (int i = 0; i < 2; ++i)
	{
		Scene candidate;
		auto owner = Take(candidate.CreateGameObject(i == 0 ? "Menu" : "OnGUI"));
		const auto id = Take(candidate.AddComponent<ScriptBehaviourComponent>(
		    owner, "Ursa.SessionFixtures.SceneSwitchProbe, Ursa.SessionFixtures"));
		payload["Fields"]["destination"] = 1 - i;
		static_cast<ScriptBehaviourComponent*>(candidate.GetComponent(id))
		    ->SetSerializedPayload(ToUrsaString(payload.dump()));
		const auto guid = ResourceId::Generate();
		const auto path = Path(i == 0 ? "prj:Menu.ursascene" : "prj:OnGUI.ursascene");
		Done(SaveSceneResource(files, path, guid, candidate, registry));
		builds.scenes.push_back({{guid, 0}, i == 0 ? "Menu" : "OnGUI", path, i});
	}
	RuntimeSceneManager manager(switching, *resources, builds, builds.scenes[0]);
	switchingApi.SetSceneManager(&manager);
	manager.validateCandidate = [&](Scene& candidate) { return switchingRuntime.ValidateCandidate(candidate); };
	manager.invalidateOldObjects = [&]() { return switchingApi.InvalidateSceneObjects(); };
	manager.notify = [&](const RuntimeScene& info, bool loaded)
	{ return host.NotifySceneChange(switchingSession, info, switchingApi.SessionIdentity(), loaded); };
	SceneFrameRunner switchingRunner;
	Gui::Session switchingGui;
	manager.cancelInput = [&]()
	{
		switchingGui.Reset();
		switchingRunner.ResetAccumulator();
	};
	Done(manager.Initialize({}));
	Expect(serialize(switching)["Fields"]["lifecycle"] == "AELS",
	       "Managed initial SceneLoaded order differs from OnEnable/Start boundary");
	auto cleanupFailure = serialize(switching);
	cleanupFailure["Fields"]["failCleanup"] = true;
	Done(host.DeserializeBehaviour(
	    static_cast<ScriptBehaviourComponent*>(switching.GetComponent(firstComponent))->InstanceToken(),
	    ToUrsaString(cleanupFailure.dump())));
	Done(switchingRunner.TickGui(switching, 0.016f, nullptr, {}, switchingGui, {down, up}, {320, 240}));
	Expect(manager.IsLoading() && switching.GetComponent(firstComponent),
	       "Managed OnGUI replaced its Scene while executing");
	const auto timeBeforeLoad = switchingRunner.ElapsedSeconds();
	Expect(Take(manager.ActivatePending(switchingRunner.CurrentContext(nullptr, {}))),
	       "Managed candidate failed activation");
	auto loaded = serialize(switching)["Fields"];
	Expect(loaded["visits"] == 2 && loaded["staleRejected"] == true && loaded["updates"] == 0 &&
	           loaded["lifecycle"] == "AELSDXUAELS",
	       "Managed scene replacement lost static state, retained old handles or dispatched lifecycle incorrectly");
	Expect(switchingRunner.ElapsedSeconds() == timeBeforeLoad && !switching.GetComponent(firstComponent),
	       "Scene replacement reset time or retained old components");
	Done(switchingRunner.TickGui(switching, 0, nullptr, {}, switchingGui, {}, {320, 240}, false));
	Expect(serialize(switching)["Fields"]["updates"] == 0, "Paused navigation advanced Update");
	const auto menuPath = builds.scenes[0].path;
	auto validDocument = Take(ReadJsonResource(files, menuPath, ResourceKind::Scene));
	auto invalidDocument = validDocument;
	invalidDocument["gameObjects"][0]["components"][0]["data"]["scriptPayload"]["Fields"]["destination"] =
	    "wrong field type";
	Done(WriteJsonResource(files, menuPath, invalidDocument));
	const auto retainedScene = manager.ActiveScene().token;
	Done(manager.Load(0));
	Expect(!Take(manager.ActivatePending({})) && manager.ActiveScene().token == retainedScene &&
	           !manager.LastLoadError().empty() && serialize(switching)["Fields"]["visits"] == 2,
	       "Malformed managed candidate field destroyed the active scene or ran Awake");
	Done(WriteJsonResource(files, menuPath, validDocument));
	invalidDocument = validDocument;
	invalidDocument["gameObjects"][0]["components"][0]["data"]["scriptPayload"]["Fields"]["image"] = {
	    {"$ursaReference",
	     {{"Kind", "Texture"}, {"Id", ""}, {"Resource", AssetReferenceToJson({ResourceId::Generate(), 0})}}}};
	Done(WriteJsonResource(files, menuPath, invalidDocument));
	Done(manager.Load(0));
	Expect(!Take(manager.ActivatePending({})) && manager.ActiveScene().token == retainedScene &&
	           !manager.LastLoadError().empty(),
	       "Missing candidate texture dependency destroyed the active scene");
	Done(WriteJsonResource(files, menuPath, validDocument));
	Done(manager.Load(0));
	Expect(Take(manager.ActivatePending({})), "Managed return navigation failed");
	Expect(serialize(switching)["Fields"]["visits"] == 3, "Return navigation recreated the script domain");
	manager.Close();
	switchingApi.SetSceneManager(nullptr);
	switching.Clear();
	Done(host.CloseScriptSession(switchingSession));
}
} // namespace Ursa::Test
