#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Scene/Scene.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return result.value;
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void ExpectNear(Ursa::Float32 actual, Ursa::Float32 expected)
{
	if (std::abs(actual - expected) > 0.0001f)
	{
		throw std::runtime_error("World transform differs from expected value");
	}
}

void TestHierarchyAndGeneration()
{
	Ursa::Scene scene;
	const auto firstRoot = Take(scene.CreateGameObject());
	const auto child = Take(scene.CreateGameObject(firstRoot));
	const auto grandchild = Take(scene.CreateGameObject(child));
	const auto secondRoot = Take(scene.CreateGameObject());
	Expect(Take(scene.ParentOf(child)) == firstRoot, "Child parent mismatch");
	Expect(Take(scene.ChildrenOf(firstRoot)).at(0) == child, "Child listing mismatch");
	Expect(!scene.SetParent(firstRoot, grandchild), "Ancestor cycle was accepted");
	Expect(!scene.SetParent(child, child), "Self parenting was accepted");
	Expect(Take(scene.ParentOf(firstRoot)) == Ursa::GameObject::Ptr{}, "Rejected cycle changed root");
	done(scene.SetParent(child, secondRoot));
	Expect(Take(scene.ParentOf(child)) == secondRoot, "Reparenting failed");
	Expect(Take(scene.ChildrenOf(firstRoot)).empty(), "Old parent retained child");
	done(scene.DestroyGameObject(secondRoot));
	Expect(!scene.Contains(child) && !scene.Contains(grandchild), "Destroyed subtree remains alive");
	Expect(!scene.DestroyGameObject(child), "Stale node was destroyed twice");
	const auto reused = Take(scene.CreateGameObject());
	Expect(!scene.Contains(child), "Reused node revived stale handle");
	Expect(scene.Contains(reused), "Reused node is invalid");
}

void TestTransformAndActivation()
{
	Ursa::Scene scene;
	const auto parent = Take(scene.CreateGameObject());
	const auto child = Take(scene.CreateGameObject(parent));
	const auto other = Take(scene.CreateGameObject());
	Ursa::LocalTransform parentTransform;
	parentTransform.position = {2.0f, 0.0f, 0.0f};
	parentTransform.rotation = Ursa::Quat::FromAxisAngle({0.0f, 1.0f, 0.0f}, 1.57079633f);
	done(scene.SetLocalTransform(parent, parentTransform));
	Ursa::LocalTransform childTransform;
	childTransform.position = {1.0f, 0.0f, 0.0f};
	done(scene.SetLocalTransform(child, childTransform));
	const auto world = Take(scene.WorldTransform(child));
	ExpectNear(world.At(0, 3), 2.0f);
	ExpectNear(world.At(2, 3), -1.0f);
	done(scene.SetActive(parent, false));
	Expect(!Take(scene.ActiveInHierarchy(child)), "Disabled parent did not disable child");
	done(scene.SetActive(parent, true));
	Expect(Take(scene.ActiveInHierarchy(child)), "Reenabled parent did not activate child");
	done(scene.SetParent(child, other));
	ExpectNear(Take(scene.WorldTransform(child)).At(0, 3), 1.0f);
	ExpectNear(Take(scene.GetLocalTransform(child)).position.x, 1.0f);
}

class TraceComponent final : public Ursa::Component
{
public:
	TraceComponent(Ursa::Vector<Ursa::String>& eventTrace, Ursa::String componentName)
	    : trace(eventTrace), name(std::move(componentName))
	{
	}

	Ursa::Result<void> Start(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":start");
		return Ursa::Result<void>{};
	}

	Ursa::Result<void> Update(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":update");
		return {};
	}

	Ursa::Result<void> FixedUpdate(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":fixed");
		return {};
	}

	Ursa::Result<void> LateUpdate(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":late");
		return {};
	}

	void OnRemove() override
	{
		trace.push_back(name + ":remove");
	}

private:
	Ursa::Vector<Ursa::String>& trace;
	Ursa::String name;
};

class AttachProbe final : public Ursa::Component
{
public:
	AttachProbe(int& startCount, int& updateCount, int& removalCount)
	    : starts(startCount), updates(updateCount), removals(removalCount)
	{
	}

