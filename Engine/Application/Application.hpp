#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>
#include <Application/FrameScheduler.hpp>
#include <Scene/Scene.hpp>
#include <Renderer3D/Renderer3D.hpp>

namespace Ursa
{
class Application
{
public:
	virtual ~Application() = default;

	Result<void> Run(Ursa::UInt32 width = 1280, Ursa::UInt32 height = 720, const String& title = "UrsaEngine",
	                 bool visible = true, Ursa::UInt32 frameLimit = 0);

	Result<void> ConfigureFixedStep(Ursa::Float32 seconds, Ursa::UInt32 maxSteps = 5);

protected:
	virtual Result<void> OnStart() = 0;

	virtual Result<void> OnFrame(Ursa::Float32 deltaSeconds)
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
