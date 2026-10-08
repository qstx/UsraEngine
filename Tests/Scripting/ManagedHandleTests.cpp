#include <Core/Types.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void TestHandleLifetime()
{
	int firstObject = 1;
	int secondObject = 2;
	auto registry = Ursa::Scripting::ManagedHandleRegistry::Create();
	Expect(static_cast<bool>(registry), "Cannot create a managed handle session");
	auto first = registry.value->Register(&firstObject, Ursa::Scripting::ManagedHandleKind::GameObject);
	Expect(static_cast<bool>(first), "Cannot register a GameObject handle");
	auto duplicate = registry.value->Register(&firstObject, Ursa::Scripting::ManagedHandleKind::GameObject);
	Expect(duplicate && duplicate.value.index == first.value.index &&
	           duplicate.value.generation == first.value.generation,
	       "Registering the same object did not reuse its session handle");
	Expect(!registry.value->Register(&firstObject, Ursa::Scripting::ManagedHandleKind::Component),
	       "One native object was registered with incompatible kinds");
	auto resolved = registry.value->ResolveAs<int>(first.value, Ursa::Scripting::ManagedHandleKind::GameObject);
	Expect(resolved && resolved.value == &firstObject, "Valid handle did not resolve to its native object");
	Expect(!registry.value->Resolve(first.value, Ursa::Scripting::ManagedHandleKind::Component),
	       "Handle resolved as the wrong object kind");

	Expect(static_cast<bool>(registry.value->Invalidate(first.value)), "Could not invalidate the first handle");
	Expect(!registry.value->Resolve(first.value, Ursa::Scripting::ManagedHandleKind::GameObject),
	       "Invalidated handle remained usable");
	auto second = registry.value->Register(&secondObject, Ursa::Scripting::ManagedHandleKind::Component);
	Expect(second && second.value.index == first.value.index && second.value.generation != first.value.generation,
	       "Reused handle index did not advance its generation");
	Expect(!registry.value->Resolve(first.value, Ursa::Scripting::ManagedHandleKind::GameObject),
	       "A stale handle resolved after index reuse");

	auto otherSession = Ursa::Scripting::ManagedHandleRegistry::Create();
	Expect(otherSession && otherSession.value->SessionId() != registry.value->SessionId(),
	       "Separate script sessions reused the same session ID");
	Expect(!otherSession.value->Resolve(second.value, Ursa::Scripting::ManagedHandleKind::Component),
	       "A handle resolved in another script session");
	Expect(static_cast<bool>(registry.value->InvalidateAll()), "Could not invalidate the session handles");
	Expect(!registry.value->Resolve(second.value, Ursa::Scripting::ManagedHandleKind::Component),
	       "A handle remained valid after session invalidation");

	std::atomic_bool workerRejected{false};
	std::thread worker(
	    [&registry, &second, &workerRejected]
	    {
		    workerRejected.store(!registry.value->Resolve(second.value, Ursa::Scripting::ManagedHandleKind::Component),
		                         std::memory_order_relaxed);
	    });
	worker.join();
	Expect(workerRejected.load(std::memory_order_relaxed), "A worker thread accessed the main-thread handle table");
}
} // namespace

int main()
{
	try
	{
		TestHandleLifetime();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
