#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <scene/Scene.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
    return result.value;
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void expectNear(float actual, float expected) {
    if (std::abs(actual - expected) > 0.0001f)
        throw std::runtime_error("World transform differs from expected value");
}

void testHierarchyAndGeneration() {
    ursa::Scene scene;
    const auto firstRoot = take(scene.createNode());
    const auto child = take(scene.createNode(firstRoot));
    const auto grandchild = take(scene.createNode(child));
    const auto secondRoot = take(scene.createNode());
    expect(take(scene.parentOf(child)) == firstRoot, "Child parent mismatch");
    expect(take(scene.childrenOf(firstRoot)).at(0) == child, "Child listing mismatch");
    expect(!scene.setParent(firstRoot, grandchild), "Ancestor cycle was accepted");
    expect(!scene.setParent(child, child), "Self parenting was accepted");
    expect(take(scene.parentOf(firstRoot)) == ursa::NodeId{}, "Rejected cycle changed root");
    done(scene.setParent(child, secondRoot));
    expect(take(scene.parentOf(child)) == secondRoot, "Reparenting failed");
    expect(take(scene.childrenOf(firstRoot)).empty(), "Old parent retained child");
    done(scene.destroyNode(secondRoot));
    expect(!scene.contains(child) && !scene.contains(grandchild), "Destroyed subtree remains alive");
    expect(!scene.destroyNode(child), "Stale node was destroyed twice");
    const auto reused = take(scene.createNode());
    expect(!scene.contains(child), "Reused node revived stale handle");
    expect(scene.contains(reused), "Reused node is invalid");
}

void testTransformAndActivation() {
    ursa::Scene scene;
    const auto parent = take(scene.createNode());
    const auto child = take(scene.createNode(parent));
    const auto other = take(scene.createNode());
    ursa::LocalTransform parentTransform;
    parentTransform.position = {2.0f, 0.0f, 0.0f};
    parentTransform.rotation = ursa::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.57079633f);
    done(scene.setLocalTransform(parent, parentTransform));
    ursa::LocalTransform childTransform;
    childTransform.position = {1.0f, 0.0f, 0.0f};
    done(scene.setLocalTransform(child, childTransform));
    const auto world = take(scene.worldTransform(child));
    expectNear(world.at(0, 3), 2.0f);
    expectNear(world.at(2, 3), -1.0f);
    done(scene.setEnabled(parent, false));
    expect(!take(scene.isActive(child)), "Disabled parent did not disable child");
    done(scene.setEnabled(parent, true));
    expect(take(scene.isActive(child)), "Reenabled parent did not activate child");
    done(scene.setParent(child, other));
    expectNear(take(scene.worldTransform(child)).at(0, 3), 1.0f);
    expectNear(take(scene.localTransform(child)).position.x, 1.0f);
}

class TraceComponent final : public ursa::Component {
public:
    TraceComponent(ursa::Vector<ursa::String> &trace, ursa::String name) : trace_(trace), name_(std::move(name)) {
    }

    ursa::Result<void> onStart(const ursa::FrameContext &) override {
        trace_.push_back(name_ + ":start");
        return ursa::Result<void>{};
    }

    ursa::Result<void> onUpdate(const ursa::FrameContext &) override {
        trace_.push_back(name_ + ":update");
        return {};
    }

    ursa::Result<void> onFixedUpdate(const ursa::FrameContext &) override {
        trace_.push_back(name_ + ":fixed");
        return {};
    }

    ursa::Result<void> onLateUpdate(const ursa::FrameContext &) override {
        trace_.push_back(name_ + ":late");
        return {};
    }

    void onRemove() override {
        trace_.push_back(name_ + ":remove");
    }

private:
    ursa::Vector<ursa::String> &trace_;
    ursa::String name_;
};

