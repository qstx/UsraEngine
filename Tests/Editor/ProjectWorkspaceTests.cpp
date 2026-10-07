#include <Workspace/ProjectWorkspace.hpp>
#include <Core/StringInterop.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Asset(const char* relative)
{
	return Take(VirtualPath::Parse(String("prj:") + relative));
}

void WriteHeader(const std::filesystem::path& path, ResourceId id)
{
	std::ofstream out(path);
	out << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << ToStdString(id.ToString()) << "\"}";
	if (!out)
	{
		throw std::runtime_error("Cannot write test resource header");
	}
}

Vector<ResourceMount> engineMounts(const std::filesystem::path& root)
{
	Vector<ResourceMount> mounts;
	mounts.push_back(ResourceMount{engineScheme, root, MountAccess::ReadOnly});
	return mounts;
}
} // namespace

int main()
{
	const auto base =
	    std::filesystem::current_path() / ("editor_workspace_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		const auto project = base / "project";
		const auto assets = project / "Assets";
		const auto engine = base / "engine";
		std::filesystem::create_directories(assets);
		std::filesystem::create_directories(engine);
		const auto id = ResourceId::Generate();
		const auto original = assets / "original.ursamat";
		const auto renamed = assets / "renamed.ursamat";
		WriteHeader(original, id);
		auto workspace = Take(Editor::ProjectWorkspace::Open(project, engineMounts(engine)));
		Expect(workspace->ProjectRoot() == project && workspace->AssetsRoot() == assets,
		       "Project directory and assets root were not separated");
		Expect(workspace->Resources().size() == 1 && workspace->Resources()[0].id == id &&
		           workspace->Resources()[0].location.path.ToString() == "prj:original.ursamat",
		       "Project resource enumeration lost the stable ID or virtual path");
		Expect(static_cast<bool>(workspace->CheckWritable(Asset("new.ursatex"))),
		       "Project write path resolution failed");
		Expect(!VirtualPath::Parse("prj:../Engine/builtin.ursamat"), "Parent escape parsed as a virtual path");
		Expect(!workspace->CheckWritable(Take(VirtualPath::Parse("ursa:builtin.ursamat"))),
		       "Engine asset was accepted as writable");
		std::filesystem::create_directories(assets / "builtin");
		Expect(!Editor::ProjectWorkspace::Open(project, engineMounts(assets / "builtin")),
		       "Engine root inside the writable assets folder was accepted");
		Expect(!Editor::ProjectWorkspace::Open(engine, engineMounts(engine)),
		       "A project without an assets directory was accepted");

		std::filesystem::rename(original, renamed);
		done(workspace->Refresh());
		Expect(workspace->Resources().size() == 1 && workspace->Resources()[0].id == id &&
		           workspace->Resources()[0].location.path.ToString() == "prj:renamed.ursamat",
		       "Resource ID changed after rename and refresh");

		const auto duplicate = engine / "duplicate.ursamat";
		WriteHeader(duplicate, id);
		const auto failed = workspace->Refresh();
		Expect(!failed && failed.error.find("Duplicate resource ID") != String::npos,
		       "Duplicate ID did not report an error");
		Expect(failed.error.find("prj:renamed.ursamat") != String::npos &&
		           failed.error.find("ursa:duplicate.ursamat") != String::npos,
		       "Duplicate ID diagnostic did not use virtual paths");
		Expect(failed.error.find(ToUrsaString(renamed.u8string())) == String::npos,
		       "Duplicate ID diagnostic leaked a host path");
		Expect(workspace->Resources().size() == 1 && workspace->Resources()[0].id == id,
		       "Failed refresh replaced the previous resource index");
		std::filesystem::remove(duplicate);

		const auto outside = base / "outside";
		std::filesystem::create_directory(outside);
#ifdef _WIN32
		const std::string command =
		    "cmd /c mklink /J \"" + (assets / "linked").string() + "\" \"" + outside.string() + "\" > nul 2>&1";
		const bool linked = std::system(command.c_str()) == 0;
#else
		std::error_code symlinkError;
		std::filesystem::create_directory_symlink(outside, assets / "linked", symlinkError);
		const bool linked = !symlinkError;
#endif
		if (linked)
		{
			Expect(!workspace->CheckWritable(Asset("linked/escape.ursatex")),
			       "Link path escaped the project assets root");
		}
		std::filesystem::remove_all(base);
		std::cout << "Editor project workspace test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(base);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
