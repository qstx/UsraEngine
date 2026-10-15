#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <RenderingFixtures.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}
} // namespace

int main()
{
	try
	{
		auto window = Take(Ursa::Window::Create(640, 480, "Ursa 3D renderer test", false));
		Ursa::Rhi::DeviceDesc desc;
		desc.window = window;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(Ursa::Rhi::CreateDevice(desc));
		auto swapchain = Take(device->CreateSwapchain({}));
		auto renderer = Take(Ursa::Test::SceneRenderer::Create(device));
		Ursa::Camera camera;
		const Ursa::Vector<Ursa::Test::CubeDraw> boxes{{{{0, 0, 0}, {1, 1, 1}}, {0.8f, 0.2f, 0.1f, 1}}};

		done(renderer->Render(swapchain, camera, boxes));
		window->Resize(800, 600);
		window->PollEvents();
		done(renderer->Render(swapchain, camera, boxes));
		window->Minimize();
		window->PollEvents();
		const auto suspended = renderer->Render(swapchain, camera, boxes);
		if (suspended || suspended.error.find("suspended") == Ursa::String::npos)
		{
			throw std::runtime_error("Minimized SRP window did not report a suspended target");
		}
		window->Restore();
		window->PollEvents();
		done(renderer->Render(swapchain, camera, boxes));
		done(device->WaitIdle());
		renderer.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		std::cout << "3D renderer window test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "3D renderer window test failed: " << error.what() << '\n';
		return 1;
	}
}
