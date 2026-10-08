#include <Core/Types.hpp>
#include <Application/EditorApplication.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/VirtualPath.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
	Ursa::LogSession logging;
	URSA_LOG_INFO("Editor", "Starting UrsaEditor");
	bool visible = true;
	Ursa::UInt32 frames = 0;
	std::filesystem::path projectRoot;
	std::filesystem::path package;
	std::filesystem::path initialScene;
	for (int index = 1; index < argc; ++index)
	{
		const Ursa::String argument = argv[index];
		if (argument == "--hidden")
		{
			visible = false;
		}
		else if (argument == "--frames" && index + 1 < argc)
		{
			frames = static_cast<Ursa::UInt32>(std::strtoul(argv[++index], nullptr, 10));
		}
		else if (argument == "--pak" && index + 1 < argc)
		{
			package = std::filesystem::u8path(argv[++index]);
		}
		else if (argument == "--project" && index + 1 < argc)
		{
			projectRoot = std::filesystem::u8path(argv[++index]);
		}
		else if (argument == "--scene" && index + 1 < argc)
		{
			initialScene = std::filesystem::u8path(argv[++index]);
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
		             "[--hidden] [--frames N]\n";
		return 2;
	}
	if (initialScene.is_absolute())
	{
		std::cerr << "--scene must be relative to the project Assets folder\n";
		return 2;
	}
	Ursa::Editor::EditorApplication editor;
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
