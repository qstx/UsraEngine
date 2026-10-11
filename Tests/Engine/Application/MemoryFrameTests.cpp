#include <Application/SceneFrameRunner.hpp>
#include <Core/Memory/Memory.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Scene/Scene.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Memory;

class ScratchProbe final : public Component
{
public:
	explicit ScratchProbe(MemoryTag tag) : tag(tag)
	{
	}

	Result<void> Update(const FrameContext&) override
	{
		ThreadTemporaryAllocator().Allocate(64, 16, tag);
		if (overflow)
		{
			ThreadTemporaryAllocator().Allocate(2 * 1024 * 1024, 64, tag);
		}
		return fail ? Result<void>::Failure("Intentional memory frame test failure") : Result<void>{};
	}

	bool fail = false;
	bool overflow = false;

private:
	MemoryTag tag;
};

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		Scene scene;
		SceneFrameRunner runner;
		const auto tag = RegisterTag("MemoryFrameTests");
		auto object = scene.CreateGameObject();
		Expect(static_cast<bool>(object), "Cannot create memory test object");
		auto added = scene.AddComponent<ScratchProbe>(object.value, tag);
		Expect(static_cast<bool>(added), "Cannot attach memory test probe");
		auto* probe = static_cast<ScratchProbe*>(scene.GetComponent(added.value));
		Expect(static_cast<bool>(runner.Tick(scene, 0.016f, nullptr, {})), "Warm-up frame failed");
		const auto heapBaseline = DefaultAllocator().Statistics();
		const auto baseline = Snapshot().categories[static_cast<Size>(tag)];
		for (int frame = 0; frame < 1000; ++frame)
		{
			Expect(static_cast<bool>(runner.Tick(scene, 0.016f, nullptr, {})), "Memory test frame failed");
			const auto state = Snapshot().categories[static_cast<Size>(tag)];
			Expect(ThreadTemporaryAllocator().Used() == 0 && state.currentBytes == baseline.currentBytes &&
			           state.liveAllocations == baseline.liveAllocations,
			       "Frame scratch outlived its frame");
		}
		Expect(DefaultAllocator().Statistics().upstreamAllocations == heapBaseline.upstreamAllocations,
		       "Warmed scene frames kept allocating upstream storage");
		probe->fail = true;
		probe->overflow = true;
		Expect(!runner.Tick(scene, 0.016f, nullptr, {}), "Intentional failure was not propagated");
		const auto finalState = Snapshot().categories[static_cast<Size>(tag)];
		Expect(finalState.currentBytes == baseline.currentBytes &&
		           finalState.liveAllocations == baseline.liveAllocations &&
		           finalState.overflowCount > baseline.overflowCount && ThreadTemporaryAllocator().Used() == 0,
		       "Failure/overflow frame did not clean up");
		std::cout << "Repeated frame allocation tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
