#include <Application/RuntimeSceneManager.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Serialization/SceneResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

class Runtime final : public ManagedBehaviourRuntime
{
public:
	Result<ComponentId> AddExisting(const GameObject::Ptr&, const String&, UInt64) override
	{
		return Result<ComponentId>::Failure("unused");
	}

	Result<ComponentId> CreateAndAdd(const GameObject::Ptr&, const String&) override
	{
		return Result<ComponentId>::Failure("unused");
	}

	Result<void> Dispatch(UInt64, BehaviourEvent event, const FrameContext*, Component* component) override
	{
		log.push_back(Take(component->GetGameObject()->Name()) + ":" +
		              ToUrsaString(std::to_string(static_cast<UInt32>(event))));
		return {};
	}

	Result<void> Destroy(UInt64) override
	{
		return {};
	}

	Result<void> Attach(Scene& scene)
	{
		for (const auto& owner : scene.RootGameObjects())
		{
			for (auto* component : owner->Components())
			{
				if (auto* script = dynamic_cast<ScriptBehaviourComponent*>(component))
				{
					auto result = script->AttachManagedRuntime(*this, ++token);
					if (!result)
					{
						return result;
					}
				}
			}
		}
		return {};
	}

	Vector<String> log;
	UInt64 token = 0;
};