	Ursa::Result<void> Start(const Ursa::FrameContext&) override
	{
		if (!GetGameObject() || !TryGetScene())
		{
			return Ursa::Result<void>::Failure("Attached component could not resolve its owner and Scene");
		}
		++starts;
		return {};
	}

	Ursa::Result<void> Update(const Ursa::FrameContext&) override
	{
		++updates;
		if (action)
		{
			return action();
		}
		return {};
	}

	void OnRemove() override
	{
		++removals;
	}

	std::function<Ursa::Result<void>()> action;

private:
	int& starts;
	int& updates;
	int& removals;
};

void TestComponents()
{
	Ursa::Vector<Ursa::String> trace;
	Ursa::Scene scene;
	const auto parent = Take(scene.CreateGameObject());
	const auto child = Take(scene.CreateGameObject(parent));
	const auto a = Take(scene.AddComponent<TraceComponent>(parent, trace, "a"));
	const auto b = Take(scene.AddComponent<TraceComponent>(parent, trace, "b"));
	const auto c = Take(scene.AddComponent<TraceComponent>(child, trace, "c"));
	Expect(scene.FindComponents<TraceComponent>(parent).size() == 2, "Same-type components were not retained");
	Expect(scene.FindComponent<TraceComponent>(child) != nullptr, "Child component lookup failed");
	scene.BeginFrame();
	done(scene.Update({}));
	done(scene.FixedUpdate({}));
	done(scene.LateUpdate({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"a:start", "b:start", "c:start", "a:update", "b:update", "c:update",
	                                            "a:fixed", "b:fixed", "c:fixed", "a:late", "b:late", "c:late"}),
	       "Component lifecycle order is wrong");
	trace.clear();
	done(scene.SetComponentEnabled(b, false));
	done(scene.SetActive(parent, false));
	scene.BeginFrame();
	done(scene.Update({}));
	Expect(trace.empty(), "Disabled hierarchy still updated");
	done(scene.SetActive(parent, true));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"a:update", "c:update"}), "Reenabled lifecycle was wrong");
	done(scene.RemoveComponent(a));
	done(scene.DestroyGameObject(parent));
	Expect(!scene.ContainsComponent(a) && !scene.ContainsComponent(b) && !scene.ContainsComponent(c),
	       "Destroyed components remain valid");
	Expect(trace == Ursa::Vector<Ursa::String>({"a:update", "c:update", "a:remove", "c:remove", "b:remove"}),
	       "Cleanup was not called once per component");
}

class LifecycleComponent final : public Ursa::Component
{
public:
	LifecycleComponent(Ursa::Vector<Ursa::String>& trace, Ursa::String name) : trace(trace), name(std::move(name))
	{
	}

	Ursa::Result<void> Awake(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":awake");
		return {};
	}

	void OnEnable() override
	{
		trace.push_back(name + ":enable");
	}

	Ursa::Result<void> Start(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":start");
		return {};
	}

	Ursa::Result<void> Update(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":update");
		return {};
	}

	void OnDisable() override
	{
		trace.push_back(name + ":disable");
	}

	void OnDestroy() override
	{
		trace.push_back(name + ":destroy");
	}

	void OnRemove() override
	{
		trace.push_back(name + ":remove");
	}

private:
	Ursa::Vector<Ursa::String>& trace;
	Ursa::String name;
};

class RuntimeCreatorComponent final : public Ursa::Component
{
public:
	RuntimeCreatorComponent(Ursa::Scene& scene, Ursa::Vector<Ursa::String>& trace) : scene(scene), trace(trace)
	{
	}

	Ursa::Result<void> Start(const Ursa::FrameContext&) override
	{
		trace.push_back("creator:start");
		return CreateChild("startChild");
	}

	Ursa::Result<void> Update(const Ursa::FrameContext&) override
	{
		if (!createdDuringUpdate)
		{
			createdDuringUpdate = true;
			trace.push_back("creator:update");
			return CreateChild("updateChild");
		}
		trace.push_back("creator:update");
		return {};
	}

private:
	Ursa::Result<void> CreateChild(const Ursa::String& name)
	{
		auto child = scene.CreateGameObject(name, GetGameObject());
		if (!child)
		{
			return Ursa::Result<void>::Failure(child.error);
		}
		auto component = scene.AddComponent<LifecycleComponent>(child.value, trace, name);
		if (!component)
		{
			return Ursa::Result<void>::Failure(component.error);
		}
		Expect(trace.size() >= 2 && trace[trace.size() - 2] == name + ":awake" && trace.back() == name + ":enable",
		       "Runtime AddComponent returned before Awake/OnEnable");
		return {};
	}

