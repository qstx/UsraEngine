#include <Shader/ShaderImporter.hpp>

#include <Core/StringInterop.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
struct Arguments
{
	std::filesystem::path sourceRoot;
	std::filesystem::path outputRoot;
	std::filesystem::path cacheRoot;
	std::filesystem::path dxc;
	std::filesystem::path spirvValidator;
	std::filesystem::path spirvCross;
};

Arguments ParseArguments(int argc, char** argv)
{
	Arguments result;
	for (int index = 1; index < argc; ++index)
	{
		const std::string_view name(argv[index]);
		if (index + 1 >= argc)
		{
			throw std::runtime_error("Missing value for argument " + std::string(name));
		}
		const std::filesystem::path value = std::filesystem::u8path(argv[++index]);
		if (name == "--source-root")
		{
			result.sourceRoot = value;
		}
		else if (name == "--output-root")
		{
			result.outputRoot = value;
		}
		else if (name == "--cache-root")
		{
			result.cacheRoot = value;
		}
		else if (name == "--dxc")
		{
			result.dxc = value;
		}
		else if (name == "--spirv-val")
		{
			result.spirvValidator = value;
		}
		else if (name == "--spirv-cross")
		{
			result.spirvCross = value;
		}
		else
		{
			throw std::runtime_error("Unknown argument " + std::string(name));
		}
	}
	if (result.sourceRoot.empty() || result.outputRoot.empty() || result.cacheRoot.empty() || result.dxc.empty() ||
	    result.spirvValidator.empty() || result.spirvCross.empty())
	{
		throw std::runtime_error(
		    "Usage: UrsaShaderImporter --source-root PATH --output-root PATH --cache-root PATH --dxc PATH "
		    "--spirv-val PATH --spirv-cross PATH");
	}
	return result;
}

void CreateDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::create_directories(path, error);
	if (error)
	{
		throw std::runtime_error("Cannot create directory " + path.u8string() + ": " + error.message());
	}
}
} // namespace

int main(int argc, char** argv)
{
	try
	{
		const Arguments arguments = ParseArguments(argc, argv);
		CreateDirectory(arguments.outputRoot);
		CreateDirectory(arguments.cacheRoot);
		Ursa::VirtualFileSystem filesystem;
		auto sourceMount = filesystem.Mount("ursa", arguments.sourceRoot, Ursa::MountAccess::ReadOnly);
		if (!sourceMount)
		{
			throw std::runtime_error(Ursa::ToStdString(sourceMount.error));
		}
		auto outputMount = filesystem.Mount("generated", arguments.outputRoot, Ursa::MountAccess::ReadWrite);
		if (!outputMount)
		{
			throw std::runtime_error(Ursa::ToStdString(outputMount.error));
		}
		auto cacheMount = filesystem.Mount("shadercache", arguments.cacheRoot, Ursa::MountAccess::ReadWrite);
		if (!cacheMount)
		{
			throw std::runtime_error(Ursa::ToStdString(cacheMount.error));
		}
		Ursa::Shader::DxcShaderCompiler compiler({arguments.dxc, arguments.spirvValidator, arguments.spirvCross});
		auto version = compiler.Version();
		if (!version)
		{
			throw std::runtime_error(Ursa::ToStdString(version.error));
		}
		auto imported =
		    Ursa::Shader::ShaderImporter::ImportAll(filesystem, compiler, version.value, "ursa", "generated",
		                                            Ursa::VirtualPath::Parse("shadercache:Shader").value);
		if (!imported)
		{
			throw std::runtime_error(Ursa::ToStdString(imported.error));
		}
		std::cout << "Imported " << imported.value.size() << " Shader assets for Vulkan\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
