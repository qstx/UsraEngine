#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <scene/Scene.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void ExpectNear(float actual, float expected)
{
	if (std::abs(actual - expected) > 0.0001f)
	{
		throw std::runtime_error("World transform differs from expected value");
	}
}

void TestHierarchyAndGeneration()
{
	Ursa::Scene scene;
	const auto firstRoot = Take(scene.CreateNode());
	const auto child = Take(scene.CreateNode(firstRoot));
	const auto grandchild = Take(scene.CreateNode(child));
	const auto secondRoot = Take(scene.CreateNode());
	Expect(Take(scene.ParentOf(child)) == firstRoot, "Child parent mismatch");
	Expect(Take(scene.ChildrenOf(firstRoot)).at(0) == child, "Child listing mismatch");
	Expect(!scene.SetParent(firstRoot, grandchild), "Ancestor cycle was accepted");
	Expect(!scene.SetParent(child, child), "Self parenting was accepted");
	Expect(Take(scene.ParentOf(firstRoot)) == Ursa::NodeId{}, "Rejected cycle changed root");
	done(scene.SetParent(child, secondRoot));
	Expect(Take(scene.ParentOf(child)) == secondRoot, "Reparenting failed");
	Expect(Take(scene.ChildrenOf(firstRoot)).empty(), "Old parent retained child");
	done(scene.DestroyNode(secondRoot));
	Expect(!scene.Contains(child) && !scene.Contains(grandchild), "Destroyed subtree remains alive");
	Expect(!scene.DestroyNode(child), "Stale node was destroyed twice");
	const auto reused = Take(scene.CreateNode());
	Expect(!scene.Contains(child), "Reused node revived stale handle");
	Expect(scene.Contains(reused), "Reused node is invalid");
}

void TestTransformAndActivation()
{
	Ursa::Scene scene;
	const auto parent = Take(scene.CreateNode());
	const auto child = Take(scene.CreateNode(parent));
	const auto other = Take(scene.CreateNode());
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
	done(scene.SetEnabled(parent, false));
	Expect(!Take(scene.IsActive(child)), "Disabled parent did not disable child");
	done(scene.SetEnabled(parent, true));
	Expect(Take(scene.IsActive(child)), "Reenabled parent did not activate child");
	done(scene.SetParent(child, other));
	ExpectNear(Take(scene.WorldTransform(child)).At(0, 3), 1.0f);
	ExpectNear(Take(scene.GetLocalTransform(child)).position.x, 1.0f);
}

class TraceComponent final : public Ursa::Component
{
public:
	TraceComponent(Ursa::Vector<Ursa::String>& trace, Ursa::String name) : trace(trace), name(std::move(name))
	{
	}

	Ursa::Result<void> OnStart(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":start");
		return Ursa::Result<void>{};
	}

	Ursa::Result<void> OnUpdate(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":update");
		return {};
	}

	Ursa::Result<void> OnFixedUpdate(const Ursa::FrameContext&) override
	{
		trace.push_back(name + ":fixed");
		return {};
	}

	Ursa::Result<void> OnLateUpdate(const Ursa::FrameContext&) override
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

void TestComponents()
{
	Ursa::Vector<Ursa::String> trace;
	Ursa::Scene scene;
	const auto parent = Take(scene.CreateNode());
	const auto child = Take(scene.CreateNode(parent));
	const auto a = Take(scene.AddComponent<TraceComponent>(parent, trace, "a"));
	const auto b = Take(scene.AddComponent<TraceComponent>(parent, trace, "b"));
	const auto c = Take(scene.AddComponent<TraceComponent>(child, trace, "c"));
	Expect(scene.FindComponents<TraceComponent>(parent).size() == 2, "Same-type components were not retained");
	Expect(scene.FindComponent<TraceComponent>(child) != nullptr, "Child component lookup failed");
	scene.BeginFrame();
	done(scene.Update({}));
	done(scene.FixedUpdate({}));
	done(scene.LateUpdate({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"a:start", "a:update", "b:start", "b:update", "c:start", "c:update",
	                                            "a:fixed", "b:fixed", "c:fixed", "a:late", "b:late", "c:late"}),
	       "Component lifecycle order is wrong");
	trace.clear();
	done(scene.SetComponentEnabled(b, false));
	done(scene.SetEnabled(parent, false));
	scene.BeginFrame();
	done(scene.Update({}));
	Expect(trace.empty(), "Disabled hierarchy still updated");
	done(scene.SetEnabled(parent, true));
	done(scene.Update({}));
	Expect(trace == Ursa::Vector<Ursa::String>({"a:update", "c:update"}), "Reenabled lifecycle was wrong");
	done(scene.RemoveComponent(a));
	done(scene.DestroyNode(parent));
	Expect(!scene.ContainsComponent(a) && !scene.ContainsComponent(b) && !scene.ContainsComponent(c),
	       "Destroyed components remain valid");
	Expect(trace == Ursa::Vector<Ursa::String>({"a:update", "c:update", "a:remove", "c:remove", "b:remove"}),
	       "Cleanup was not called once per component");
}

