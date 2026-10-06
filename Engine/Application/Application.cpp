#include <Core/Types.hpp>
#include <Application/Application.hpp>
#include <chrono>

namespace Ursa
{
Result<void> Application::ConfigureFixedStep(Ursa::Float32 seconds, Ursa::UInt32 maxSteps)
{
	return frameScheduler.Configure(seconds, maxSteps);
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
	Ursa::UInt64 frameIndex = 0;
	Ursa::Float64 elapsedSeconds = 0.0;
	while (!window->ShouldClose())
	{
		window->PollEvents();
		if (window->FramebufferWidth() == 0 || window->FramebufferHeight() == 0)
		{
			window->WaitEvents();
			previous = std::chrono::steady_clock::now();
			frameScheduler.Reset();
			continue;
		}
		const auto now = std::chrono::steady_clock::now();
		const Ursa::Float32 dt = std::chrono::duration<Ursa::Float32>(now - previous).count();
		previous = now;
		if (scene)
		{
			const auto schedule = frameScheduler.Advance(dt);
			elapsedSeconds += schedule.deltaSeconds;
			FrameContext context;
			context.deltaSeconds = schedule.deltaSeconds;
			context.fixedDeltaSeconds = frameScheduler.FixedSeconds();
			context.elapsedSeconds = elapsedSeconds;
			context.frameIndex = ++frameIndex;
			context.window = window.get();
			context.input = FrameInput::Capture(*window);
			scene->BeginFrame();
			result = scene->Update(context);
			for (Ursa::UInt32 step = 0; result && step < schedule.fixedSteps; ++step)
			{
				result = scene->FixedUpdate(context);
			}
			if (result)
			{
				result = scene->LateUpdate(context);
			}
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
