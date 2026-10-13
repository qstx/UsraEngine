#include <Core/Types.hpp>
#include <Application/EditorApplication.hpp>
#include <Core/Log.hpp>
#include <Core/Profiler.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/VirtualPath.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
	URSA_PROFILE_APP("UrsaEditor");
	Ursa::LogSession logging;
	URSA_LOG_INFO("Editor", "Starting UrsaEditor");
	bool visible = true;
	Ursa::UInt32 frames = 0;
	std::filesystem::path captureOutput;
	Ursa::UInt32 captureFrame = 1;
	std::filesystem::path projectRoot;
	std::filesystem::path package;
	std::filesystem::path initialScene;
	Ursa::String startupWindow;
	bool startupPlay = false;
	for (int index = 1; index < argc; ++index)
	{
		const Ursa::String argument = argv[index];
		if (argument == "--hidden")
		{
			visible = false;
		}
		else if (argument == "--play")
		{
			startupPlay = true;
		}
		else if (argument == "--frames" && index + 1 < argc)
		{
			frames = static_cast<Ursa::UInt32>(std::strtoul(argv[++index], nullptr, 10));
		}
		else if (argument == "--pak" && index + 1 < argc)
		{
			package = std::filesystem::u8path(argv[++index]);
		}
		else if (argument == "--capture" && index + 1 < argc)
		{
			captureOutput = std::filesystem::u8path(argv[++index]);
		}
		else if (argument == "--capture-frame" && index + 1 < argc)
		{
			char* end = nullptr;
			const auto value = std::strtoull(argv[++index], &end, 10);
			if (!value || value > UINT32_MAX || !end || *end)
			{
				std::cerr << "--capture-frame must be between 1 and 4294967295\n";
				return 2;
			}
			captureFrame = static_cast<Ursa::UInt32>(value);
		}
		else if (argument == "--project" && index + 1 < argc)
		{
			projectRoot = std::filesystem::u8path(argv[++index]);
		}
		else if ((argument == "--scene" || argument == "--asset") && index + 1 < argc)
		{
			initialScene = std::filesystem::u8path(argv[++index]);
		}
		else if (argument == "--view" && index + 1 < argc)
		{
			const Ursa::String view = argv[++index];
			if (view != "Scene" && view != "Game" && view != "Material Preview")
			{
				std::cerr << "--view must be Scene, Game or Material Preview\n";
				return 2;
			}
			startupWindow = view;
		}
		else
		{
			std::cerr << "Unknown editor option: " << argument.c_str() << '\n';
			return 2;
		}
	}

	if (projectRoot.empty())
	{
		std::cerr << "Usage: UrsaEditor --project <project-directory> [--scene <relative-scene-path>] "
		             "[--asset <asset path>] [--view <Scene|Game|Material Preview>] [--hidden] [--frames N] "
		             "[--play] [--capture <output.png>] [--capture-frame N]\n";
		return 2;
	}
	if (initialScene.is_absolute())
	{
		std::cerr << "--scene/--asset must be relative to the project Assets folder\n";
		return 2;
	}
	Ursa::Editor::EditorApplication editor;
	if (startupPlay && initialScene.extension() != ".ursascene")
	{
		std::cerr << "--play requires a .ursascene startup asset\n";
		return 2;
	}
	editor.SetStartupWindow(std::move(startupWindow));
	editor.SetStartupPlay(startupPlay);
	if (!captureOutput.empty())
	{
		auto configured = editor.SetFrameCapture(captureOutput, captureFrame);
		if (!configured)
		{
			URSA_LOG_CRITICAL("Capture", configured.error);
			return 2;
		}
	}
	auto opened = editor.OpenProject(projectRoot, package);
	if (!opened)
	{
		URSA_LOG_CRITICAL("Editor", opened.error);
		return 1;
	}
	if (!initialScene.empty())
	{
		auto scenePath =
		    Ursa::VirtualPath::Parse(Ursa::String("prj:") + Ursa::ToUrsaString(initialScene.generic_string()));
		if (!scenePath)
		{
			URSA_LOG_CRITICAL("Editor", scenePath.error);
			return 1;
		}
		editor.SetStartupAsset(scenePath.value);
	}
	auto result = editor.Run(1280, 720, "Ursa Editor", visible, frames);
	if (!result)
	{
		URSA_LOG_CRITICAL("Editor", result.error);
		return 1;
	}
	URSA_LOG_INFO("Editor", "UrsaEditor stopped");
	return 0;
}
