#pragma once
#include <core/Containers.hpp>
#include <rhi/RHI.hpp>
#include <application/FrameScheduler.hpp>
#include <scene/Scene.hpp>
#include <renderer3d/Renderer3D.hpp>

namespace Ursa
{
class Application
{
public:
	virtual ~Application() = default;

	Result<void> Run(uint32_t width = 1280, uint32_t height = 720, const String& title = "UrsaEngine",
	                 bool visible = true, uint32_t frameLimit = 0);

	Result<void> ConfigureFixedStep(float seconds, uint32_t maxSteps = 5);

protected:
	virtual Result<void> OnStart() = 0;

	virtual Result<void> OnFrame(float deltaSeconds)
	{
		(void)deltaSeconds;
		return {};
	}

	virtual void OnStop()
	{
	}

	std::shared_ptr<Window> window;
	Rhi::DevicePtr device;
	Rhi::SwapchainPtr swapchain;
	std::shared_ptr<Scene> scene;

private:
	FrameScheduler frameScheduler;
	std::unique_ptr<Renderer3D> sceneRenderer;
};
} // namespace Ursa