void testComponents() {
    ursa::Vector<ursa::String> trace;
    ursa::Scene scene;
    const auto parent = take(scene.createNode());
    const auto child = take(scene.createNode(parent));
    const auto a = take(scene.addComponent<TraceComponent>(parent, trace, "a"));
    const auto b = take(scene.addComponent<TraceComponent>(parent, trace, "b"));
    const auto c = take(scene.addComponent<TraceComponent>(child, trace, "c"));
    expect(scene.findComponents<TraceComponent>(parent).size() == 2, "Same-type components were not retained");
    expect(scene.findComponent<TraceComponent>(child) != nullptr, "Child component lookup failed");
    scene.beginFrame();
    done(scene.update({}));
    done(scene.fixedUpdate({}));
    done(scene.lateUpdate({}));
    expect(trace == ursa::Vector<ursa::String>({"a:start", "a:update", "b:start", "b:update", "c:start", "c:update",
                                                "a:fixed", "b:fixed", "c:fixed", "a:late", "b:late", "c:late"}),
           "Component lifecycle order is wrong");
    trace.clear();
    done(scene.setComponentEnabled(b, false));
    done(scene.setEnabled(parent, false));
    scene.beginFrame();
    done(scene.update({}));
    expect(trace.empty(), "Disabled hierarchy still updated");
    done(scene.setEnabled(parent, true));
    done(scene.update({}));
    expect(trace == ursa::Vector<ursa::String>({"a:update", "c:update"}), "Reenabled lifecycle was wrong");
    done(scene.removeComponent(a));
    done(scene.destroyNode(parent));
    expect(!scene.containsComponent(a) && !scene.containsComponent(b) && !scene.containsComponent(c),
           "Destroyed components remain valid");
    expect(trace == ursa::Vector<ursa::String>({"a:update", "c:update", "a:remove", "c:remove", "b:remove"}),
           "Cleanup was not called once per component");
}

class ActionComponent final : public ursa::Component {
public:
    std::function<ursa::Result<void>(ActionComponent &, const ursa::FrameContext &)> action;
    int *updates = nullptr;
    int *removals = nullptr;

    ursa::Result<void> onUpdate(const ursa::FrameContext &context) override {
        if (updates)
            ++*updates;
        return action ? action(*this, context) : ursa::Result<void>{};
    }

    void onRemove() override {
        if (removals)
            ++*removals;
    }
};

void testDeferredChanges() {
    ursa::Scene scene;
    const auto parent = take(scene.createNode());
    const auto child = take(scene.createNode(parent));
    const auto other = take(scene.createNode());
    int childUpdates = 0;
    int childRemovals = 0;
    auto childId = take(scene.addComponent<ActionComponent>(child));
    auto *childBehavior = scene.findComponent<ActionComponent>(child);
    childBehavior->updates = &childUpdates;
    childBehavior->removals = &childRemovals;
    int newUpdates = 0;
    const auto actor = take(scene.addComponent<ActionComponent>(parent));
    auto *behavior = scene.findComponent<ActionComponent>(parent);
    behavior->action = [&](ActionComponent &self, const ursa::FrameContext &) {
        expect(!scene.setParent(parent, child), "Cycle was accepted during an update");
        expect(!scene.setParent(parent, parent), "Self cycle was accepted during an update");
        const auto newId = take(scene.addComponent<ActionComponent>(other));
        (void)newId;
        scene.findComponent<ActionComponent>(other);
        const auto created = take(scene.createNode(other));
        expect(scene.contains(created), "Created node handle is unavailable");
        auto added = scene.addComponent<ActionComponent>(created);
        expect(static_cast<bool>(added), "Could not add a component to a pending node");
        done(scene.removeComponent(self.id()));
        expect(!scene.removeComponent(self.id()), "Repeated component removal was accepted");
        done(scene.destroyNode(parent));
        expect(!scene.destroyNode(parent), "Repeated node deletion was accepted");
        expect(!scene.contains(parent), "Deleted node was not invalidated immediately");
        expect(!scene.containsComponent(childId), "Child component remained callable after parent deletion");
        return ursa::Result<void>{};
    };
    // The parent component executes before the child even though it was attached later.
    scene.beginFrame();
    done(scene.update({}));
    expect(childUpdates == 0, "Deleted child still updated in the same phase");
    expect(childRemovals == 1, "Deleted child was not cleaned once");
    expect(!scene.containsComponent(actor), "Self-removing component remains valid");
    expect(scene.findComponents<ActionComponent>(other).size() == 1, "Deferred component was not committed");
    auto *newBehavior = scene.findComponent<ActionComponent>(other);
    newBehavior->updates = &newUpdates;
    done(scene.fixedUpdate({}));
    done(scene.lateUpdate({}));
    expect(newUpdates == 0, "New component ran in its creation frame");
    scene.beginFrame();
    done(scene.update({}));
    expect(newUpdates == 1, "New component did not start on the following frame");
}

