#pragma once
#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>
#include <Application/SceneFrameRunner.hpp>
#include <Scene/Scene.hpp>
#include <Rendering/Pipeline/NativeForwardPipeline.hpp>

namespace Ursa
{
class Application
{
public:
	virtual ~Application() = default;

	Result<void> Run(Ursa::UInt32 width = 1280, Ursa::UInt32 height = 720, const String& title = "UrsaEngine",
	                 bool visible = true, Ursa::UInt32 frameLimit = 0);

	Result<void> ConfigureFixedStep(Ursa::Float32 seconds, Ursa::UInt32 maxSteps = 5);

	Result<void> SetFrameCapture(std::filesystem::path output, UInt32 frame = 1)
	{
		return frameCapture.Configure(std::move(output), frame);
	}

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

	virtual Result<void> OnRender(Rendering::FrameCapture* capture);
	virtual Result<void> InitializeRenderPipeline();

	std::shared_ptr<Window> window;
	Rhi::DevicePtr device;
	Rhi::SwapchainPtr swapchain;
	std::shared_ptr<Scene> scene;

private:
	Rendering::FrameCapture frameCapture;
	SceneFrameRunner sceneFrameRunner;
	std::unique_ptr<Rendering::NativeForwardPipeline> sceneRenderer;
};
} // namespace Ursa
