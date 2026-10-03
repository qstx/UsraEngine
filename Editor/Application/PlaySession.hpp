#pragma once

#include <Application/FrameScheduler.hpp>
#include <Assets/SceneDraft.hpp>
#include <memory>

namespace Ursa::Editor
{
using PlayContextSetup = std::function<Result<std::shared_ptr<void>>(ComponentLoadContext&)>;

FrameInput SelectPlayInput(FrameInput polled, bool viewportFocused, bool editorTextActive);

class PlaySession final
{
public:
	Result<void> Start(const SceneDraft& draft, ResourceId documentId, const ComponentRegistry& registry,
	                   ResourceSession& resources, const PlayContextSetup& setup = {});

	Result<void> Tick(float deltaSeconds, Window* window, FrameInput input);

	void Stop();

	bool Active() const
	{
		return static_cast<bool>(scene);
	}

	Scene* GetScene()
	{
		return scene.get();
	}

	const Scene* GetScene() const
	{
		return scene.get();
	}

	~PlaySession()
	{
		Stop();
	}

private:
	std::shared_ptr<void> services;
	std::unique_ptr<Scene> scene;
	FrameScheduler scheduler;
	uint64_t frameIndex = 0;
	double elapsedSeconds = 0.0;
};
} // namespace Ursa::Editor
