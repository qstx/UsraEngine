#include <Vfs/PakBuilder.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>

int main(int argc, char** argv)
{
	Ursa::VirtualFileSystem source;
	Ursa::Vector<Ursa::String> schemes;
	std::filesystem::path output;
	for (int index = 1; index < argc; ++index)
	{
		const std::string option = argv[index];
		if (index + 1 >= argc)
		{
			std::cerr << "Usage: UrsaPak --engine <assets> [--engine-generated <assets>] [--project <assets>] --output "
			             "<Content.pak>\n";
			return 2;
		}
		const auto path = std::filesystem::u8path(argv[++index]);
		if (option == "--output")
		{
			output = path;
			continue;
		}
		const Ursa::String scheme = option == "--engine" || option == "--engine-generated" ? "ursa" : "prj";
		if (option != "--engine" && option != "--engine-generated" && option != "--project")
		{
			std::cerr << "Unknown package option: " << option << '\n';
			return 2;
		}
		auto mounted = source.Mount(scheme, path, Ursa::MountAccess::ReadOnly);
		if (!mounted)
		{
			std::cerr << mounted.error.c_str() << '\n';
			return 1;
		}
	}
	if (output.empty() || source.Schemes().empty())
	{
		std::cerr << "Package source and output are required\n";
		return 2;
	}
	std::error_code error;
	output = std::filesystem::absolute(output, error);
	auto provider = Ursa::DirectoryFileProvider::Open(output.parent_path(), true);
	if (error || !provider)
	{
		std::cerr << "Cannot open package output directory\n";
		return 1;
	}
	Ursa::VirtualFileSystem destination;
	auto mounted = destination.MountProvider("out", provider.value, Ursa::MountAccess::ReadWrite);
	auto path = Ursa::VirtualPath::Parse("out:" + Ursa::PathToUrsaString(output.filename()));
	auto built = mounted && path ? Ursa::PakBuilder::Build(source, source.Schemes(), destination, path.value)
	                             : Ursa::Result<void>::Failure("Invalid package output path");
	if (!built)
	{
		std::cerr << built.error.c_str() << '\n';
		return 1;
	}
	auto package = Ursa::PakArchive::Open(output);
	if (!package)
	{
		std::cerr << package.error.c_str() << '\n';
		return 1;
	}
	std::cout << "Built " << output.string() << " with " << package.value->Schemes().size() << " resource schemes\n";
	return 0;
}
