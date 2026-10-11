#include <Scripting/ManagedBehaviourRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <algorithm>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class MockManagedBehaviourRuntime final : public ManagedBehaviourRuntime
{
public:
	Result<ComponentId> AddExisting(const GameObject::Ptr&, const String&, UInt64) override
	{
		return Result<ComponentId>::Failure("Not used by this lifecycle fixture");
	}

	Result<ComponentId> CreateAndAdd(const GameObject::Ptr&, const String&) override
	{
		return Result<ComponentId>::Failure("Not used by this lifecycle fixture");
	}

	Result<void> Dispatch(UInt64 token, BehaviourEvent event, const FrameContext* context,
	                      Component* component) override
	{
		Expect(token == expectedToken && component != nullptr && component->GetGameObject() == owner,
		       "Script lifecycle did not retain its native owner/token");
		if (event == BehaviourEvent::Awake || event == BehaviourEvent::Start || event == BehaviourEvent::Update)
		{
			Expect(context != nullptr, "Frame lifecycle callback did not receive frame context");
		}
		events.push_back(event);
		if (event == BehaviourEvent::Update)
		{
			return Result<void>::Failure("synthetic managed exception");
		}
		return {};
	}

	Result<void> Destroy(UInt64 token) override
	{
		++destroyCount;
		return token == expectedToken ? Result<void>{} : Result<void>::Failure("Wrong script token destroyed");
	}

	GameObject::Ptr owner;
	UInt64 expectedToken = 45;
	UInt32 destroyCount = 0;
	Vector<BehaviourEvent> events;
};
} // namespace

int main()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	Scene scene;
	MockManagedBehaviourRuntime runtime;
	auto owner = scene.CreateGameObject("Script owner");
	Expect(static_cast<bool>(owner), "Could not create script owner");
	runtime.owner = owner.value;
	auto added =
	    scene.AddComponent<ScriptBehaviourComponent>(owner.value, runtime, "Tests.Mover", runtime.expectedToken);
	Expect(static_cast<bool>(added), "Could not attach ScriptBehaviourComponent");
	FrameContext frame;
	frame.deltaSeconds = 1.0f / 60.0f;
	frame.fixedDeltaSeconds = 1.0f / 50.0f;
	frame.frameIndex = 1;
	scene.BeginFrame();
	Expect(static_cast<bool>(scene.Update(frame)), "Isolated managed callback failure escaped into Scene::Update");
	auto enabled = scene.IsComponentEnabled(added.value);
	Expect(static_cast<bool>(enabled) && !enabled.value, "Failing managed behavior was not disabled");
	Expect(static_cast<bool>(scene.FixedUpdate(frame)) && static_cast<bool>(scene.LateUpdate(frame)),
	       "Disabled managed behavior disrupted later frame phases");
	Expect(std::count(runtime.events.begin(), runtime.events.end(), BehaviourEvent::Update) == 1 &&
	           std::count(runtime.events.begin(), runtime.events.end(), BehaviourEvent::OnDisable) == 1,
	       "Managed callback failure did not stop updates and disable exactly once");
	Expect(static_cast<bool>(scene.RemoveComponent(added.value)) && static_cast<bool>(scene.CommitChanges()),
	       "Could not remove the managed behavior component");
	Expect(std::count(runtime.events.begin(), runtime.events.end(), BehaviourEvent::OnDestroy) == 1 &&
	           runtime.destroyCount == 1,
	       "Managed behavior destruction and token cleanup were not called exactly once");
	return 0;
}
