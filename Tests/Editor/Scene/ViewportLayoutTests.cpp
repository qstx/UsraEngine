#include <Core/StringInterop.hpp>
#include <Scene/ViewportLayout.hpp>
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

		const auto half = ComputeViewportLayout({0, 0, 2000, 1200}, 1, {1920, 1080}, 0.5f, {}, false);
		ExpectNear(half.displayRect.width, 960, "0.5x display width is wrong");
		ExpectNear(half.displayRect.height, 540, "0.5x display height is wrong");
		const auto pixel = ScreenToViewportPixel({1000, 600}, half);
		Expect(pixel.has_value(), "Visible center did not map to target pixels");
		ExpectNear(pixel->x, 960, "Horizontal pointer mapping is wrong");
		ExpectNear(pixel->y, 540, "Vertical pointer mapping is wrong");
		Expect(!ScreenToViewportPixel({10, 10}, half), "Letterbox area was treated as image content");

		const auto highDpi = ComputeViewportLayout({0, 0, 960, 540}, 2, {1920, 1080}, 1, {}, true);
		ExpectNear(highDpi.displayRect.width, 960, "DPI-aware source pixel size is wrong");
		ExpectNear(highDpi.scale, 1, "Fit should use the full available logical area");
		ExpectNear(SnapViewportScale(0.996f), 1, "Scale was not snapped to an integer");
		const auto panned = ComputeViewportLayout({0, 0, 640, 360}, 1, {1920, 1080}, 1, {10000, -10000}, false);
		ExpectNear(panned.displayRect.x, 0, "Horizontal pan exceeded its boundary");
		ExpectNear(panned.displayRect.y, -720, "Vertical pan exceeded its boundary");
		std::cout << "Viewport layout tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
