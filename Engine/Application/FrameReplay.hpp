#pragma once

#include <Core/Result.hpp>
#include <Window/Window.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace Ursa
{
// Optional deterministic window-event replay for integration diagnostics.
// Dispatches through Window subscriptions; it never invokes a GUI control or
// SceneManager directly. Recordings are explicitly marked as injected input.
class FrameReplay final
{
public:
	static Result<std::unique_ptr<FrameReplay>> Open(const std::filesystem::path& path);
	Result<void> Feed(UInt32 frame, Window& window,
	                  const std::function<std::pair<Float64, Float64>(Float64, Float64)>& mapPointer = {});
	Result<void> Verify(UInt32 frame, const nlohmann::json& state);
	Result<void> Finish();

	UInt32 EndFrame() const
	{
		return steps.empty() ? 0 : steps.back().at("frame").get<UInt32>();
	}

private:
	nlohmann::json steps;
	nlohmann::json evidence = nlohmann::json::array();
	std::filesystem::path output;
	UInt32 lastFrame = 0;
};
} // namespace Ursa
