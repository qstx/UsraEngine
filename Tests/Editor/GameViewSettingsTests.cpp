#include <Core/StringInterop.hpp>
#include <Scene/GameViewSettings.hpp>
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
} // namespace

int main()
{
	try
	{
		Vector<GameViewPreset> presets{{"custom-1", "Portrait", GameViewSizeMode::FixedResolution, 1080, 1920},
		                               {"custom-2", "Cinema", GameViewSizeMode::AspectRatio, 21, 9}};
		const String encodedPresets = SerializeGameViewPresets(presets);
		auto decodedPresets = DeserializeGameViewPresets(encodedPresets, 4096, 16'777'216);
		Expect(decodedPresets && decodedPresets.value.size() == 2 && decodedPresets.value[0].id == "custom-1" &&
		           decodedPresets.value[1].mode == GameViewSizeMode::AspectRatio,
		       "Custom Game presets did not round-trip");
		Expect(!DeserializeGameViewPresets(encodedPresets, 1024, 16'777'216),
		       "Unsupported custom preset dimensions were accepted");
		Expect(!DeserializeGameViewPresets("{\"version\":2,\"presets\":[]}", 4096, 16'777'216),
		       "Unsupported preset settings version was accepted");
		Expect(
		    !DeserializeGameViewPresets("{\"version\":1,\"presets\":[{\"id\":\"same\",\"name\":\"A\",\"mode\":"
		                                "\"FixedResolution\",\"width\":640,\"height\":480},{\"id\":\"same\",\"name\":"
		                                "\"B\",\"mode\":\"FixedResolution\",\"width\":800,\"height\":600}]}",
		                                4096, 16'777'216),
		    "Duplicate custom preset IDs were accepted");

		GameViewSettingsState settings;
		settings.mode = GameViewSizeMode::FixedResolution;
		settings.width = 1920;
		settings.height = 1080;
		settings.scale = 0.5f;
		settings.fit = false;
		settings.pan = {120, -45};
		settings.presetId = "custom-3";
		auto decodedSettings = DeserializeGameViewSettings(SerializeGameViewSettings(settings));
		Expect(decodedSettings && decodedSettings.value.mode == settings.mode && decodedSettings.value.width == 1920 &&
		           decodedSettings.value.height == 1080 && decodedSettings.value.scale == 0.5f &&
		           decodedSettings.value.pan.x == 120 && decodedSettings.value.pan.y == -45 &&
		           decodedSettings.value.presetId == "custom-3",
		       "Per-project Game view settings did not round-trip");
		Expect(!DeserializeGameViewSettings("invalid json"), "Corrupt Game view settings were accepted");
		std::cout << "Game view settings tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