	Ursa::Scene& scene;
	Ursa::Vector<Ursa::String>& trace;
	bool createdDuringUpdate = false;
};

void TestNativeLifecycleTransitions()
{
	Ursa::Scene scene;
	Ursa::Vector<Ursa::String> trace;
	const auto root = Take(scene.CreateGameObject("Root"));
	const auto inactiveChild = Take(scene.CreateGameObject("Inactive", root));
	const auto activeId = Take(scene.AddComponent<LifecycleComponent>(root, trace, "active"));
	const auto disabledId = Take(scene.AddComponent<LifecycleComponent>(root, trace, "disabled"));
	Take(scene.AddComponent<LifecycleComponent>(inactiveChild, trace, "child"));
	done(scene.SetComponentEnabled(disabledId, false));
	done(scene.SetActive(inactiveChild, false));
	Expect(!Take(scene.ActiveSelf(inactiveChild)) && !Take(scene.ActiveInHierarchy(inactiveChild)),
	       "GameObject self and hierarchy activity were not reported separately");

	scene.BeginFrame();
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>(
	                    {"active:awake", "disabled:awake", "active:enable", "active:start", "active:update"}),
	       "Initial lifecycle did not awaken disabled behaviours or defer inactive objects");

	trace.clear();
	done(scene.SetComponentEnabled(disabledId, true));
	done(scene.SetActive(inactiveChild, true));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"child:awake", "disabled:enable", "child:enable", "disabled:start",
	                                            "child:start", "active:update", "disabled:update", "child:update"}),
	       "First activation did not run Awake, OnEnable, and Start in order");

	trace.clear();
	done(scene.SetActive(root, false));
	done(scene.SetActive(root, false));
	Expect(!Take(scene.ActiveSelf(root)) && !Take(scene.ActiveInHierarchy(inactiveChild)),
	       "Disabling the root did not update hierarchy activity");
	done(scene.FixedUpdate({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"active:disable", "disabled:disable", "child:disable"}),
	       "Disabling a parent did not disable its active behaviours exactly once");

	trace.clear();
	done(scene.SetActive(root, true));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"active:enable", "disabled:enable", "child:enable", "active:update",
	                                            "disabled:update", "child:update"}),
	       "Reenabling a parent reran initialization or skipped the enable transition");
	trace.clear();
	done(scene.SetActive(inactiveChild, false));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"child:disable", "active:update", "disabled:update"}),
	       "Setting activeSelf false did not disable only the child hierarchy");
	trace.clear();
	done(scene.SetParent(inactiveChild));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"active:update", "disabled:update"}),
	       "Reparenting did not preserve activeSelf=false");
	trace.clear();
	done(scene.SetActive(inactiveChild, true));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"child:enable", "active:update", "disabled:update", "child:update"}),
	       "Enabling activeSelf did not activate the child behavior");

	const auto inactiveParent = Take(scene.CreateGameObject("Other"));
	done(scene.SetActive(inactiveParent, false));
	trace.clear();
	done(scene.SetParent(inactiveChild, inactiveParent));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"child:disable", "active:update", "disabled:update"}),
	       "Reparenting under an inactive ancestor did not disable the child behavior");
	trace.clear();
	done(scene.SetParent(inactiveChild, root));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"child:enable", "active:update", "disabled:update", "child:update"}),
	       "Reparenting under an active ancestor did not enable the child behavior");

	trace.clear();
	done(scene.DestroyGameObject(root));
	Expect(trace == Ursa::Vector<Ursa::String>({"child:disable", "child:destroy", "child:remove", "active:disable",
	                                            "active:destroy", "active:remove", "disabled:disable",
	                                            "disabled:destroy", "disabled:remove"}),
	       "Scene destruction did not deliver lifecycle events before component cleanup");
	Expect(!scene.ContainsComponent(activeId), "Destroyed lifecycle component remained valid");
}