void SceneReplacement()
{
	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	SceneBuildSettings builds;
	builds.configured = true;
	for (int i = 0; i < 2; ++i)
	{
		const String name = i == 0 ? "Menu" : "Test";
		Scene graph;
		auto object = Take(graph.CreateGameObject(name));
		auto scriptId = Take(graph.AddComponent<ScriptBehaviourComponent>(object, "Probe"));
		auto* script = static_cast<ScriptBehaviourComponent*>(graph.GetComponent(scriptId));
		script->SetSerializedPayload(R"({"FormatVersion":1,"ScriptType":"Probe","Version":1,"Fields":{}})");
		const auto guid = ResourceId::Generate();
		const auto path = Take(VirtualPath::Parse("prj:" + name + ".ursascene"));
		Done(SaveSceneResource(files, path, guid, graph, registry));
		builds.scenes.push_back({{guid, 0}, name, path, i});
	}
	auto resources = Take(ResourceSession::Open(files));
	ComponentLoadContext context(*resources);
	auto active = Take(LoadSceneResource(files, builds.scenes[0].path, registry, context));
	Runtime runtime;
	Done(runtime.Attach(*active.scene));
	ManagedSceneApi api(*active.scene, resources.get());
	RuntimeSceneManager manager(*active.scene, *resources, builds, builds.scenes[0]);
	api.SetSceneManager(&manager);
	int cancellations = 0;
	bool rejectCandidate = false;
	bool requestAnother = false;
	bool requestSelf = false;
	manager.cancelInput = [&]() { ++cancellations; };
	manager.invalidateOldObjects = [&]() { return api.InvalidateSceneObjects(); };
	manager.validateCandidate = [&](Scene& candidate)
	{ return rejectCandidate ? Result<void>::Failure("Bad candidate script field") : runtime.Attach(candidate); };
	manager.notify = [&](const RuntimeScene& scene, bool loaded) -> Result<void>
	{
		runtime.log.push_back(scene.asset.name + (loaded ? ":loaded" : ":unloaded"));
		if (loaded && requestSelf)
		{
			requestSelf = false;
			return manager.Load(scene.asset.buildIndex);
		}
		if (loaded && requestAnother)
		{
			requestAnother = false;
			return manager.Load(0);
		}
		return {};
	};
	FrameContext frame;
	frame.frameIndex = 42;
	frame.elapsedSeconds = 9;
	Done(manager.Initialize(frame));
	Expect(runtime.log == Vector<String>{"Menu:0", "Menu:1", "Menu:loaded", "Menu:2"},
	       "Initial sceneLoaded was not between OnEnable and Start");
	runtime.log.clear();
	auto oldOwner = active.scene->RootGameObjects()[0];
	const auto oldComponent = oldOwner->Components()[0]->Id();
	const auto oldHandle = Take(api.Register(oldOwner));
	const auto oldScene = manager.ActiveScene();
	Done(manager.Load(1));
	Done(manager.Load(1));
	Expect(!manager.Load(0) && manager.IsLoading(), "Different pending target replaced the requested Scene");
	Done(manager.Load(1));
	Expect(manager.LastLoadError().empty() && manager.ActiveScene().token == oldScene.token &&
	           active.scene->Contains(oldOwner),
	       "LoadScene switched immediately or did not clear a corrected error");
	rejectCandidate = true;
	Expect(!Take(manager.ActivatePending(frame)) && manager.ActiveScene().token == oldScene.token &&
	           runtime.log.empty() && !manager.IsLoading() && !manager.LastLoadError().empty(),
	       "Candidate validation failure changed the active scene or ran lifecycle");
	Expect(cancellations == 0 && active.scene->Contains(oldOwner),
	       "Failed preparation canceled active input or destroyed current objects");
	rejectCandidate = false;
	Done(manager.Load(1));
	Expect(Take(manager.ActivatePending(frame)), "A valid candidate did not activate");
	Expect(runtime.log ==
	           Vector<String>{"Menu:6", "Menu:7", "Menu:unloaded", "Test:0", "Test:1", "Test:loaded", "Test:2"},
	       "Single scene replacement lifecycle order was incorrect");
	Expect(!manager.IsLoaded(oldScene.token) && manager.ActiveScene().asset.buildIndex == 1 && cancellations == 1 &&
	           !active.scene->Contains(oldOwner) && !active.scene->GetComponent(oldComponent),
	       "Scene generations did not invalidate old graph identities");
	bool staleRejected = false;
	try
	{
		api.CheckHandle(&oldHandle, sizeof(oldHandle), static_cast<UInt32>(ManagedHandleKind::GameObject));
	}
	catch (const std::exception&)
	{
		staleRejected = true;
	}
	Expect(staleRejected, "Old Scene GameObject handle remained valid after graph replacement");
	Expect(!manager.Load(0, 1), "Unsupported Additive silently performed Single loading");
	runtime.log.clear();
	requestAnother = true;
	Done(manager.Load(1));
	Expect(Take(manager.ActivatePending(frame)) && manager.IsLoading() && manager.ActiveScene().asset.buildIndex == 1,
	       "SceneLoaded request activated recursively");
	Expect(Take(manager.ActivatePending(frame)) && manager.ActiveScene().asset.buildIndex == 0,
	       "SceneLoaded request did not wait for the next boundary");
	Expect(frame.frameIndex == 42 && frame.elapsedSeconds == 9, "Scene activation mutated global simulation time");
	requestSelf = true;
	Done(manager.Load(1));
	Expect(Take(manager.ActivatePending(frame)) && manager.IsLoading(),
	       "A SceneLoaded self-reload was incorrectly swallowed as an older pending request");
	const auto beforeSelfReload = manager.ActiveScene().token;
	Expect(Take(manager.ActivatePending(frame)) && !manager.IsLoading() &&
	           manager.ActiveScene().token != beforeSelfReload,
	       "A SceneLoaded self-reload did not wait for the next activation boundary");
	manager.invalidateOldObjects = []() { return Result<void>::Failure("Expected fatal host invalidation failure"); };
	Done(manager.Load(1));
	Expect(!manager.ActivatePending(frame) && active.scene->RootGameObjects().empty() && !manager.ActiveScene().loaded,
	       "A fatal native commit failure pretended to roll back to the already destroyed old scene");
	active.scene->Clear();
}
} // namespace

int main()
{
	try
	{
		SceneReplacement();
		std::cout << "Single Scene queue, preparation failure, generations and lifecycle passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
