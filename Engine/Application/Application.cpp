#include <Core/Types.hpp>
#include <Application/Application.hpp>
#include <chrono>

namespace Ursa
{
Result<void> Application::ConfigureFixedStep(Ursa::Float32 seconds, Ursa::UInt32 maxSteps)
{
	return sceneFrameRunner.ConfigureFixedStep(seconds, maxSteps);
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
	if (!started)
	{
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
		auto rendererResult = Renderer3D::Create(device);
		if (!rendererResult)
		{
			scene->Clear();
			OnStop();
			swapchain.reset();
			device.reset();
			window.reset();
			return Result<void>::Failure("Cannot initialize scene renderer: " + rendererResult.error);
		}
		sceneRenderer = std::move(rendererResult.value);
	}
	auto previous = std::chrono::steady_clock::now();
	Result<void> result;
	Ursa::UInt32 frames = 0;
	while (!window->ShouldClose())
	{
		window->PollEvents();
		if (window->FramebufferWidth() == 0 || window->FramebufferHeight() == 0)
		{
			window->WaitEvents();
			previous = std::chrono::steady_clock::now();
			sceneFrameRunner.ResetAccumulator();
			continue;
		}
		const auto now = std::chrono::steady_clock::now();
		const Ursa::Float32 dt = std::chrono::duration<Ursa::Float32>(now - previous).count();
		previous = now;
		if (scene)
		{
			result = sceneFrameRunner.Tick(*scene, dt, window.get(), FrameInput::Capture(*window));
			if (result)
			{
				const Ursa::Float32 aspect = static_cast<Ursa::Float32>(window->FramebufferWidth()) /
				                             static_cast<Ursa::Float32>(window->FramebufferHeight());
				auto data = scene->CollectRenderData(aspect);
				if (!data)
				{
					result = Result<void>::Failure(data.error);
				}
				else
				{
					result = sceneRenderer->RenderModels(swapchain, data.value.viewProjection, data.value.boxes,
					                                     data.value.lighting, data.value.cameraPosition);
				}
			}
		}
		else
		{
			result = OnFrame(dt);
		}
		if (!result)
		{
			break;
		}
		if (frameLimit && ++frames >= frameLimit)
		{
			break;
		}
	}
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
	return idle;
}
} // namespace Ursa
