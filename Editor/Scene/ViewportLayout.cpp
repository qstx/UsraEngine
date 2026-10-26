#include <Scene/ViewportLayout.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ursa::Editor
{
namespace
{
Float32 Clamp(Float32 value, Float32 minimum, Float32 maximum)
{
	return std::max(minimum, std::min(value, maximum));
}

ViewportRect Intersect(const ViewportRect& left, const ViewportRect& right)
{
	const Float32 x = std::max(left.x, right.x);
	const Float32 y = std::max(left.y, right.y);
	const Float32 maxX = std::min(left.x + left.width, right.x + right.width);
	const Float32 maxY = std::min(left.y + left.height, right.y + right.height);
	return {x, y, std::max(0.f, maxX - x), std::max(0.f, maxY - y)};
}
} // namespace

Result<ViewportSize> ResolveViewportSize(ViewportSize availablePixels, GameViewSizeMode mode, UInt32 width,
                                         UInt32 height, UInt32 maxDimension, UInt64 maxPixelCount)
{
	if (!availablePixels.width || !availablePixels.height)
	{
		return Result<ViewportSize>::Failure("Viewport available size must be positive");
	}
	ViewportSize result = availablePixels;
	if (mode == GameViewSizeMode::FixedResolution)
	{
		result = {width, height};
	}
	else if (mode == GameViewSizeMode::AspectRatio)
	{
		if (!width || !height)
		{
			return Result<ViewportSize>::Failure("Viewport aspect ratio must be positive");
		}
		const Float32 aspect = static_cast<Float32>(width) / static_cast<Float32>(height);
		if (static_cast<Float32>(availablePixels.width) / availablePixels.height > aspect)
		{
			result.width = static_cast<UInt32>(std::floor(availablePixels.height * aspect));
		}
		else
		{
			result.height = static_cast<UInt32>(std::floor(availablePixels.width / aspect));
		}
	}
	if (!result.width || !result.height)
	{
		return Result<ViewportSize>::Failure("Viewport target size must be positive");
	}
	if (result.width > maxDimension || result.height > maxDimension)
	{
		return Result<ViewportSize>::Failure("Viewport target exceeds the device maximum texture dimension");
	}
	const UInt64 pixelCount = static_cast<UInt64>(result.width) * result.height;
	if (pixelCount > maxPixelCount)
	{
		return Result<ViewportSize>::Failure("Viewport target exceeds the editor pixel budget");
	}
	return Result<ViewportSize>::Success(result);
}

ViewportLayout ComputeViewportLayout(ViewportRect available, Float32 dpiScale, ViewportSize targetPixels, Float32 scale,
                                     ViewportPoint pan, bool fit)
{
	ViewportLayout result;
	result.targetPixels = targetPixels;
	result.dpiScale = std::isfinite(dpiScale) && dpiScale > 0 ? dpiScale : 1.f;
	if (!targetPixels.width || !targetPixels.height || available.width <= 0 || available.height <= 0)
	{
		return result;
	}
	const Float32 logicalWidth = static_cast<Float32>(targetPixels.width) / result.dpiScale;
	const Float32 logicalHeight = static_cast<Float32>(targetPixels.height) / result.dpiScale;
	const Float32 fitScale = std::min(available.width / logicalWidth, available.height / logicalHeight);
	result.scale = fit ? fitScale
	                   : Clamp(std::isfinite(scale) && scale > 0 ? scale : fitScale, std::min(1.f, fitScale),
	                           std::max(5.f * result.dpiScale, fitScale));
	const Float32 displayWidth = logicalWidth * result.scale;
	const Float32 displayHeight = logicalHeight * result.scale;
	const ViewportSize displaySize{static_cast<UInt32>(std::ceil(displayWidth)),
	                               static_cast<UInt32>(std::ceil(displayHeight))};
	const ViewportSize availableSize{static_cast<UInt32>(std::floor(available.width)),
	                                 static_cast<UInt32>(std::floor(available.height))};
	pan = ClampViewportPan(pan, displaySize, availableSize);
	const Float32 x = available.x + (available.width - displayWidth) * 0.5f + pan.x;
	const Float32 y = available.y + (available.height - displayHeight) * 0.5f + pan.y;
	result.displayRect = {x, y, displayWidth, displayHeight};
	result.clipRect = Intersect(result.displayRect, available);
	result.visible = result.clipRect.width > 0 && result.clipRect.height > 0;
	return result;
}

ViewportPoint ClampViewportPan(ViewportPoint pan, ViewportSize displaySize, ViewportSize availableSize)
{
	const Float32 maxX = displaySize.width > availableSize.width
	                         ? static_cast<Float32>(displaySize.width - availableSize.width) * 0.5f
	                         : 0.f;
	const Float32 maxY = displaySize.height > availableSize.height
	                         ? static_cast<Float32>(displaySize.height - availableSize.height) * 0.5f
	                         : 0.f;
	return {Clamp(pan.x, -maxX, maxX), Clamp(pan.y, -maxY, maxY)};
}

std::optional<ViewportPoint> ScreenToViewportPixel(ViewportPoint screenPosition, const ViewportLayout& layout)
{
	const auto& rect = layout.clipRect;
	if (!layout.visible || screenPosition.x < rect.x || screenPosition.y < rect.y ||
	    screenPosition.x >= rect.x + rect.width || screenPosition.y >= rect.y + rect.height)
	{
		return std::nullopt;
	}
	const Float32 u = (screenPosition.x - layout.displayRect.x) / layout.displayRect.width;
	const Float32 v = (screenPosition.y - layout.displayRect.y) / layout.displayRect.height;
	return ViewportPoint{u * layout.targetPixels.width, v * layout.targetPixels.height};
}

Float32 SnapViewportScale(Float32 scale)
{
	if (!std::isfinite(scale) || scale <= 0)
	{
		return 1.f;
	}
	const Float32 integer = std::round(scale);
	return std::abs(scale - integer) <= 0.025f * std::max(1.f, scale) ? integer : scale;
}
} // namespace Ursa::Editor
