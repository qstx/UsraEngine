#include <Core/Types.hpp>
#include <Scene/GameObject.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class TestComponent final : public Ursa::Component
{
public:
	explicit TestComponent(int& removalCount) : removed(removalCount)
	{
	}

	void OnRemove() override
	{
		++removed;
	}

private:
	int& removed;
};

void ExpectNear(Ursa::Float32 actual, Ursa::Float32 expected)
{
	if (std::abs(actual - expected) > 0.0001f)
	{
		throw std::runtime_error("GameObject world transform differs from expected value");
	}
}

void TestIdentityHierarchyAndTransform()
{
	auto root = Ursa::GameObject::Create("Root");
	auto child = Ursa::GameObject::Create("Child");
	auto sibling = Ursa::GameObject::Create("Sibling");
	Expect(root->IsValid() && child->IsValid() && root->Id() && child->Id() && root->Id() != child->Id(),
	       "GameObject identity was missing or reused");

	Ursa::LocalTransform rootTransform;
	rootTransform.position = {2.0f, 0.0f, 0.0f};
	Expect(static_cast<bool>(root->SetLocalTransform(rootTransform)), "Could not set the root transform");
	Ursa::LocalTransform childTransform;
	childTransform.position = {1.0f, 0.0f, 0.0f};
	Expect(static_cast<bool>(child->SetLocalTransform(childTransform)), "Could not set the child transform");
	Expect(static_cast<bool>(child->SetParent(root)), "Could not parent a GameObject");
	Expect(child->Parent() == root && root->Children().size() == 1 && root->Children().front() == child,
	       "GameObject hierarchy did not preserve parent and child references");
	ExpectNear(child->WorldTransform().value.At(0, 3), 3.0f);
	Expect(!root->SetParent(child), "GameObject hierarchy accepted a cycle");
	Expect(!child->SetParent(child), "GameObject hierarchy accepted self-parenting");
	Expect(child->Parent() == root, "Rejected reparenting changed the original hierarchy");
	Expect(static_cast<bool>(child->SetParent(sibling)), "Could not reparent a GameObject");
	Expect(root->Children().empty() && sibling->Children().front() == child && child->Parent() == sibling,
	       "Reparenting did not update both object lists");
	ExpectNear(child->WorldTransform().value.At(0, 3), 1.0f);
}

void TestDetachedComponentsAndInvalidation()
{
	auto root = Ursa::GameObject::Create("Component owner");
	int removed = 0;
	auto added = root->AddComponent<TestComponent>(removed);
	Expect(static_cast<bool>(added), "Could not add a component to a detached GameObject");
	Expect(added.value->GetGameObject() == root, "Component did not report its owning GameObject");
	Expect(added.value->TryGetScene() == nullptr, "Detached component reported a Scene");
	bool rejectedSceneAccess = false;
	try
	{
		added.value->GetScene();
	}
	catch (const std::logic_error&)
	{
		rejectedSceneAccess = true;
	}
	Expect(rejectedSceneAccess, "Detached component allowed unsafe Scene access");
	Expect(root->FindComponent<TestComponent>() == added.value, "GameObject component lookup failed");
	Expect(root->FindComponents<TestComponent>().size() == 1, "GameObject multi-component lookup failed");
	Expect(static_cast<bool>(root->RemoveComponent(added.value)), "Could not remove a GameObject component");
	Expect(removed == 1 && root->FindComponent<TestComponent>() == nullptr,
	       "Removing a component did not clear it and invoke cleanup exactly once");

	auto child = Ursa::GameObject::Create("Child");
	Expect(static_cast<bool>(child->SetParent(root)), "Could not parent the child before destruction");
	Expect(static_cast<bool>(root->Destroy()), "Could not destroy a detached GameObject tree");
	Expect(!root->IsValid() && !child->IsValid(), "Destroyed GameObject handles remained valid");
	Expect(!root->Name() && !child->WorldTransform(), "Destroyed GameObject allowed access to stale data");
}
} // namespace

int main()
{
	try
	{
		TestIdentityHierarchyAndTransform();
		TestDetachedComponentsAndInvalidation();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
