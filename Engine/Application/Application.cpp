#include <Core/Types.hpp>
#include <Core/Profiler.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Application/Application.hpp>
#include <chrono>

namespace Ursa
{
Result<void> Application::ConfigureFixedStep(Ursa::Float32 seconds, Ursa::UInt32 maxSteps)
{
	return sceneFrameRunner.ConfigureFixedStep(seconds, maxSteps);
}

Result<void> Application::InitializeRenderPipeline()
{
	auto created = Rendering::NativeForwardPipeline::Create(device);
	if (!created)
	{
		return Result<void>::Failure(created.error);
	}
	sceneRenderer = std::move(created.value);
	return {};
}

Result<void> Application::OnRender(Rendering::FrameCapture* capture)
{
	return sceneRenderer->Render(*scene, swapchain, capture);
}

Result<void> Application::Run(Ursa::UInt32 width, Ursa::UInt32 height, const String& title, bool visible,
                              Ursa::UInt32 frameLimit)
{
	auto windowResult = Window::Create(width, height, title, visible);
	if (!windowResult)
	{
		return Result<void>::Failure(windowResult.error);
	}
	window = std::move(windowResult.value);
	Rendering::WindowCaptureScope captureScope(frameCapture, "Player");
	sceneFrameRunner.Reset();
	Rhi::DeviceDesc deviceDesc;
	deviceDesc.window = window;
#ifndef NDEBUG
	deviceDesc.validation = true;
#endif
	auto deviceResult = Rhi::CreateDevice(deviceDesc);
	if (!deviceResult)
	{
		return Result<void>::Failure(deviceResult.error);
	}
	device = std::move(deviceResult.value);
	auto swapchainResult = device->CreateSwapchain({});
	if (!swapchainResult)
	{
		return Result<void>::Failure(swapchainResult.error);
	}
	swapchain = std::move(swapchainResult.value);
	auto started = OnStart();
	if (started && scene)
	{
		started = InitializeRenderPipeline();
	}
	if (!started)
	{
		frameCapture.StopWindowCapture();
		if (scene)
		{
			scene->Clear();
		}
		OnStop();
		swapchain.reset();
		device.reset();
		window.reset();
		return started;
	}
	if (scene)
	{
		frameCapture.ReadyWindowCapture();
	}
	auto previous = std::chrono::steady_clock::now();
	Result<void> result;
	Ursa::UInt32 frames = 0;
	while (!window->ShouldClose())
	{
		window->PollEvents();
		frameCapture.PollWindowCapture();
		if (window->FramebufferWidth() == 0 || window->FramebufferHeight() == 0)
		{
			window->WaitEvents();
			previous = std::chrono::steady_clock::now();
			sceneFrameRunner.ResetAccumulator();
			continue;
		}
		Memory::TemporaryScope frameMemory;
		URSA_PROFILE_SCOPE("Engine.Frame");
		const auto now = std::chrono::steady_clock::now();
		const Ursa::Float32 dt = std::chrono::duration<Ursa::Float32>(now - previous).count();
		previous = now;
		if (!scene)
		{
			URSA_PROFILE_SCOPE("Engine.GameFrame");
			result = OnFrame(dt);
		}
		else
		{
			result = OnFrame(dt);
			if (result)
			{
				result = sceneFrameRunner.Tick(*scene, dt, window.get(), FrameInput::Capture(*window));
			}
			if (result)
			{
				URSA_PROFILE_SCOPE("Engine.RenderPreparation");
				result = OnRender(&frameCapture);
			}
		}
		if (!result)
		{
			break;
		}
		URSA_PROFILE_FRAME("Engine");
		if (frameLimit && ++frames >= frameLimit)
		{
			break;
		}
	}
	frameCapture.StopWindowCapture();
	auto idle = device->WaitIdle();
	if (scene)
	{
		scene->Clear();
	}
	OnStop();
	sceneRenderer.reset();
	swapchain.reset();
	device.reset();
	window.reset();
	if (!result)
	{
		return result;
	}
	return idle ? frameCapture.Finish() : idle;
}
} // namespace Ursa
