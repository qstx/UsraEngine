#include <Application/FrameReplay.hpp>
#include <Core/StringInterop.hpp>
#include <fstream>

namespace Ursa
{
namespace
{
bool Matches(const nlohmann::json& actual, const nlohmann::json& expected)
{
	if (!expected.is_object())
	{
		return actual == expected;
	}
	if (!actual.is_object())
	{
		return false;
	}
	for (const auto& entry : expected.items())
	{
		if (!actual.contains(entry.key()) || !Matches(actual[entry.key()], entry.value()))
		{
			return false;
		}
	}
	return true;
}
} // namespace

Result<std::unique_ptr<FrameReplay>> FrameReplay::Open(const std::filesystem::path& path)
{
	try
	{
		std::ifstream input(path);
		nlohmann::json document;
		input >> document;
		if (document.value("format", "") != "UrsaFrameReplay" || document.value("version", 0) != 1 ||
		    !document.at("steps").is_array())
		{
			return Result<std::unique_ptr<FrameReplay>>::Failure("Unsupported frame replay format");
		}
		auto result = std::make_unique<FrameReplay>();
		result->steps = document.at("steps");
		result->output = path.parent_path() / std::filesystem::u8path(document.value("output", "ReplayResult.json"));
		UInt32 previous = 0;
		for (const auto& step : result->steps)
		{
			const auto frame = step.at("frame").get<UInt32>();
			if (!frame || frame <= previous)
			{
				return Result<std::unique_ptr<FrameReplay>>::Failure("Replay frames must increase strictly");
			}
			previous = frame;
		}
		return Result<std::unique_ptr<FrameReplay>>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<FrameReplay>>::Failure(ToUrsaString(error.what()));
	}
}

Result<void> FrameReplay::Feed(UInt32 frame, Window& window,
                               const std::function<std::pair<Float64, Float64>(Float64, Float64)>& mapPointer)
{
	try
	{
		for (const auto& step : steps)
		{
			if (step.at("frame").get<UInt32>() != frame)
			{
				continue;
			}
			for (const auto& input : step.value("events", nlohmann::json::array()))
			{
				WindowEvent event;
				const auto type = input.at("type").get<std::string>();
				if (type == "Move")
				{
					event.type = WindowEventType::CursorMoved;
					event.x = input.at("x");
					event.y = input.at("y");
				}
				else if (type == "Down" || type == "Up")
				{
					event.type = type == "Down" ? WindowEventType::MousePressed : WindowEventType::MouseReleased;
					event.button = static_cast<MouseButton>(input.value("button", 0));
				}
				else if (type == "KeyDown" || type == "KeyUp")
				{
					event.type = type == "KeyDown" ? WindowEventType::KeyPressed : WindowEventType::KeyReleased;
					event.key = static_cast<Key>(input.at("key").get<UInt32>());
				}
				else if (type == "Text")
				{
					event.type = WindowEventType::Character;
					event.codepoint = input.at("codepoint");
				}
				else
				{
					return Result<void>::Failure("Unknown replay event type: " + ToUrsaString(type));
				}
				if (event.type == WindowEventType::CursorMoved && mapPointer)
				{
					const auto mapped = mapPointer(event.x, event.y);
					event.x = mapped.first;
					event.y = mapped.second;
				}
				Detail::WindowAccess::Dispatch(window, event);
			}
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
}

Result<void> FrameReplay::Verify(UInt32 frame, const nlohmann::json& state)
{
	lastFrame = frame;
	for (const auto& step : steps)
	{
		if (step.at("frame").get<UInt32>() != frame)
		{
			continue;
		}
		const bool passed = !step.contains("expect") || Matches(state, step.at("expect"));
		evidence.push_back({{"frame", frame}, {"passed", passed}, {"state", state}});
		if (!passed)
		{
			std::ofstream file(output);
			file << nlohmann::json{{"injectedWindowInput", true}, {"evidence", evidence}}.dump(2);
			return Result<void>::Failure("Frame replay assertion failed at frame " +
			                             ToUrsaString(std::to_string(frame)) + ": " +
			                             ToUrsaString(step["expect"].dump()) + " actual=" + ToUrsaString(state.dump()));
		}
	}
	return {};
}

Result<void> FrameReplay::Finish()
{
	if (!steps.empty() && lastFrame < steps.back().at("frame").get<UInt32>())
	{
		return Result<void>::Failure("Frame replay ended before all assertions executed");
	}
	std::ofstream file(output);
	file << nlohmann::json{{"injectedWindowInput", true}, {"evidence", evidence}}.dump(2);
	return file ? Result<void>{} : Result<void>::Failure("Could not write frame replay evidence");
}
} // namespace Ursa