void testFrameContextAndErrors() {
    ursa::Scene scene;
    const auto node = take(scene.createNode());
    const auto id = take(scene.addComponent<ActionComponent>(node));
    auto *behavior = scene.findComponent<ActionComponent>(node);
    int removals = 0;
    behavior->removals = &removals;
    behavior->action = [](ActionComponent &, const ursa::FrameContext &context) {
        if (!context.input.keyDown(ursa::Key::Space) || context.deltaSeconds != 0.125f || context.frameIndex != 42)
            return ursa::Result<void>::failure("Frame input or time was not forwarded");
        return ursa::Result<void>::failure("Expected behavior failure");
    };
    ursa::FrameContext context;
    context.input.keys[static_cast<size_t>(ursa::Key::Space)] = true;
    context.deltaSeconds = 0.125f;
    context.frameIndex = 42;
    scene.beginFrame();
    const auto result = scene.update(context);
    expect(!result && result.error.find("Expected behavior failure") != ursa::String::npos,
           "Behavior error was not propagated with diagnostic context");
    done(scene.removeComponent(id));
    scene.clear();
    expect(removals == 1, "Failed behavior cleanup ran more than once");
}

void testPersistentIdsAndOrder() {
    ursa::Scene scene;
    const auto first = take(scene.createNode());
    const auto second = take(scene.createNode());
    const auto childA = take(scene.createNode(first));
    const auto childB = take(scene.createNode(first));
    const auto firstId = take(scene.nodePersistentId(first));
    const auto secondId = take(scene.nodePersistentId(second));
    expect(firstId != secondId && take(scene.nodeByPersistentId(firstId)) == first,
           "Persistent node IDs are not unique or queryable");
    expect(!scene.assignNodePersistentId(second, firstId), "Duplicate persistent node ID was accepted");
    done(scene.reorderNode(second, 0));
    done(scene.reorderNode(childB, 0));
    expect(scene.rootNodes().at(0) == second && take(scene.childrenOf(first)).at(0) == childB,
           "Node ordering was not preserved");

    const auto componentA = take(scene.addComponent<ActionComponent>(first));
    const auto componentB = take(scene.addComponent<ActionComponent>(first));
    const auto componentId = take(scene.componentPersistentId(componentA));
    expect(take(scene.componentByPersistentId(componentId)) == componentA && scene.component(componentA) != nullptr,
           "Persistent component ID or type-erased lookup failed");
    expect(!scene.assignComponentPersistentId(componentB, componentId),
           "Duplicate persistent component ID was accepted");
    done(scene.reorderComponent(componentB, 0));
    expect(take(scene.componentsOf(first)).at(0) == componentB, "Component reorder failed");
    done(scene.setComponentNode(componentA, second));
    expect(take(scene.componentNode(componentA)) == second && scene.component(componentA)->node() == second &&
               take(scene.componentsOf(second)).at(0) == componentA,
           "Component move did not update owner and order");

    done(scene.destroyNode(first));
    const auto reused = take(scene.createNode());
    expect(!scene.nodeByPersistentId(firstId) && take(scene.nodePersistentId(reused)) != firstId,
           "Reused node slot retained old persistent identity");
}
} // namespace

int main() {
    try {
        testHierarchyAndGeneration();
        testTransformAndActivation();
        testComponents();
        testDeferredChanges();
        testFrameContextAndErrors();
        testPersistentIdsAndOrder();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
