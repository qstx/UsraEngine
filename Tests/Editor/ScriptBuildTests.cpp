#include <Assets/AssetDatabase.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

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

VirtualPath Path(const char* value)
{
	auto path = VirtualPath::Parse(value);
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	return path.value;
}

void WriteSource(const std::filesystem::path& path, const char* text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << text;
	if (!output)
	{
		throw std::runtime_error("Cannot create ScriptBuild source fixture");
	}
}

String ReadText(VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto text = filesystem.ReadText(path);
	if (!text)
	{
		throw std::runtime_error(ToStdString(text.error));
	}
	return text.value;
}
} // namespace

int main(int argumentCount, char** arguments)
{
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaScriptBuildTests-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		Expect(argumentCount == 6,
		       "ScriptBuildTests requires dotnet, Ursa API assemblies, catalog generator, and reference pack paths");
		std::filesystem::remove_all(root);
		const auto assets = root / "Assets";
		const auto library = root / "Library";
		std::filesystem::create_directories(library);
		WriteSource(assets / "Scripts" / "GameMover.cs",
		            "using Ursa; namespace DemoScripts; "
		            "[UrsaScriptType(\"demo.mover\", Version = 2)] public sealed class GameMover : Behaviour { "
		            "public float step = 1.0f; public override void Update() { if (Input.GetKeyDown(KeyCode.Right)) "
		            "{ Vector3 position = Transform.LocalPosition; position.X += step; Transform.LocalPosition = "
		            "position; } } } "
		            "[UrsaScriptType(\"demo.follower\")] public sealed class Follower : Behaviour { } "
		            "public abstract class AbstractProbe : Behaviour { } "
		            "public sealed class GenericProbe<T> : Behaviour { }\n");
		WriteSource(assets / "Scripts" / "Editor" / "SceneTool.cs",
		            "using Ursa; namespace DemoScripts.Editor; "
		            "[UrsaScriptType(\"editor.scene-tool\")] public sealed class SceneTool : Behaviour { } "
		            "public sealed class EditorUtility { }\n");

		VirtualFileSystem filesystem;
		auto projectMount = filesystem.Mount(projectScheme, assets, MountAccess::ReadWrite);
		Expect(static_cast<bool>(projectMount), "Could not mount ScriptBuild Assets");
		auto libraryMount = filesystem.Mount(editorLibraryScheme, library, MountAccess::ReadWrite);
		Expect(static_cast<bool>(libraryMount), "Could not mount ScriptBuild Library");
		auto database = AssetDatabase::Open(filesystem);
		Expect(static_cast<bool>(database), "Could not open ScriptBuild AssetDatabase");
		auto service =
		    ScriptBuildService::Open(filesystem, *database.value, std::filesystem::u8path(arguments[1]),
		                             std::filesystem::u8path(arguments[2]), std::filesystem::u8path(arguments[3]),
		                             std::filesystem::u8path(arguments[4]), std::filesystem::u8path(arguments[5]));
		Expect(static_cast<bool>(service), "Could not create ScriptBuild service");
		auto projects = service.value->GenerateProjects();
		Expect(static_cast<bool>(projects), "ScriptBuild did not generate its projects");
		Expect(service.value->ScriptTypes().size() == 3,
		       "Roslyn script catalog included abstract/generic types or missed a concrete Behaviour");
		const auto findType = [&service](const char* id) -> ScriptTypeRecord
		{
			for (const ScriptTypeRecord& type : service.value->ScriptTypes())
			{
				if (ToStdString(type.scriptType) == id)
				{
					return type;
				}
			}
			throw std::runtime_error(ToStdString(String("ScriptType is missing from generated catalog: ") + id));
		};
		const ScriptTypeRecord moverType = findType("demo.mover");
		const ScriptTypeRecord followerType = findType("demo.follower");
		const ScriptTypeRecord editorType = findType("editor.scene-tool");
		Expect(moverType.group == ScriptAssemblyGroup::Game && moverType.version == 2 && moverType.localFileId != 0 &&
		           followerType.group == ScriptAssemblyGroup::Game && editorType.group == ScriptAssemblyGroup::Editor &&
		           moverType.fields.size() == 1 && moverType.fields[0].name == "step" &&
		           moverType.fields[0].kind == "FloatingPoint" &&
		           nlohmann::json::parse(ToStdString(moverType.fields[0].defaultValueJson)).get<double>() == 1.0,
		       "Script catalog lost GUID/local-file-ID/version/group identity");
		const String gameProject = ReadText(filesystem, projects.value.gameProject);
		const String editorProject = ReadText(filesystem, projects.value.editorProject);
		Expect(gameProject.find("GameMover.cs") != String::npos && gameProject.find("SceneTool.cs") == String::npos &&
		           gameProject.find("Ursa.Editor") == String::npos,
		       "Game script project included Editor sources or references");
		Expect(editorProject.find("SceneTool.cs") != String::npos &&
		           editorProject.find("GameMover.cs") == String::npos &&
		           editorProject.find("Ursa.Game.dll") != String::npos &&
		           editorProject.find("Ursa.Editor.dll") != String::npos,
		       "Editor script project has an invalid source group or dependency direction");

		auto editorBuild = service.value->Build(ScriptAssemblyGroup::Editor);
		if (!editorBuild)
		{
			throw std::runtime_error("ScriptBuild invocation failed: " + ToStdString(editorBuild.error));
		}
		Expect(editorBuild.value.succeeded, ToStdString(editorBuild.value.diagnostics).c_str());
		Expect(static_cast<bool>(filesystem.Exists(editorBuild.value.assembly).value),
		       "ScriptBuild output did not appear at its VFS Library location");
		auto currentGameBuild = service.value->IsBuildCurrent(ScriptAssemblyGroup::Game);
		Expect(currentGameBuild && currentGameBuild.value,
		       "ScriptBuild did not recognize an assembly built from the current script inputs");
		auto cachedBuild = service.value->Build(ScriptAssemblyGroup::Editor);
		Expect(cachedBuild && cachedBuild.value.succeeded &&
		           cachedBuild.value.diagnostics.find("up to date") != String::npos,
		       "ScriptBuild did not reuse a valid fingerprinted output");
		const VirtualPath editorState = Path("lib:ScriptBuild/State/Editor-Debug.json");
		Expect(static_cast<bool>(filesystem.Remove(editorState)), "Could not invalidate the Library build cache");
		auto rebuiltEditor = service.value->Build(ScriptAssemblyGroup::Editor);
		Expect(rebuiltEditor && rebuiltEditor.value.succeeded &&
		           rebuiltEditor.value.diagnostics.find("up to date") == String::npos,
		       "ScriptBuild did not rebuild after its Library cache was removed");

		WriteSource(assets / "Scripts" / "GameMover.cs",
		            "using Ursa; namespace DemoScripts; "
		            "[UrsaScriptType(\"demo.mover\", Version = 2)] public sealed class GameMover : Behaviour { } "
		            "[UrsaScriptType(\"demo.follower\")] public sealed class Follower : Behaviour { }\n");
		const auto gameStagingRoot = library / "ScriptBuild" / "Staging" / "Game";
		std::set<std::filesystem::path> existingStaging;
		if (std::filesystem::exists(gameStagingRoot))
		{
			for (const auto& entry : std::filesystem::directory_iterator(gameStagingRoot))
			{
				existingStaging.insert(entry.path());
			}
		}
		std::atomic<bool> sourceChangedDuringCompile = false;
		std::thread sourceMutation(
		    [&]
		    {
			    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			    while (std::chrono::steady_clock::now() < deadline)
			    {
				    if (std::filesystem::exists(gameStagingRoot))
				    {
					    for (const auto& entry : std::filesystem::directory_iterator(gameStagingRoot))
					    {
						    if (existingStaging.find(entry.path()) == existingStaging.end())
						    {
							    std::this_thread::sleep_for(std::chrono::milliseconds(20));
							    WriteSource(
							        assets / "Scripts" / "GameMover.cs",
							        "using Ursa; namespace DemoScripts; "
							        "[UrsaScriptType(\"demo.mover\", Version = 2)] public sealed class GameMover : "
							        "Behaviour { } "
							        "[UrsaScriptType(\"demo.follower\")] public sealed class Follower : Behaviour { } "
							        "// changed after build batch started\n");
							    sourceChangedDuringCompile = true;
							    return;
						    }
					    }
				    }
				    std::this_thread::sleep_for(std::chrono::milliseconds(1));
			    }
		    });
		auto staleBatch = service.value->Build(ScriptAssemblyGroup::Game);
		sourceMutation.join();
		Expect(sourceChangedDuringCompile && staleBatch && !staleBatch.value.succeeded &&
		           staleBatch.value.diagnostics.find("changed during compilation") != String::npos,
		       "ScriptBuild published an output after its inputs changed during compilation");
		auto stableBatch = service.value->Build(ScriptAssemblyGroup::Game);
		Expect(stableBatch && stableBatch.value.succeeded,
		       "ScriptBuild did not recover after rejecting a stale build batch");

		WriteSource(assets / "Scripts" / "GameMover.cs",
		            "using Ursa; namespace DemoScripts; "
		            "[UrsaScriptType(\"demo.follower\")] public sealed class Follower : Behaviour { } "
		            "[UrsaScriptType(\"demo.mover\", Version = 2)] public sealed class RenamedMover : Behaviour { } "
		            "public sealed class GenericProbe<T> : Behaviour { } "
		            "public abstract class AbstractProbe : Behaviour { }\n");
		auto staleGameBuild = service.value->IsBuildCurrent(ScriptAssemblyGroup::Game);
		Expect(staleGameBuild && !staleGameBuild.value,
		       "ScriptBuild allowed Run with an assembly stale against changed source inputs");
		Expect(static_cast<bool>(database.value->Refresh()),
		       "AssetDatabase could not refresh reordered script declarations");
		projects = service.value->GenerateProjects();
		Expect(projects && findType("demo.mover").localFileId == moverType.localFileId &&
		           findType("demo.follower").localFileId == followerType.localFileId,
		       "Script catalog identity changed when declaration order/type name changed");

		WriteSource(assets / "Scripts" / "Duplicate.cs",
		            "using Ursa; namespace DemoScripts; [UrsaScriptType(\"demo.mover\")] "
		            "public sealed class DuplicateMover : Behaviour { }\n");
		Expect(static_cast<bool>(database.value->Refresh()),
		       "AssetDatabase could not discover duplicate ScriptType fixture");
		Expect(!service.value->GenerateProjects(), "Script catalog accepted duplicate ScriptType identities");
		Expect(static_cast<bool>(filesystem.Remove(Path("prj:Scripts/Duplicate.cs"))) &&
		           static_cast<bool>(filesystem.Remove(Path("prj:Scripts/Duplicate.cs.meta"))) &&
		           static_cast<bool>(database.value->Refresh()),
		       "Could not remove duplicate ScriptType fixture");
		Expect(static_cast<bool>(service.value->GenerateProjects()),
		       "Script catalog did not recover after duplicate removal");

		auto lastGoodAssembly = filesystem.Read(projects.value.gameAssembly);
		Expect(static_cast<bool>(lastGoodAssembly), "Could not read the last good script assembly");
		WriteSource(assets / "Scripts" / "Broken.cs", "namespace DemoScripts; public class Broken { invalid }\n");
		Expect(static_cast<bool>(database.value->Refresh()), "AssetDatabase did not discover a broken script source");
		auto brokenCatalog = service.value->GenerateProjects();
		Expect(!brokenCatalog && brokenCatalog.error.find("prj:Scripts/Broken.cs") != String::npos,
		       "Script compiler diagnostics did not map the source path back to VFS");
		auto retainedAssembly = filesystem.Read(projects.value.gameAssembly);
		Expect(retainedAssembly && retainedAssembly.value == lastGoodAssembly.value,
		       "A failed script compilation replaced the last good game assembly");

		Expect(static_cast<bool>(filesystem.Remove(Path("prj:Scripts/Broken.cs"))) &&
		           static_cast<bool>(filesystem.Remove(Path("prj:Scripts/Broken.cs.meta"))) &&
		           static_cast<bool>(database.value->Refresh()),
		       "Could not remove the broken script fixture");
		auto recoveredBuild = service.value->Build(ScriptAssemblyGroup::Game);
		Expect(recoveredBuild && recoveredBuild.value.succeeded,
		       "ScriptBuild did not recover after the source error was removed");
		auto manifestText = filesystem.ReadText(Path("lib:ScriptBuild/Manifests/Game.json"));
		Expect(static_cast<bool>(manifestText), "Successful script compilation published no VFS content manifest");
		auto content = nlohmann::json::parse(ToStdString(manifestText.value));
		Expect(content["Version"] == 1 && content["Group"] == "Game" && content["Assemblies"].size() == 1,
		       "Game content manifest included an unexpected assembly group");
		const auto catalogPath = VirtualPath::Parse(ToUrsaString(content["TypeCatalog"].get<std::string>())).value;
		auto publishedCatalog = filesystem.ReadText(catalogPath);
		Expect(static_cast<bool>(publishedCatalog), "Content manifest referred to a missing immutable type catalog");
		auto catalog = nlohmann::json::parse(ToStdString(publishedCatalog.value));
		for (const auto& script : catalog["Scripts"])
		{
			Expect(script["Group"] == "Game", "Player type catalog included Editor scripts");
		}
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
