#include <Application/EditorApplication.hpp>
#include <Core/Log.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
	Ursa::LogSession logging;
	URSA_LOG_INFO("Editor", "Starting UrsaEditor");
	bool visible = true;
	uint32_t frames = 0;
	std::filesystem::path projectRoot;
	for (int index = 1; index < argc; ++index)
	{
		const Ursa::String argument = argv[index];
		if (argument == "--hidden")
		{
			visible = false;
		}
		else if (argument == "--frames" && index + 1 < argc)
		{
			frames = static_cast<uint32_t>(std::strtoul(argv[++index], nullptr, 10));
		}
		else if (argument == "--project" && index + 1 < argc)
		{
			projectRoot = std::filesystem::u8path(argv[++index]);
		}
		else
		{
			std::cerr << "Unknown editor option: " << argument.c_str() << '\n';
			return 2;
		}
	}

	if (projectRoot.empty())
	{
		std::cerr << "Usage: UrsaEditor --project <project-directory> [--hidden] [--frames N]\n";
		return 2;
	}
	Ursa::Editor::EditorApplication editor;
	auto opened = editor.OpenProject(projectRoot);
	if (!opened)
	{
		URSA_LOG_CRITICAL("Editor", opened.error);
		return 1;
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
