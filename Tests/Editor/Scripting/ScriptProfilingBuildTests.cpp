#include <Assets/AssetDatabase.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

VirtualPath Path(const String& text)
{
	return Take(VirtualPath::Parse(text));
}

bool Contains(const Vector<UInt8>& bytes, const char* text)
{
	return String(reinterpret_cast<const char*>(bytes.data()), bytes.size()).find(text) != String::npos;
}
} // namespace

int main(int argc, char** argv)
{
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaScriptProfiling-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		Expect(argc == 7, "Expected dotnet, Runtime, Editor, catalog tool, reference pack and configuration");
		const String configuration = argv[6];
		std::filesystem::create_directories(root / "Assets");
		std::filesystem::create_directories(root / "Library");
		{
			std::ofstream source(root / "Assets/Probe.cs");
			source << "using Ursa; public sealed class ProfilingBuildProbe : Behaviour { "
			          "private int Helper(int value) => value + 1; public override void Update() { _ = Helper(2); } }";
		}
		VirtualFileSystem files;
		Expect(bool(files.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite)) &&
		           bool(files.Mount(editorLibraryScheme, root / "Library", MountAccess::ReadWrite)),
		       "Cannot mount test project");
		auto database = Take(AssetDatabase::Open(files));
		auto open = [&](const std::filesystem::path& runtime, const std::filesystem::path& editor,
		                const std::filesystem::path& catalog)
		{
			return Take(ScriptBuildService::Open(files, *database, std::filesystem::u8path(argv[1]), runtime, editor,
			                                     catalog, std::filesystem::u8path(argv[5])));
		};
		auto service =
		    open(std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]), std::filesystem::u8path(argv[4]));
		service->SetDeepProfiling(false);
		const auto normal = Take(service->Build(ScriptAssemblyGroup::Game, configuration));
		Expect(normal.succeeded && Take(service->IsBuildCurrent(ScriptAssemblyGroup::Game, configuration)),
		       "Normal script build was not cached");
		const auto manifestPath = Path("lib:ScriptBuild/Manifests/Game.json");
		const auto normalManifest = Take(files.ReadText(manifestPath));
		Expect(!Contains(Take(files.Read(normal.assembly)), "DeepProfiledAttribute"),
		       "Normal build contains instrumentation");
		service->SetDeepProfiling(true);
		if (configuration == "Publish")
		{
			const auto rejected = service->Build(ScriptAssemblyGroup::Game, configuration);
			Expect(!rejected && rejected.error.find("Publish") != String::npos &&
			           Take(files.ReadText(manifestPath)) == normalManifest,
			       "Publish accepted Deep Profiling or changed live content");
		}
		else
		{
			Expect(!Take(service->IsBuildCurrent(ScriptAssemblyGroup::Game, configuration)),
			       "Deep option did not invalidate cache");
			const auto deep = Take(service->Build(ScriptAssemblyGroup::Game, configuration));
			Expect(deep.succeeded && Contains(Take(files.Read(deep.assembly)), "DeepProfiledAttribute") &&
			           Take(files.ReadText(manifestPath)) != normalManifest,
			       "Deep build did not publish rewritten content");
			service->SetDeepProfiling(false);
			Expect(!Take(service->IsBuildCurrent(ScriptAssemblyGroup::Game, configuration)),
			       "Normal build reused deep content");
			const auto restored = Take(service->Build(ScriptAssemblyGroup::Game, configuration));
			Expect(restored.succeeded && !Contains(Take(files.Read(restored.assembly)), "DeepProfiledAttribute"),
			       "Disabling Deep Profiling retained instrumented IL");
			const auto savedManifest = Take(files.ReadText(manifestPath));
			const auto savedAssembly = Take(files.Read(restored.assembly));
			const auto brokenTools = root / "Tools";
			std::filesystem::create_directories(brokenTools);
			for (const auto& file : std::filesystem::directory_iterator(std::filesystem::u8path(argv[4]).parent_path()))
			{
				if (file.is_regular_file())
				{
					std::filesystem::copy_file(file.path(), brokenTools / file.path().filename());
				}
			}
			{
				std::ofstream invalid(brokenTools / "Ursa.DeepProfiler.dll", std::ios::trunc);
				invalid << "invalid managed image";
			}
			auto failing = open(std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]),
			                    brokenTools / "Ursa.ScriptCatalogGenerator.dll");
			failing->SetDeepProfiling(true);
			const auto failed = failing->Build(ScriptAssemblyGroup::Game, configuration);
			Expect((!failed || !failed.value.succeeded) && Take(files.ReadText(manifestPath)) == savedManifest &&
			           Take(files.Read(restored.assembly)) == savedAssembly,
			       "Failed instrumentation replaced live content");
		}
		const auto variants = root / "Variants/Debug";
		std::filesystem::create_directories(variants);
		for (const auto* name : {"Ursa.Runtime.dll", "Ursa.Rendering.dll", "Ursa.Bindings.dll", "Ursa.Editor.dll"})
		{
			std::filesystem::copy_file(std::filesystem::u8path(argv[2]).parent_path() / name, variants / name);
		}
		auto missing =
		    open(variants / "Ursa.Runtime.dll", variants / "Ursa.Editor.dll", std::filesystem::u8path(argv[4]));
		missing->SetDeepProfiling(false);
		const auto absent = missing->Build(ScriptAssemblyGroup::Game, "Publish");
		Expect(!absent && absent.error.find("matching Publish framework") != String::npos,
		       "Missing Publish framework silently fell back");
		std::cout << "Script profiling modes, immutable content, failed tools and framework variants verified\n";
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << " [fixture " << root.string() << "]\n";
		std::filesystem::remove_all(root);
		return 1;
	}
}
