#pragma once

#include <Core/Types.hpp>
#include <Application/SceneFrameRunner.hpp>
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

	Result<void> Tick(Ursa::Float32 deltaSeconds, Window* window, FrameInput input);

	void Pause()
	{
		paused = true;
	}

	void Resume()
	{
		paused = false;
	}

	void Step()
	{
		if (paused)
		{
			stepRequested = true;
		}
	}

	void Stop();

	bool Active() const
	{
		return static_cast<bool>(scene);
	}

	bool Paused() const
	{
		return paused;
	}

	Scene* GetScene()
	{
		return scene.get();
	}

	const Scene* GetScene() const
	{
		return scene.get();
	}

	ResourceSession* Resources()
	{
		return resourceSession.get();
	}

	std::shared_ptr<void> ServiceOwner() const
	{
		return services;
	}

	~PlaySession()
	{
		Stop();
	}

private:
	std::unique_ptr<ResourceSession> resourceSession;
	std::shared_ptr<void> services;
	std::unique_ptr<Scene> scene;
	SceneFrameRunner frameRunner;
	bool paused = false;
	bool stepRequested = false;
};
} // namespace Ursa::Editor