void TestDestructionCleanupRules()
{
	Ursa::Vector<Ursa::String> trace;
	{
		Ursa::Scene scene;
		const auto root = Take(scene.CreateGameObject("Root"));
		const auto inactive = Take(scene.CreateGameObject("Inactive", root));
		Take(scene.AddComponent<LifecycleComponent>(inactive, trace, "neverActive"));
		done(scene.SetActive(inactive, false));
		scene.BeginFrame();
		done(scene.Update({}));
		trace.clear();
		done(scene.DestroyGameObject(root));
		Expect(trace == Ursa::Vector<Ursa::String>({"neverActive:remove"}),
		       "An object that was never active received game destruction callbacks or skipped native cleanup");
	}

	trace.clear();
	{
		Ursa::Scene scene;
		const auto root = Take(scene.CreateGameObject("Root"));
		Take(scene.AddComponent<LifecycleComponent>(root, trace, "direct"));
		scene.BeginFrame();
		done(scene.Update({}));
		trace.clear();
		done(scene.SetActive(root, false));
		done(scene.DestroyGameObject(root));
		Expect(trace == Ursa::Vector<Ursa::String>({"direct:disable", "direct:destroy", "direct:remove"}),
		       "Direct destruction did not disable and destroy once before releasing native resources");
	}

	trace.clear();
	{
		Ursa::Scene scene;
		const auto root = Take(scene.CreateGameObject("Root"));
		Take(scene.AddComponent<LifecycleComponent>(root, trace, "clear"));
		scene.BeginFrame();
		done(scene.Update({}));
		trace.clear();
		scene.Clear();
		Expect(trace == Ursa::Vector<Ursa::String>({"clear:disable", "clear:destroy", "clear:remove"}),
		       "Scene clear did not perform each lifecycle and native cleanup callback once");
	}
}

void TestRuntimeCreationLifecycleBoundary()
{
	Ursa::Scene scene;
	Ursa::Vector<Ursa::String> trace;
	const auto root = Take(scene.CreateGameObject("Root"));
	Take(scene.AddComponent<RuntimeCreatorComponent>(root, scene, trace));
	scene.BeginFrame();
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"creator:start", "startChild:awake", "startChild:enable",
	                                            "startChild:start", "creator:update", "updateChild:awake",
	                                            "updateChild:enable", "startChild:update", "updateChild:start"}),
	       "Runtime-created components did not initialize at the safe lifecycle boundary");

	trace.clear();
	scene.BeginFrame();
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"creator:update", "startChild:update", "updateChild:update"}),
	       "Runtime-created components missed or repeated their first eligible update");
}

class ActionComponent final : public Ursa::Component
{
public:
	std::function<Ursa::Result<void>(ActionComponent&, const Ursa::FrameContext&)> action;
	int* updates = nullptr;
	int* removals = nullptr;

	Ursa::Result<void> Update(const Ursa::FrameContext& context) override
	{
		if (updates)
		{
			++*updates;
		}
		return action ? action(*this, context) : Ursa::Result<void>{};
	}

	void OnRemove() override
	{
		if (removals)
		{
			++*removals;
		}
	}
};

void TestDeferredChanges()
{
	Ursa::Scene scene;
	const auto parent = Take(scene.CreateGameObject());
	const auto child = Take(scene.CreateGameObject(parent));
	const auto other = Take(scene.CreateGameObject());
	int childUpdates = 0;
	int childRemovals = 0;
	auto childId = Take(scene.AddComponent<ActionComponent>(child));
	auto* childBehavior = scene.FindComponent<ActionComponent>(child);
	childBehavior->updates = &childUpdates;
	childBehavior->removals = &childRemovals;
	int newUpdates = 0;
	const auto actor = Take(scene.AddComponent<ActionComponent>(parent));
	auto* behavior = scene.FindComponent<ActionComponent>(parent);
	behavior->action = [&](ActionComponent& self, const Ursa::FrameContext&)
	{
		Expect(!scene.SetParent(parent, child), "Cycle was accepted during an update");
		Expect(!scene.SetParent(parent, parent), "Self cycle was accepted during an update");
		const auto newId = Take(scene.AddComponent<ActionComponent>(other));
		(void)newId;
		scene.FindComponent<ActionComponent>(other);
		const auto created = Take(scene.CreateGameObject(other));
		Expect(scene.Contains(created), "Created node handle is unavailable");
		auto added = scene.AddComponent<ActionComponent>(created);
		Expect(static_cast<bool>(added), "Could not add a component to a pending node");
		done(scene.RemoveComponent(self.Id()));
		Expect(!scene.RemoveComponent(self.Id()), "Repeated component removal was accepted");
		done(scene.DestroyGameObject(parent));
		Expect(!scene.DestroyGameObject(parent), "Repeated GameObject deletion was accepted");
		Expect(!scene.Contains(parent), "Deleted node was not invalidated immediately");
		Expect(!scene.ContainsComponent(childId), "Child component remained callable after parent deletion");
		return Ursa::Result<void>{};
	};
	// The parent component executes before the child even though it was attached later.
	scene.BeginFrame();
	done(scene.Update({}));
	Expect(childUpdates == 0, "Deleted child still updated in the same phase");
	Expect(childRemovals == 1, "Deleted child was not cleaned once");
	Expect(!scene.ContainsComponent(actor), "Self-removing component remains valid");
	Expect(scene.FindComponents<ActionComponent>(other).size() == 1, "Deferred component was not committed");
	auto* newBehavior = scene.FindComponent<ActionComponent>(other);
	newBehavior->updates = &newUpdates;
	done(scene.FixedUpdate({}));
	done(scene.LateUpdate({}));
	Expect(newUpdates == 0, "New component ran in its creation frame");
	scene.BeginFrame();
	done(scene.Update({}));
	Expect(newUpdates == 1, "New component did not start on the following frame");
}

