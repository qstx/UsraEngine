#include <Core/StringInterop.hpp>
#include <Scene/ViewportLayout.hpp>
#include <Scene/SceneViewPanel.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

void ExpectNear(Float32 actual, Float32 expected, const char* message)
{
	if (std::abs(actual - expected) > 0.01f)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		const auto freeAspect = ResolveViewportSize({1280, 800}, GameViewSizeMode::FreeAspect, 0, 0, 8192, 16'000'000);
		Expect(freeAspect && freeAspect.value.width == 1280 && freeAspect.value.height == 800,
		       "Free Aspect did not follow the available area");
		const auto ratio = ResolveViewportSize({1280, 800}, GameViewSizeMode::AspectRatio, 16, 9, 8192, 16'000'000);
		Expect(ratio && ratio.value.width == 1280 && ratio.value.height == 720,
		       "Aspect ratio did not fit the available area");
		const auto fixed =
		    ResolveViewportSize({640, 480}, GameViewSizeMode::FixedResolution, 1920, 1080, 8192, 16'000'000);
		Expect(fixed && fixed.value.width == 1920 && fixed.value.height == 1080,
		       "Fixed resolution changed with the panel size");
		Expect(!ResolveViewportSize({640, 480}, GameViewSizeMode::FixedResolution, 1920, 1080, 8192, 1'000'000),
		       "Pixel budget overflow was accepted");
		Expect(!ResolveViewportSize({640, 480}, GameViewSizeMode::FixedResolution, 1920, 1080, 1024, 16'000'000),
		       "Device texture dimension overflow was accepted");

		const auto half = ComputeViewportLayout({0, 0, 800, 800}, 1, {1920, 1080}, 0.5f, {}, false);
		ExpectNear(half.displayRect.width, 960, "0.5x display width is wrong");
		ExpectNear(half.displayRect.height, 540, "0.5x display height is wrong");
		const auto pixel = ScreenToViewportPixel({400, 400}, half);
		Expect(pixel.has_value(), "Visible center did not map to target pixels");
		ExpectNear(pixel->x, 960, "Horizontal pointer mapping is wrong");
		ExpectNear(pixel->y, 540, "Vertical pointer mapping is wrong");
		Expect(!ScreenToViewportPixel({10, 10}, half), "Letterbox area was treated as image content");

		const auto highDpi = ComputeViewportLayout({0, 0, 960, 540}, 2, {1920, 1080}, 1, {}, true);
		ExpectNear(highDpi.displayRect.width, 960, "DPI-aware source pixel size is wrong");
		ExpectNear(highDpi.scale, 1, "Fit should use the full available logical area");
		const auto secondaryHighDpi = ComputeViewportLayout({-1440, 120, 960, 500}, 1.5f, {1440, 810}, 1, {}, true);
		const auto secondaryCenter = ScreenToViewportPixel({-960, 370}, secondaryHighDpi);
		Expect(secondaryCenter.has_value(), "A viewport on a negative desktop origin was not hit-testable");
		ExpectNear(secondaryCenter->x, 720, "Negative-origin DPI mapping produced the wrong horizontal pixel");
		ExpectNear(secondaryCenter->y, 405, "Negative-origin DPI mapping produced the wrong vertical pixel");
		Expect(!ScreenToViewportPixel({-1439, 121}, secondaryHighDpi),
		       "A high-DPI letterbox gutter on a secondary display mapped into the render target");
		ExpectNear(SnapViewportScale(0.996f, 0.1f, 5.f), 1, "Scale was not snapped to an integer");
		const auto panned = ComputeViewportLayout({0, 0, 640, 360}, 1, {1920, 1080}, 1, {10000, -10000}, false);
		ExpectNear(panned.displayRect.x, 0, "Horizontal pan exceeded its boundary");
		ExpectNear(panned.displayRect.y, -720, "Vertical pan exceeded its boundary");
		const auto zoomedOutPan = ComputeViewportLayout({0, 0, 640, 360}, 1, {320, 180}, 1.f, {100, -80}, false);
		ExpectNear(zoomedOutPan.displayRect.x, 160,
		           "Game view panning while zoomed out did not keep the render target centered");
		ExpectNear(zoomedOutPan.displayRect.y, 90,
		           "Game view vertical panning while zoomed out did not keep the render target centered");
		const auto zoomedInPan = ComputeViewportLayout({0, 0, 640, 360}, 1, {320, 180}, 3, {100, -40}, false);
		ExpectNear(zoomedInPan.displayRect.x, -60,
		           "Game view panning at 3x did not retain an in-bounds horizontal offset");
		ExpectNear(zoomedInPan.displayRect.y, -130,
		           "Game view panning at 3x did not retain an in-bounds vertical offset");
		ExpectNear(ComputeViewportLayout({0, 0, 640, 360}, 1, {320, 180}, 2, {100, -40}, false).displayRect.x, 0,
		           "Game view panning at an exact-fit scale exposed blank content instead of recentering");

		Camera camera;
		camera.position = {0.f, 0.f, 5.f};
		camera.target = {0.f, 0.f, 0.f};
		camera.up = {0.f, 1.f, 0.f};
		const auto squareViewport = ComputeViewportLayout({0, 0, 200, 100}, 1, {100, 100}, 1, {}, true);
		ExpectNear(squareViewport.displayRect.x, 50.f, "Scene image did not preserve its letterboxed bounds");
		Rendering::RenderObject nearBox;
		nearBox.worldBounds = {{-1.f, -1.f, 0.f}, {1.f, 1.f, 2.f}, false};
		Rendering::RenderObject farBox;
		farBox.worldBounds = {{-1.f, -1.f, -3.f}, {1.f, 1.f, -1.f}, false};
		auto geometryHit = ResolveSceneDropPoint(camera, 1.f, squareViewport, {100.f, 50.f}, {farBox, nearBox});
		Expect(geometryHit && geometryHit.value.hitGeometry,
		       "Scene placement did not prefer a visible geometry hit over the target plane");
		ExpectNear(geometryHit.value.world.z, 2.f, "Scene placement did not choose the nearest geometry bounds");
		auto targetPlaneHit = ResolveSceneDropPoint(camera, 1.f, squareViewport, {75.f, 50.f}, {farBox});
		Expect(targetPlaneHit && !targetPlaneHit.value.hitGeometry && targetPlaneHit.value.world.x < 0.f,
		       "Scene placement did not map the pointer ray onto the camera target plane");
		Expect(!ResolveSceneDropPoint(camera, 1.f, squareViewport, {20.f, 50.f}, {}),
		       "A letterbox area outside the displayed Scene image accepted a placement point");
		std::cout << "Viewport layout tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
