#include <Assets/AssetDatabase.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Scripting/ExternalScriptEditor.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <Scripting/Bindings/BindingRequirements.hpp>
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
		const auto framework = root / "Framework";
		std::filesystem::create_directories(framework);
		const auto originalFramework = std::filesystem::u8path(arguments[2]).parent_path();
		for (const char* assembly : {"Ursa.Runtime.dll", "Ursa.Rendering.dll", "Ursa.Bindings.dll", "Ursa.Editor.dll"})
		{
			std::filesystem::copy_file(originalFramework / assembly, framework / assembly);
		}
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
		            "public sealed class EditorUtility { private int unused; }\n");
		WriteSource(assets / "Scripts" / "Settings.cs",
		            "using Ursa; using Ursa.Rendering; namespace DemoScripts; "
		            "[UrsaScriptType(\"demo.settings\")] [CreateAssetMenu(MenuName = \"Demo/Settings\", "
		            "FileName = \"GameSettings\", Order = 7)] public sealed class Settings : ScriptableObject { "
		            "public Vector2 size = new(8,4); public Vector4 tint = new(1,2,3,4); "
		            "public OtherSettings other = null!; public Shader shader = null!; public Material? material; "
		            "public ComputeShader[] kernels = System.Array.Empty<ComputeShader>(); "
		            "public System.Collections.Generic.List<Shader> shaders = new(); } "
		            "[UrsaScriptType(\"demo.other-settings\")] public sealed class OtherSettings : ScriptableObject {} "
		            "[CreateAssetMenu(MenuName = \"Demo/Feature\", FileName = \"Feature\")] "
		            "public sealed class FeatureSettings : ScriptableRendererFeature { "
		            "protected override RendererFeature CreateFeature() => new FeatureInstance(); } "
		            "public sealed class FeatureInstance : RendererFeature {} "
		            "public abstract class AbstractSettings : ScriptableObject {} "
		            "public sealed class GenericSettings<T> : ScriptableObject {}\n");

		VirtualFileSystem filesystem;
		auto projectMount = filesystem.Mount(projectScheme, assets, MountAccess::ReadWrite);
		Expect(static_cast<bool>(projectMount), "Could not mount ScriptBuild Assets");
		auto libraryMount = filesystem.Mount(editorLibraryScheme, library, MountAccess::ReadWrite);
		Expect(static_cast<bool>(libraryMount), "Could not mount ScriptBuild Library");
		auto database = AssetDatabase::Open(filesystem);
		Expect(static_cast<bool>(database), "Could not open ScriptBuild AssetDatabase");
		auto service =
		    ScriptBuildService::Open(filesystem, *database.value, std::filesystem::u8path(arguments[1]),
		                             framework / "Ursa.Runtime.dll", framework / "Ursa.Editor.dll",
		                             std::filesystem::u8path(arguments[4]), std::filesystem::u8path(arguments[5]));
		Expect(static_cast<bool>(service), "Could not create ScriptBuild service");
		Expect(static_cast<bool>(service.value->GenerateIdeProjects()),
		       "IDE generation required a successful first Game build");
		auto projects = service.value->GenerateProjects();
		Expect(static_cast<bool>(projects), "ScriptBuild did not generate its projects");
		Expect(service.value->ScriptTypes().size() == 9,
		       "Script catalog missed project types or the built-in rendering asset types");
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
		const ScriptTypeRecord settingsType = findType("demo.settings");
		const ScriptTypeRecord otherSettingsType = findType("demo.other-settings");
		const ScriptTypeRecord universalPipelineType = findType("ursa.rendering.universal-pipeline");
		const ScriptTypeRecord universalRendererType = findType("ursa.rendering.universal-renderer-data");
		const auto featureSettingsType =
		    std::find_if(service.value->ScriptTypes().begin(), service.value->ScriptTypes().end(), [](const auto& type)
		                 { return type.clrTypeName.find("DemoScripts.FeatureSettings") != String::npos; });
		Expect(featureSettingsType != service.value->ScriptTypes().end(),
		       "ScriptableRendererFeature asset type was omitted from the project catalog");
		const auto featureEnabled = std::find_if(featureSettingsType->fields.begin(), featureSettingsType->fields.end(),
		                                         [](const auto& field) { return field.name == "Enabled"; });
		Expect(featureEnabled != featureSettingsType->fields.end() && featureEnabled->defaultValueJson == "true",
		       "New Renderer Feature assets did not default to enabled");
		const auto pipelineField = [&](const String& name) -> const ScriptFieldTypeDescriptor&
		{
			const auto field = std::find_if(universalPipelineType.fields.begin(), universalPipelineType.fields.end(),
			                                [&](const auto& item) { return item.name == name; });
			if (field == universalPipelineType.fields.end())
			{
				throw std::runtime_error("Universal pipeline catalog is missing field: " + ToStdString(name));
			}
			return *field;
		};
		const auto featureList = std::find_if(universalRendererType.fields.begin(), universalRendererType.fields.end(),
		                                      [](const auto& field) { return field.name == "Features"; });
		Expect(universalPipelineType.isRenderPipeline && universalPipelineType.version == 2 &&
		           universalPipelineType.createAssetMenuName == "Rendering/Universal Pipeline" &&
		           universalPipelineType.sourceGuid && universalPipelineType.localFileId != 0 &&
		           universalRendererType.createAssetMenuName == "Rendering/Universal Renderer Data" &&
		           featureList != universalRendererType.fields.end() && featureList->kind == "Array" &&
		           featureList->elementKind == "ScriptableObjectReference" &&
		           featureList->elementTypeName == "Ursa.Rendering.ScriptableRendererFeature" &&
		           pipelineField("MainLightShadowCascadeCount").defaultValueJson == "2" &&
		           pipelineField("MainLightShadowSplitFractions").defaultValueJson == "[0.25]" &&
		           pipelineField("ToneMapping").defaultValueJson == "1" &&
		           pipelineField("BloomThreshold").defaultValueJson == "1" &&
		           pipelineField("BloomIntensity").defaultValueJson == "0.5" &&
		           pipelineField("BloomScatter").defaultValueJson == "0.7",
		       "Built-in Universal pipeline and RendererData assets are not selectable with typed Feature references");
		Expect(std::find(settingsType.baseTypeNames.begin(), settingsType.baseTypeNames.end(),
		                 "Ursa.ScriptableObject") != settingsType.baseTypeNames.end() &&
		           std::find(otherSettingsType.baseTypeNames.begin(), otherSettingsType.baseTypeNames.end(),
		                     "Ursa.ScriptableObject") != otherSettingsType.baseTypeNames.end(),
		       "Script type catalog did not preserve base-type identities for typed asset references");
		const auto settingsField = [&](const String& name) -> const ScriptFieldTypeDescriptor&
		{
			for (const auto& field : settingsType.fields)
			{
				if (field.name == name)
				{
					return field;
				}
			}
			throw std::runtime_error("Missing settings field");
		};
		Expect(settingsType.kind == ScriptTypeKind::ScriptableObject && settingsType.fields.size() == 7 &&
		           settingsType.createAssetMenuName == "Demo/Settings" &&
		           settingsType.createAssetFileName == "GameSettings" && settingsType.createAssetMenuOrder == 7 &&
		           settingsType.sourceGuid == otherSettingsType.sourceGuid &&
		           settingsType.localFileId != otherSettingsType.localFileId &&
		           moverType.kind == ScriptTypeKind::Behaviour && settingsField("shader").kind == "ShaderReference" &&
		           settingsField("kernels").elementKind == "ComputeShaderReference" &&
		           settingsField("shaders").elementKind == "ShaderReference" &&
		           settingsField("material").kind == "MaterialReference" &&
		           settingsField("material").typeName == "Ursa.Material",
		       "ScriptableObject classification or stable subasset identity was lost");
		WriteSource(
		    assets / "Scripts" / "Settings.cs",
		    "using Ursa; namespace DemoScripts; "
		    "[UrsaScriptType(\"demo.settings\")] public sealed class Settings : ScriptableObject {} "
		    "[UrsaScriptType(\"demo.other-settings\")] public sealed class OtherSettings : ScriptableObject {}\n");
		Expect(static_cast<bool>(database.value->Refresh()) && static_cast<bool>(service.value->GenerateProjects()) &&
		           findType("demo.settings").createAssetMenuName.empty(),
		       "Removed CreateAssetMenu attribute remained registered in the refreshed script catalog");
		WriteSource(assets / "Scripts" / "Settings.cs",
		            "using Ursa; namespace DemoScripts; "
		            "[UrsaScriptType(\"demo.settings\")] [CreateAssetMenu(MenuName = \"Demo/Settings\", "
		            "FileName = \"GameSettings\", Order = 7)] public sealed class Settings : ScriptableObject { "
		            "public Vector2 size = new(8,4); public Vector4 tint = new(1,2,3,4); "
		            "public OtherSettings other = null!; public Shader shader = null!; "
		            "public ComputeShader[] kernels = System.Array.Empty<ComputeShader>(); "
		            "public System.Collections.Generic.List<Shader> shaders = new(); } "
		            "[UrsaScriptType(\"demo.other-settings\")] public sealed class OtherSettings : ScriptableObject {} "
		            "public abstract class AbstractSettings : ScriptableObject {} "
		            "public sealed class GenericSettings<T> : ScriptableObject {}\n");
		Expect(static_cast<bool>(database.value->Refresh()) && static_cast<bool>(service.value->GenerateProjects()) &&
		           findType("demo.settings").createAssetMenuName == "Demo/Settings",
		       "CreateAssetMenu was not restored after the type source reloaded");
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
		bool compilerWarning = false;
		for (const auto& diagnostic : editorBuild.value.records)
		{
			compilerWarning |= diagnostic.stage == "Compiler" && diagnostic.severity == "warning" &&
			                   diagnostic.source.relative == "Scripts/Editor/SceneTool.cs" && diagnostic.line > 0 &&
			                   diagnostic.column > 0 && !diagnostic.code.empty();
		}
		Expect(compilerWarning, "Compiler warnings lost structured source locations");
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
		const auto beforeSupportChange = service.value->SourceFingerprint();
		const auto beforeManifest =
		    nlohmann::json::parse(ToStdString(ReadText(filesystem, Path("lib:ScriptBuild/Manifests/Game.json"))));
		{
			// A PE overlay changes the support assembly input without changing project source or its metadata API.
			std::ofstream support(framework / "Ursa.Bindings.dll", std::ios::binary | std::ios::app);
			support.put('\0');
		}
		const auto afterSupportChange = service.value->SourceFingerprint();
		auto supportCurrent = service.value->IsBuildCurrent(ScriptAssemblyGroup::Game);
		Expect(beforeSupportChange && afterSupportChange && beforeSupportChange.value != afterSupportChange.value &&
		           supportCurrent && !supportCurrent.value,
		       "Binding support assembly change did not invalidate unchanged project source");
		auto supportRebuild = service.value->Build(ScriptAssemblyGroup::Editor);
		Expect(supportRebuild && supportRebuild.value.succeeded &&
		           supportRebuild.value.diagnostics.find("up to date") == String::npos,
		       "Binding input change did not rebuild project scripts");
		const auto afterManifest =
		    nlohmann::json::parse(ToStdString(ReadText(filesystem, Path("lib:ScriptBuild/Manifests/Game.json"))));
		Expect(afterManifest.at("BuildFingerprint") != beforeManifest.at("BuildFingerprint") &&
		           afterManifest.at("RequiredBindings") == Scripting::Bindings::ScriptBindingRequirements(false),
		       "Rebuilt content did not publish its new fingerprint and current binding requirements");
		auto incompatibleManifest = afterManifest;
		incompatibleManifest["RequiredBindings"][0]["Hash"] = std::string(64, '0');
		const auto incompatibleText = incompatibleManifest.dump();
		Expect(static_cast<bool>(filesystem.WriteAtomic(
		           Path("lib:ScriptBuild/Manifests/Game.json"),
		           Vector<UInt8>(incompatibleText.data(), incompatibleText.data() + incompatibleText.size()))),
		       "Manifest mismatch setup failed");
		auto incompatibleCurrent = service.value->IsBuildCurrent(ScriptAssemblyGroup::Game);
		Expect(incompatibleCurrent && !incompatibleCurrent.value, "Run accepted content with a stale binding identity");
		auto repairedBindings = service.value->Build(ScriptAssemblyGroup::Game);
		Expect(repairedBindings && repairedBindings.value.succeeded &&
		           nlohmann::json::parse(ToStdString(ReadText(filesystem, Path("lib:ScriptBuild/Manifests/Game.json"))))
		                   .at("RequiredBindings") == Scripting::Bindings::ScriptBindingRequirements(false),
		       "Content cache did not recover from binding identity mismatch");
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
		const auto beforeIdeBuild = filesystem.Read(projects.value.gameAssembly);
		auto ideWorkspace = service.value->GenerateIdeProjects();
		Expect(static_cast<bool>(ideWorkspace), "Could not generate the stale-input IDE fixture");
		auto ideBuild =
		    ExternalScriptEditor::Launch({std::filesystem::u8path(arguments[1]),
		                                  root,
		                                  {"build", PathToUrsaString(ideWorkspace.value.solution), "--configuration",
		                                   "Debug", "--nologo", "--disable-build-servers", "-m:1", "-nr:false"},
		                                  60000});
		if (!ideBuild)
		{
			throw std::runtime_error(ToStdString(ideBuild.error));
		}
		const auto afterIdeBuild = filesystem.Read(projects.value.gameAssembly);
		staleGameBuild = service.value->IsBuildCurrent(ScriptAssemblyGroup::Game);
		Expect(staleGameBuild && !staleGameBuild.value && beforeIdeBuild && afterIdeBuild &&
		           beforeIdeBuild.value == afterIdeBuild.value,
		       "IDE build replaced formal content or made stale formal inputs current");
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
		const auto lastGoodCatalog = filesystem.Read(projects.value.typeCatalog);
		const auto duplicateCatalog = service.value->GenerateProjects();
		Expect(!service.value->LastDiagnostics().empty() &&
		           service.value->LastDiagnostics().back().code == "URSA-CATALOG" &&
		           !service.value->LastDiagnostics().back().source.scheme.empty() &&
		           service.value->LastDiagnostics().back().line > 0,
		       "Type catalog validation error has no structured declaration location");
		Expect(static_cast<bool>(service.value->GenerateIdeProjects()),
		       "Duplicate stable type identities prevented IDE generation");
		const auto retainedCatalog = filesystem.Read(projects.value.typeCatalog);
		Expect(!duplicateCatalog && duplicateCatalog.error.find("DuplicateMover") != String::npos &&
		           duplicateCatalog.error.find("RenamedMover") != String::npos &&
		           duplicateCatalog.error.find("prj:Scripts/Duplicate.cs") != String::npos &&
		           duplicateCatalog.error.find("prj:Scripts/GameMover.cs") != String::npos && lastGoodCatalog &&
		           retainedCatalog && retainedCatalog.value == lastGoodCatalog.value,
		       "Duplicate ScriptType diagnostics lost either origin or replaced the last good catalog");
		Expect(static_cast<bool>(filesystem.Remove(Path("prj:Scripts/Duplicate.cs"))) &&
		           static_cast<bool>(filesystem.Remove(Path("prj:Scripts/Duplicate.cs.meta"))) &&
		           static_cast<bool>(database.value->Refresh()),
		       "Could not remove duplicate ScriptType fixture");
		Expect(static_cast<bool>(service.value->GenerateProjects()),
		       "Script catalog did not recover after duplicate removal");
		WriteSource(assets / "Scripts" / "MenuCollision.cs", "using Ursa; namespace DemoScripts; "
		                                                     "[CreateAssetMenu(MenuName = \"Demo/Settings\")] public "
		                                                     "sealed class MenuCollision : ScriptableObject {}\n");
		Expect(static_cast<bool>(database.value->Refresh()), "AssetDatabase missed duplicate CreateAssetMenu fixture");
		auto duplicateMenu = service.value->GenerateProjects();
		Expect(!duplicateMenu && duplicateMenu.error.find("Duplicate CreateAssetMenu") != String::npos &&
		           duplicateMenu.error.find("MenuCollision") != String::npos &&
		           duplicateMenu.error.find("Settings") != String::npos,
		       "Duplicate CreateAssetMenu declarations did not produce actionable diagnostics");
		Expect(static_cast<bool>(filesystem.Remove(Path("prj:Scripts/MenuCollision.cs"))) &&
		           static_cast<bool>(filesystem.Remove(Path("prj:Scripts/MenuCollision.cs.meta"))) &&
		           static_cast<bool>(database.value->Refresh()) && static_cast<bool>(service.value->GenerateProjects()),
		       "Script catalog did not recover after duplicate CreateAssetMenu removal");
		WriteSource(assets / "Scripts" / "InvalidMenu.cs", "using Ursa; namespace DemoScripts; "
		                                                   "[CreateAssetMenu(MenuName = \"Demo/Abstract\")] public "
		                                                   "abstract class AbstractMenu : ScriptableObject {}\n");
		Expect(static_cast<bool>(database.value->Refresh()), "AssetDatabase missed abstract CreateAssetMenu fixture");
		auto invalidMenu = service.value->GenerateProjects();
		Expect(!invalidMenu &&
		           invalidMenu.error.find("public, concrete, non-generic Game ScriptableObject") != String::npos &&
		           invalidMenu.error.find("AbstractMenu") != String::npos,
		       "Abstract CreateAssetMenu type did not produce a diagnostic");
		Expect(static_cast<bool>(filesystem.Remove(Path("prj:Scripts/InvalidMenu.cs"))) &&
		           static_cast<bool>(filesystem.Remove(Path("prj:Scripts/InvalidMenu.cs.meta"))) &&
		           static_cast<bool>(database.value->Refresh()) && static_cast<bool>(service.value->GenerateProjects()),
		       "Script catalog did not recover after invalid CreateAssetMenu removal");
		WriteSource(assets / "Scripts" / "ForwardCollision.cs",
		            "using Ursa; namespace DemoScripts; "
		            "[UrsaScriptType(\"ursa.rendering.forward\")] public sealed class ForwardCollision : "
		            "ScriptableObject {}\n");
		Expect(static_cast<bool>(database.value->Refresh()), "AssetDatabase missed the framework identity collision");
		auto reservedIdentity = service.value->GenerateProjects();
		Expect(!reservedIdentity &&
		           reservedIdentity.error.find("framework type Ursa.Rendering.ForwardRenderPipelineAsset") !=
		               String::npos &&
		           reservedIdentity.error.find("ForwardCollision") != String::npos,
		       "Project ScriptableObject was allowed to shadow the stable Forward pipeline type identity");
		Expect(static_cast<bool>(filesystem.Remove(Path("prj:Scripts/ForwardCollision.cs"))) &&
		           static_cast<bool>(filesystem.Remove(Path("prj:Scripts/ForwardCollision.cs.meta"))) &&
		           static_cast<bool>(database.value->Refresh()) && static_cast<bool>(service.value->GenerateProjects()),
		       "Script catalog did not recover after removing the framework identity collision");
		WriteSource(
		    assets / "Scripts" / "Settings.cs",
		    "using Ursa; namespace DemoScripts; "
		    "[UrsaScriptType(\"demo.other-settings\")] public sealed class OtherSettings : ScriptableObject {} "
		    "[UrsaScriptType(\"demo.settings\")] public sealed partial class RenamedSettings : ScriptableObject {} "
		    "public sealed partial class RenamedSettings { public int value = 5; }\n");
		Expect(static_cast<bool>(database.value->Refresh()) && static_cast<bool>(service.value->GenerateProjects()) &&
		           findType("demo.settings").localFileId == settingsType.localFileId &&
		           findType("demo.settings").sourceGuid == settingsType.sourceGuid,
		       "ScriptableObject identity changed after a CLR type rename");

		auto lastGoodAssembly = filesystem.Read(projects.value.gameAssembly);
		Expect(static_cast<bool>(lastGoodAssembly), "Could not read the last good script assembly");
		WriteSource(assets / "Scripts" / "Broken.cs", "namespace DemoScripts; public class Broken { invalid }\n");
		Expect(static_cast<bool>(database.value->Refresh()), "AssetDatabase did not discover a broken script source");
		auto brokenCatalog = service.value->GenerateProjects();
		Expect(!service.value->LastDiagnostics().empty() &&
		           service.value->LastDiagnostics().back().source.relative == "Scripts/Broken.cs" &&
		           service.value->LastDiagnostics().back().line > 0 &&
		           !service.value->LastDiagnostics().back().code.empty(),
		       "Roslyn errors lost their structured source location or code");
		Expect(static_cast<bool>(service.value->GenerateIdeProjects()), "Syntax errors prevented IDE generation");
		Expect(!brokenCatalog && brokenCatalog.error.find("prj:Scripts/Broken.cs") != String::npos,
		       "Script compiler diagnostics did not map the source path back to VFS");
		WriteSource(assets / "Scripts" / "Broken.cs", "public class Broken : MissingBaseType { }\n");
		Expect(static_cast<bool>(service.value->ProjectInput()) &&
		           static_cast<bool>(service.value->GenerateIdeProjects()) && !service.value->GenerateProjects(),
		       "Missing types blocked IDE generation or were accepted by the formal catalog");
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
		Expect(content["Version"] == 2 && content["Group"] == "Game" && content["Assemblies"].size() == 1 &&
		           content["RequiredBindings"].size() == 3,
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