void TestFrameContextAndErrors()
{
	Ursa::Scene scene;
	const auto node = Take(scene.CreateGameObject());
	const auto id = Take(scene.AddComponent<ActionComponent>(node));
	auto* behavior = scene.FindComponent<ActionComponent>(node);
	int removals = 0;
	behavior->removals = &removals;
	behavior->action = [](ActionComponent&, const Ursa::FrameContext& context)
	{
		if (!context.input.KeyDown(Ursa::Key::Space) || context.deltaSeconds != 0.125f || context.frameIndex != 42)
		{
			return Ursa::Result<void>::Failure("Frame input or time was not forwarded");
		}
		return Ursa::Result<void>::Failure("Expected behavior failure");
	};
	Ursa::FrameContext context;
	context.input.keys[static_cast<Ursa::Size>(Ursa::Key::Space)] = true;
	context.deltaSeconds = 0.125f;
	context.frameIndex = 42;
	scene.BeginFrame();
	const auto result = scene.Update(context);
	Expect(!result && result.error.find("Expected behavior failure") != Ursa::String::npos,
	       "Behavior error was not propagated with diagnostic context");
	done(scene.RemoveComponent(id));
	scene.Clear();
	Expect(removals == 1, "Failed behavior cleanup ran more than once");
}

void TestAttachGameObjectTree()
{
	auto root = Ursa::GameObject::Create("Asset root");
	auto child = Ursa::GameObject::Create("Asset child");
	done(child->SetParent(root));
	int starts = 0;
	int updates = 0;
	int removals = 0;
	auto component = Take(child->AddComponent<AttachProbe>(starts, updates, removals));
	Ursa::Scene scene;
	done(scene.AttachGameObject(root));
	Expect(scene.Contains(root) && scene.Contains(child), "Scene did not attach the complete GameObject tree");
	Expect(component->GetGameObject() == child && component->TryGetScene() == &scene,
	       "Scene attachment did not bind component ownership and context");
	Expect(scene.ContainsComponent(component->Id()), "Scene did not register a preconfigured component");
	auto otherRoot = Ursa::GameObject::Create("Other root");
	done(scene.AttachGameObject(otherRoot));
	done(child->SetParent(otherRoot));
	Expect(root->Children().empty() && child->Parent() == otherRoot,
	       "GameObject reparenting did not update its Scene hierarchy");
	done(child->SetParent(root));
	Expect(otherRoot->Children().empty() && child->Parent() == root,
	       "GameObject could not return to its original Scene parent");
	auto attachedComponent = Take(child->AddComponent<AttachProbe>(starts, updates, removals));
	Expect(attachedComponent->TryGetScene() == &scene && scene.ContainsComponent(attachedComponent->Id()),
	       "Adding a component through an attached GameObject bypassed Scene registration");
	component->action = [&] { return child->SetParent(otherRoot); };
	done(scene.Update({}));
	Expect(starts == 2 && updates == 2 && child->Parent() == otherRoot,
	       "Attached GameObject lifecycle or deferred reparenting was incorrect");
	component->action = {};
	done(child->SetParent(root));
	done(child->RemoveComponent(attachedComponent));
	Expect(removals == 1, "Removing a component through its GameObject did not invoke cleanup");
	done(scene.DestroyGameObject(root));
	Expect(!root->IsValid() && !child->IsValid() && removals == 2,
	       "Scene destruction did not invalidate and clean up the complete GameObject tree");
}

