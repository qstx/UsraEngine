#include <Rendering/Scene/CameraData.hpp>
#include <cmath>

namespace Ursa::Rendering
{
Result<void> CameraSettings::Validate() const
{
	if (static_cast<UInt32>(type) > static_cast<UInt32>(CameraType::Preview) ||
	    static_cast<UInt32>(clearFlags) > static_cast<UInt32>(CameraClearFlags::Nothing) || !std::isfinite(depth) ||
	    !std::isfinite(background.red) || !std::isfinite(background.green) || !std::isfinite(background.blue) ||
	    !std::isfinite(background.alpha) || !std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
	    !std::isfinite(viewport.width) || !std::isfinite(viewport.height) || viewport.x < 0 || viewport.y < 0 ||
	    viewport.width <= 0 || viewport.height <= 0 || viewport.x + viewport.width > 1 ||
	    viewport.y + viewport.height > 1 || ((targetWidth == 0) != (targetHeight == 0)) || targetWidth > 16384 ||
	    targetHeight > 16384)
	{
		return Result<void>::Failure("Camera rendering settings contain an invalid type, viewport or target size");
	}
	return {};
}
} // namespace Ursa::Rendering
