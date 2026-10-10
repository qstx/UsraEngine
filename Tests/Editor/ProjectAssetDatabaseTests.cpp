#include <Workspace/ProjectWorkspace.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

VirtualPath Path(const char* value)
{
	return Take(VirtualPath::Parse(value));
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

void WriteText(VirtualFileSystem& files, const VirtualPath& path, const std::string& text)
{
	Done(files.WriteAtomic(path, Vector<UInt8>(text.data(), text.data() + text.size())));
}

void AddMetadata(VirtualFileSystem& files, const char* path, ResourceId guid)
{
	WriteText(files, Take(VirtualPath::Parse(String(path) + ".meta")),
	          nlohmann::json{{"version", 1u}, {"guid", ToStdString(guid.ToString())}, {"folder", false}}.dump());
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem source;
		auto builtin = std::make_shared<MemoryFileProvider>();
		Done(source.MountProvider("ursa", builtin, MountAccess::ReadWrite));
		WriteText(source, Path("ursa:Shared.txt"), "builtin");
		const auto builtinGuid = ResourceId::Generate();
		AddMetadata(source, "ursa:Shared.txt", builtinGuid);
		const auto originalMeta = Take(source.Read(Path("ursa:Shared.txt.meta")));
		WriteText(source, Path("ursa:Include.txt"), "include");
		AddMetadata(source, "ursa:Include.txt", ResourceId::Generate());
		for (int project = 0; project < 2; ++project)
		{
			VirtualFileSystem files;
			Done(files.MountProvider("ursa", builtin, MountAccess::ReadOnly));
			Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
			Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
			WriteText(files, Path("prj:Shared.txt"), "project");
			auto workspace = Take(ProjectWorkspace::FromFilesystem({}, files));
			Done(workspace->InitializeBuiltinAssets());
			auto* database = workspace->BuiltinDatabase();
			Expect(database && database->Contains(builtinGuid), "Builtin source is not indexed");
			Expect(Take(workspace->AssetResolver()(builtinGuid, 0)).ToString() == "ursa:Shared.txt",
			       "Builtin identity lost its scheme");
			const nlohmann::json settings = nlohmann::json::object();
			const Vector<VirtualPath> dependencies{Path("ursa:Include.txt")};
			Done(database->PublishImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies,
			                             {{0, "Default", {1, 2, 3}}}));
			Done(database->PublishImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "alternate", settings,
			                             dependencies, {{0, "Default", {4, 5, 6}}}));
			Expect(
			    Take(database->CheckImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies))
			        .upToDate,
			    "Switching targets discarded earlier valid artifacts");
			auto artifact = Take(database->Resolve(builtinGuid));
			Expect(Take(files.Read(artifact)) == Vector<UInt8>({1, 2, 3}), "Target selection resolved wrong data");
			Done(files.WriteAtomic(artifact, {9}));
			Expect(
			    !Take(database->CheckImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies))
			         .upToDate,
			    "Damaged output was considered current");
			Done(database->PublishImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies,
			                             {{0, "Default", {1, 2, 3}}}));
			Done(files.Remove(artifact));
			Expect(
			    !Take(database->CheckImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies))
			         .upToDate,
			    "Missing output was considered current");
			Done(database->PublishImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies,
			                             {{0, "Default", {1, 2, 3}}}));
			Done(workspace->InitializeBuiltinAssets());
			Expect(Take(files.Read(Take(workspace->BuiltinDatabase()->Resolve(builtinGuid)))) ==
			           Vector<UInt8>({1, 2, 3}),
			       "Persistent target selection was not restored");
			database = workspace->BuiltinDatabase();
			WriteText(source, Path("ursa:Include.txt"), "changed include");
			Expect(
			    !Take(database->CheckImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies))
			         .upToDate,
			    "Changed include reused an obsolete import");
			WriteText(source, Path("ursa:Include.txt"), "include");
			WriteText(source, Path("ursa:Shared.txt.meta"),
			          nlohmann::json{{"version", 1u},
			                         {"guid", ToStdString(builtinGuid.ToString())},
			                         {"folder", false},
			                         {"settings", {{"probe", true}}}}
			              .dump());
			Expect(
			    !Take(database->CheckImport(Path("ursa:Shared.txt"), "ursa.Default", 1, "cpu", settings, dependencies))
			         .upToDate,
			    "Changed metadata reused an obsolete import");
			Done(source.WriteAtomic(Path("ursa:Shared.txt.meta"), originalMeta));
			Expect(Take(source.Read(Path("ursa:Shared.txt.meta"))) == originalMeta, "Builtin metadata was modified");
		}
		Done(source.Remove(Path("ursa:Shared.txt.meta")));
		VirtualFileSystem missing;
		Done(missing.MountProvider("ursa", builtin, MountAccess::ReadOnly));
		Done(missing.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(missing.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		auto workspace = Take(ProjectWorkspace::FromFilesystem({}, missing));
		Expect(!workspace->InitializeBuiltinAssets(), "Missing builtin metadata was silently recreated");
		Expect(!Take(source.Exists(Path("ursa:Shared.txt.meta"))), "Readonly metadata check wrote to builtin source");
		std::cout << "Project builtin asset database tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