class ActionComponent final : public Ursa::Component
{
public:
	std::function<Ursa::Result<void>(ActionComponent&, const Ursa::FrameContext&)> action;
	int* updates = nullptr;
	int* removals = nullptr;

	Ursa::Result<void> OnUpdate(const Ursa::FrameContext& context) override
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
	const auto parent = Take(scene.CreateNode());
	const auto child = Take(scene.CreateNode(parent));
	const auto other = Take(scene.CreateNode());
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
		const auto created = Take(scene.CreateNode(other));
		Expect(scene.Contains(created), "Created node handle is unavailable");
		auto added = scene.AddComponent<ActionComponent>(created);
		Expect(static_cast<bool>(added), "Could not add a component to a pending node");
		done(scene.RemoveComponent(self.Id()));
		Expect(!scene.RemoveComponent(self.Id()), "Repeated component removal was accepted");
		done(scene.DestroyNode(parent));
		Expect(!scene.DestroyNode(parent), "Repeated node deletion was accepted");
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
	const auto node = Take(scene.CreateNode());
	const auto id = Take(scene.AddComponent<ActionComponent>(node));
	auto* behavior = scene.FindComponent<ActionComponent>(node);
	int removals = 0;
	behavior->removals = &removals;
	behavior->action = [](ActionComponent&, const Ursa::FrameContext& context)
	{
		if (!context.input.KeyDown(Ursa::Key::space) || context.deltaSeconds != 0.125f || context.frameIndex != 42)
		{
			return Ursa::Result<void>::Failure("Frame input or time was not forwarded");
		}
		return Ursa::Result<void>::Failure("Expected behavior failure");
	};
	Ursa::FrameContext context;
	context.input.keys[static_cast<size_t>(Ursa::Key::space)] = true;
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

void TestPersistentIdsAndOrder()
{
	Ursa::Scene scene;
	const auto first = Take(scene.CreateNode());
	const auto second = Take(scene.CreateNode());
	const auto childA = Take(scene.CreateNode(first));
	const auto childB = Take(scene.CreateNode(first));
	const auto firstId = Take(scene.NodePersistentId(first));
	const auto secondId = Take(scene.NodePersistentId(second));
	Expect(firstId != secondId && Take(scene.NodeByPersistentId(firstId)) == first,
	       "Persistent node IDs are not unique or queryable");
	Expect(!scene.AssignNodePersistentId(second, firstId), "Duplicate persistent node ID was accepted");
	done(scene.ReorderNode(second, 0));
	done(scene.ReorderNode(childB, 0));
	Expect(scene.RootNodes().at(0) == second && Take(scene.ChildrenOf(first)).at(0) == childB,
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
	done(scene.SetComponentNode(componentA, second));
	Expect(Take(scene.ComponentNode(componentA)) == second && scene.GetComponent(componentA)->Node() == second &&
	           Take(scene.ComponentsOf(second)).at(0) == componentA,
	       "Component move did not update owner and order");

	done(scene.DestroyNode(first));
	const auto reused = Take(scene.CreateNode());
	Expect(!scene.NodeByPersistentId(firstId) && Take(scene.NodePersistentId(reused)) != firstId,
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
		TestDeferredChanges();
		TestFrameContextAndErrors();
		TestPersistentIdsAndOrder();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
