#pragma once

#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <optional>

namespace Ursa::Editor
{
struct ViewportPoint
{
	Float32 x = 0;
	Float32 y = 0;
};

struct ViewportSize
{
	UInt32 width = 0;
	UInt32 height = 0;
};

struct ViewportRect
{
	Float32 x = 0;
	Float32 y = 0;
	Float32 width = 0;
	Float32 height = 0;
};

enum class GameViewSizeMode
{
	FreeAspect,
	AspectRatio,
	FixedResolution
};

struct ViewportLayout
{
	ViewportSize targetPixels;
	ViewportRect displayRect;
	ViewportRect clipRect;
	Float32 scale = 1;
	Float32 dpiScale = 1;
	bool visible = false;
};

Result<ViewportSize> ResolveViewportSize(ViewportSize availablePixels, GameViewSizeMode mode, UInt32 width,
                                         UInt32 height, UInt32 maxDimension, UInt64 maxPixelCount);

ViewportLayout ComputeViewportLayout(ViewportRect available, Float32 dpiScale, ViewportSize targetPixels, Float32 scale,
                                     ViewportPoint pan, bool fit);

ViewportPoint ClampViewportPan(ViewportPoint pan, ViewportSize displaySize, ViewportSize availableSize);

std::optional<ViewportPoint> ScreenToViewportPixel(ViewportPoint screenPosition, const ViewportLayout& layout);

Float32 SnapViewportScale(Float32 scale);
} // namespace Ursa::Editor