void TestGameObjectRenderCollection()
{
	Ursa::Scene scene;
	auto cameraObject = Ursa::GameObject::Create("Camera");
	Ursa::LocalTransform cameraTransform;
	cameraTransform.position = {0.0f, 0.0f, 5.0f};
	done(cameraObject->SetLocalTransform(cameraTransform));
	auto camera = Take(cameraObject->AddComponent<Ursa::CameraComponent>());
	auto boxObject = Ursa::GameObject::Create("Box");
	Ursa::LocalTransform boxTransform;
	boxTransform.position = {1.0f, 2.0f, 0.0f};
	done(boxObject->SetLocalTransform(boxTransform));
	Take(boxObject->AddComponent<Ursa::BoxRenderComponent>());
	done(scene.AttachGameObject(cameraObject));
	done(scene.AttachGameObject(boxObject));
	done(scene.SetActiveCamera(camera->Id()));
	const auto renderData = Take(scene.CollectRenderData(16.0f / 9.0f));
	Expect(renderData.boxes.size() == 1 && renderData.boxes.front().model.At(0, 3) == 1.0f &&
	           renderData.boxes.front().model.At(1, 3) == 2.0f,
	       "Scene render-data collection did not traverse GameObject transforms and components");
}

void TestPersistentIdsAndOrder()
{
	Ursa::Scene scene;
	const auto first = Take(scene.CreateGameObject());
	const auto second = Take(scene.CreateGameObject());
	const auto childA = Take(scene.CreateGameObject(first));
	const auto childB = Take(scene.CreateGameObject(first));
	const auto firstId = Take(scene.PersistentId(first));
	const auto secondId = Take(scene.PersistentId(second));
	Expect(firstId != secondId && Take(scene.GameObjectById(firstId)) == first,
	       "Persistent node IDs are not unique or queryable");
	Expect(!scene.AssignGameObjectId(second, firstId), "Duplicate persistent GameObject ID was accepted");
	done(scene.ReorderGameObject(second, 0));
	done(scene.ReorderGameObject(childB, 0));
	Expect(scene.RootGameObjects().at(0) == second && Take(scene.ChildrenOf(first)).at(0) == childB,
	       "Node ordering was not preserved");

	const auto componentA = Take(scene.AddComponent<ActionComponent>(first));
	const auto componentB = Take(scene.AddComponent<ActionComponent>(first));
	const auto componentId = Take(scene.ComponentPersistentId(componentA));
	Expect(Take(scene.ComponentByPersistentId(componentId)) == componentA && scene.GetComponent(componentA) != nullptr,
	       "Persistent component ID or type-erased lookup failed");
	Expect(!scene.AssignComponentPersistentId(componentB, componentId),
	       "Duplicate persistent component ID was accepted");
	done(scene.ReorderComponent(componentB, 0));
	Expect(Take(scene.ComponentsOf(first)).at(0) == componentB, "Component reorder failed");
	done(scene.SetComponentOwner(componentA, second));
	Expect(Take(scene.ComponentOwner(componentA)) == second &&
	           scene.GetComponent(componentA)->GetGameObject() == second &&
	           Take(scene.ComponentsOf(second)).at(0) == componentA,
	       "Component move did not update owner and order");

	done(scene.DestroyGameObject(first));
	const auto reused = Take(scene.CreateGameObject());
	Expect(!scene.GameObjectById(firstId) && Take(scene.PersistentId(reused)) != firstId,
	       "Reused node slot retained old persistent identity");
}
} // namespace

int main()
{
	try
	{
		TestHierarchyAndGeneration();
		TestTransformAndActivation();
		TestComponents();
		TestNativeLifecycleTransitions();
		TestRuntimeCreationLifecycleBoundary();
		TestDestructionCleanupRules();
		TestDeferredChanges();
		TestFrameContextAndErrors();
		TestAttachGameObjectTree();
		TestGameObjectRenderCollection();
		TestPersistentIdsAndOrder();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
