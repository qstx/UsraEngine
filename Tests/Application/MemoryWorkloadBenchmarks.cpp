#include <Core/Memory/MemoryFactories.hpp>
#include <Application/SceneFrameRunner.hpp>
#include <Material/Material.hpp>
#include <Scene/Scene.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Memory;
volatile UInt64 checksum = 0;

double MaterialWorkload(int policy)
{
	const auto started = std::chrono::steady_clock::now();
	for (int frame = 0; frame < 2000; ++frame)
	{
		TemporaryScope scope;
		std::array<std::shared_ptr<MaterialInstance>, 32> materials;
		for (auto& material : materials)
		{
			if (policy == 0)
			{
				material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
			}
			else if (policy == 1)
			{
				material = MakeShared<MaterialInstance>(MemoryTag::Renderer, MaterialType::Unlit);
			}
			else
			{
				material = MakeShared<MaterialInstance>(scope.GetAllocator(), MemoryTag::Renderer, MaterialType::Unlit);
			}
			checksum += static_cast<UInt64>(material->Type());
		}
	}
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count() / 64000;
}

class Probe final : public Component
{
public:
	Result<void> Update(const FrameContext&) override
	{
		ThreadTemporaryAllocator().Allocate(64, 16, MemoryTag::Temporary);
		return {};
	}
};
} // namespace

int main()
{
	MaterialWorkload(1);
	MaterialWorkload(2);
	const auto before = DefaultAllocator().Statistics();
	const double baseline = MaterialWorkload(0);
	const double persistent = MaterialWorkload(1);
	const double temporary = MaterialWorkload(2);
	const auto after = DefaultAllocator().Statistics();
	std::cout << "Renderer.FallbackMaterial: std=" << baseline << " ns/object, persistent=" << persistent
	          << " ns/object, temporary=" << temporary
	          << " ns/object, Ursa-warm-upstream=" << after.upstreamAllocations - before.upstreamAllocations << '\n';
	if (after.upstreamAllocations != before.upstreamAllocations ||
	    Snapshot().categories[static_cast<Size>(MemoryTag::Renderer)].liveAllocations != 0)
	{
		return 1;
	}
	Scene scene;
	SceneFrameRunner runner;
	for (int index = 0; index < 128; ++index)
	{
		auto object = scene.CreateGameObject();
		if (!object || !scene.AddComponent<Probe>(object.value))
		{
			return 2;
		}
	}
	if (!runner.Tick(scene, 0.016f, nullptr, {}))
	{
		return 3;
	}
	const auto frameBefore = DefaultAllocator().Statistics();
	const auto started = std::chrono::steady_clock::now();
	for (int frame = 0; frame < 1000; ++frame)
	{
		if (!runner.Tick(scene, 0.016f, nullptr, {}))
		{
			return 4;
		}
	}
	const double elapsed =
	    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count() / 1000;
	const auto frameAfter = DefaultAllocator().Statistics();
	std::cout << "Scene.Tick.128Components: " << elapsed
	          << " us/frame, warm-upstream=" << frameAfter.upstreamAllocations - frameBefore.upstreamAllocations
	          << '\n';
	return frameAfter.upstreamAllocations == frameBefore.upstreamAllocations && ThreadTemporaryAllocator().Used() == 0
	           ? 0
	           : 5;
}
