#include <Assets/AssetImportPipeline.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Scripting/ManagedEditorBridge.hpp>
#include <Core/StringInterop.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <future>
#include <thread>
#include <chrono>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void Write(const std::filesystem::path& path, const std::string& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << text;
	Expect(static_cast<bool>(output), "Cannot write workflow source");
}

std::string Read(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string Replace(std::string text, const std::string& before, const std::string& after)
{
	const auto position = text.find(before);
	Expect(position != std::string::npos, "Workflow script edit has no matching source");
	text.replace(position, before.size(), after);
	return text;
}

void CompareOutputs(const ImportCacheStatus& actual, const ImportCacheStatus& expected)
{
	Expect(actual.mainObjectLocalFileId == expected.mainObjectLocalFileId &&
	           actual.artifacts.size() == expected.artifacts.size(),
	       "Published main object or output count changed");
	for (Size index = 0; index < actual.artifacts.size(); ++index)
	{
		Expect(actual.artifacts[index].localFileId == expected.artifacts[index].localFileId &&
		           actual.artifacts[index].contentFingerprint == expected.artifacts[index].contentFingerprint,
		       "Editor and CLI differ in generated output identity or contents");
	}
}

void Compare(const ImportCacheStatus& actual, const ImportCacheStatus& expected)
{
	Expect(actual.upToDate && actual.fingerprint == expected.fingerprint,
	       "Editor and CLI differ in input fingerprint or readiness");
	CompareOutputs(actual, expected);
}

void ExpectPublished(AssetDatabase& database, ResourceId guid, const ImportCacheStatus& expected)
{
	Expect(Take(database.ResolveImportedArtifact(guid)).localFileId == expected.mainObjectLocalFileId,
	       "Failed import replaced the last published main object");
	for (const auto& output : expected.artifacts)
	{
		const auto actual = Take(database.ResolveImportedArtifact(guid, output.localFileId));
		Expect(actual.contentFingerprint == output.contentFingerprint && actual.size == output.size,
		       "Failed import replaced the last published output bytes");
	}
}

int Run(const std::string& command, const std::filesystem::path& ready = {}, const std::filesystem::path& capture = {})
{
#ifdef _WIN32
	auto text = std::filesystem::u8path(command).wstring();
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(nullptr, text.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
	                    &process))
	{
		return -1;
	}
	if (ready.empty())
	{
		WaitForSingleObject(process.hProcess, INFINITE);
	}
	else
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
		bool captured = false;
		while (WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT)
		{
			if (!captured && std::filesystem::exists(ready))
			{
				const std::string request = std::string("\"") + URSA_WORKFLOW_CAPTURE + "\" --pid " +
				                            std::to_string(process.dwProcessId) + " --output \"" + capture.u8string() +
				                            "\" --timeout-ms 10000";
				captured = Run(request) == 0 && std::filesystem::exists(capture);
				if (captured)
				{
					EnumWindows(
					    [](HWND window, LPARAM identity) -> BOOL
					    {
						    DWORD owner = 0;
						    GetWindowThreadProcessId(window, &owner);
						    wchar_t windowClass[64]{};
						    GetClassNameW(window, windowClass, 64);
						    if (owner == static_cast<DWORD>(identity) && std::wstring(windowClass) == L"GLFW30")
						    {
							    PostMessageW(window, WM_CLOSE, 0, 0);
							    return FALSE;
						    }
						    return TRUE;
					    },
					    static_cast<LPARAM>(process.dwProcessId));
				}
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				TerminateProcess(process.hProcess, 2);
				WaitForSingleObject(process.hProcess, 10000);
				break;
			}
		}
	}
	DWORD code = 1;
	GetExitCodeProcess(process.hProcess, &code);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return static_cast<int>(code);
#else
	return std::system(command.c_str());
#endif
}
} // namespace

int main(int argc, char** argv)
{
	try
	{
		const auto base = std::filesystem::absolute(std::filesystem::u8path(URSA_WORKFLOW_ROOT)).lexically_normal();
		std::filesystem::create_directories(base);
		const auto root = base / ToStdString(ResourceId::Generate().ToString());
		Expect(!std::filesystem::exists(root), "Workflow project must start empty");
		std::filesystem::create_directories(root / "Assets" / "Editor");
		const auto expectedGuid = ResourceId::Generate();
		const auto prefabGuid = ResourceId::Generate();
		const auto examples = std::filesystem::u8path(URSA_WORKFLOW_EXAMPLES);
		std::filesystem::copy_file(examples / "GeneratedFormatAsset.cs", root / "Assets" / "GeneratedFormatAsset.cs");
		for (const char* name : {"DatasetImporter.cs", "DatasetImporterEditor.cs", "GeneratedFormatAssetEditor.cs",
		                         "TextureImportRules.cs", "ModelImportRules.cs"})
		{
			std::filesystem::copy_file(examples / "Editor" / name, root / "Assets" / "Editor" / name);
		}
		const auto editorReport = root / "EditorCallback.txt";
		const auto importerEditorPath = root / "Assets/Editor/DatasetImporterEditor.cs";
		Write(importerEditorPath, Replace(Read(importerEditorPath), "EditorGui.Text(importer.AssetPath);",
		                                  "EditorGui.Text(importer.AssetPath); System.IO.File.WriteAllText(@\"" +
		                                      editorReport.generic_string() + "\", \"drawn\");"));
		std::filesystem::create_directories(root / "Assets" / "GeneratedTextures");
		std::filesystem::create_directories(root / "Assets" / "Models" / "Processed");
		std::filesystem::copy_file(std::filesystem::u8path(URSA_WORKFLOW_ENGINE_ASSETS) /
		                               "Icons/Editor/EditorIcons.png",
		                           root / "Assets/GeneratedTextures/Order.mask.png");
		std::filesystem::copy_file(std::filesystem::u8path(URSA_WORKFLOW_ANIMATED_FBX),
		                           root / "Assets/Models/Processed/Order.fbx");
		const auto gltfModelPath = root / "Assets/Models/Processed/SimpleSkin/simple_skin.gltf";
		std::filesystem::create_directories(gltfModelPath.parent_path());
		std::filesystem::copy_file(std::filesystem::u8path(URSA_WORKFLOW_ANIMATED_GLTF), gltfModelPath);
		Write(root / "Assets/OrderConfig.txt", "0.5");
		Write(root / "Assets/ModelConfig.txt", "ok");
		Write(root / "Assets/Editor/OrderedRules.cs", R"(using System;
using System.Globalization;
using Ursa;
using Ursa.Editor;
public sealed class FirstRules : AssetPostprocessor
{
    public override uint GetVersion() => 1;
    public override int GetPostprocessOrder() => -30;
    public override void OnPreprocessTexture() { ((TextureImporter)AssetImporter).MaxDimension = 2; }
    public override void OnPostprocessTexture(Texture2D texture)
    {
        float value = float.Parse(Context.ReadSourceAssetText("prj:OrderConfig.txt"), CultureInfo.InvariantCulture);
        Color[] pixels = texture.GetPixels();
        for (int i = 0; i < pixels.Length; ++i) pixels[i] = new Color(value, 0, 0, 1);
        texture.SetPixels(pixels);
    }
    public override void OnPostprocessModel(GameObject root)
    {
        if (AssetPath.EndsWith("simple_skin.gltf", StringComparison.Ordinal) &&
            Context.ReadSourceAssetText("prj:ModelConfig.txt") == "fail")
            throw new InvalidOperationException("Injected glTF model postprocessor failure");
        root.Name += "-first";
    }
}
public sealed class SecondRules : AssetPostprocessor
{
    public override int GetPostprocessOrder() => 200;
    public override void OnPreprocessTexture() { ((TextureImporter)AssetImporter).MaxDimension = 2; }
    public override void OnPostprocessTexture(Texture2D texture)
    {
        Color[] pixels = texture.GetPixels();
        for (int i = 0; i < pixels.Length; ++i) pixels[i].R *= 0.5f;
        texture.SetPixels(pixels);
    }
    public override void OnPostprocessModel(GameObject root) { root.Name += "-second"; }
}
)");
		{
			std::ofstream input(root / "Assets" / "Example.ursadataset", std::ios::binary);
			input << R"({"size":2,"label":"Workflow asset","main":"data"})";
		}
		for (const auto& selected : {std::make_pair("Example", expectedGuid), std::make_pair("Prefab", prefabGuid)})
		{
			Write(root / "Assets" / (std::string(selected.first) + ".ursadataset.meta"),
			      nlohmann::json{{"version", 1}, {"guid", ToStdString(selected.second.ToString())}, {"folder", false}}
			          .dump());
		}
		Write(root / "Assets/Prefab.ursadataset", R"({"size":2,"label":"Workflow prefab","main":"prefab"})");
		std::string reader = R"(using System;
using System.Linq;
using Ursa;
using Ursa.Examples;
public sealed class WorkflowReader : Behaviour
{
    public override void Awake()
    {
        var reference = new AssetReference("ASSET_GUID", 0);
        var asset = ScriptableObject.Load<GeneratedFormatAsset>(reference);
        if (asset.label != "Workflow asset" || asset.size != 2 || asset.mesh.GetData().Positions[1].X != 2 ||
            asset.material.Color.G != 1 || asset.texture.Width != 2 || asset.texture.GetPixels()[0].R != 1 ||
            !ReferenceEquals(asset, ScriptableObject.Load<GeneratedFormatAsset>(reference)))
            throw new InvalidOperationException("Player generated resource references differ");
        if (AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "Ursa.Editor" || a.GetName().Name == "Ursa.EditorScripts"))
            throw new InvalidOperationException("Player loaded Editor code");
        System.IO.File.WriteAllText(@"PLAYER_REPORT", "validated");
    }
}
)";
		reader = Replace(reader, "ASSET_GUID", ToStdString(expectedGuid.ToString()));
		reader = Replace(reader, "PLAYER_REPORT", (root / "PlayerReport").generic_string());
		Write(root / "Assets/WorkflowReader.cs", reader);
		{
			VirtualFileSystem authorFiles;
			Done(authorFiles.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite));
			Scene scene;
			Take(Take(scene.CreateGameObject("Player camera"))->AddComponent<CameraComponent>());
			const auto object = Take(scene.CreateGameObject("Runtime validation"));
			Take(object->AddComponent<Scripting::ScriptBehaviourComponent>("WorkflowReader"))
			    ->SetSerializedPayload(
			        R"({"FormatVersion":1,"ScriptType":"test.workflow-reader","Version":1,"Fields":{}})");
			ComponentRegistry registry;
			Done(RegisterBuiltinComponents(registry));
			auto document =
			    Take(EncodeSceneDocument("prj:Workflow.ursascene", ResourceId::Generate(), scene, registry));
			Done(AddPrefabInstance(document, prefabGuid, ResourceId::Generate()));
			Write(root / "Assets/Workflow.ursascene", document.dump());
		}
		const std::string command = std::string("\"") + URSA_WORKFLOW_CLI + "\" --project \"" + root.u8string() +
		                            "\" --engine \"" + URSA_WORKFLOW_ENGINE_ASSETS + "\" --configuration " +
		                            URSA_WORKFLOW_CONFIGURATION + " --profile Player";
		Expect(Run(command) == 0, "Cold CLI scripted asset import failed");
		const auto source = Take(VirtualPath::Parse("prj:Example.ursadataset"));
		const auto textureSource = Take(VirtualPath::Parse("prj:GeneratedTextures/Order.mask.png"));
		const auto modelSource = Take(VirtualPath::Parse("prj:Models/Processed/Order.fbx"));
		const auto gltfModelSource = Take(VirtualPath::Parse("prj:Models/Processed/SimpleSkin/simple_skin.gltf"));
		auto workspace =
		    Take(AssetImportPipeline::OpenProject(root, std::filesystem::u8path(URSA_WORKFLOW_ENGINE_ASSETS)));
		const auto cli = Take(workspace->Database()->CurrentImport(source));
		const auto cliTexture = Take(workspace->Database()->CurrentImport(textureSource));
		const auto cliModel = Take(workspace->Database()->CurrentImport(modelSource));
		const auto cliGltfModel = Take(workspace->Database()->CurrentImport(gltfModelSource));
		Expect(cliTexture.upToDate && cliModel.upToDate && cliGltfModel.upToDate && cliGltfModel.artifacts.size() >= 4,
		       "CLI did not process its PNG, FBX, and animated glTF");
		const auto guid = Take(workspace->Database()->Metadata(source)).guid;
		Expect(cli.upToDate && cli.artifacts.size() == 5 && cli.mainObjectLocalFileId,
		       "CLI did not publish its complete typed outputs");
		const auto playerManifest = Take(workspace->Index().Filesystem().ReadText(
		    Take(RuntimeContentManifestPath(URSA_WORKFLOW_CONFIGURATION, "Player", "WindowsVulkan"))));
		Expect(playerManifest.find("Ursa.Editor.dll") == String::npos &&
		           playerManifest.find("__UrsaManaged/Editor") == String::npos,
		       "Player content includes Editor import code");
		if (argc == 2 && std::string(argv[1]) == "--player")
		{
			const auto snapshot =
			    Take(ReadRuntimeContent(workspace->Index().Filesystem(), URSA_WORKFLOW_CONFIGURATION));
			const auto runtime = Take(
			    OpenRuntimeContentView(std::make_shared<VirtualFileSystem>(workspace->Index().Filesystem()), snapshot));
			VirtualFileSystem output;
			Done(output.MountProvider("out", Take(DirectoryFileProvider::Open(root / "Package", true)),
			                          MountAccess::ReadWrite));
			Done(PakBuilder::Build(runtime, {engineScheme, projectScheme}, output,
			                       Take(VirtualPath::Parse("out:Content.pak"))));
			const std::string player = std::string("\"") + URSA_WORKFLOW_PLAYER + "\"";
			const std::string options = " --scene Workflow.ursascene --hidden --frames 3";
			Expect(Run(player + " --project \"" + root.generic_string() + "\" --engine \"" +
			           URSA_WORKFLOW_ENGINE_ASSETS + "\"" + options) == 0,
			       "Loose Player could not load generated resources and Prefab");
			Expect(Read(root / "PlayerReport") == "validated",
			       "Loose Player did not validate generated Game types and references");
			std::filesystem::remove(root / "PlayerReport");
			Expect(Run(player + " --pak \"" + (root / "Package/Content.pak").generic_string() + "\"" + options) == 0,
			       "Pak Player could not load generated resources and Prefab");
			Expect(Read(root / "PlayerReport") == "validated",
			       "Pak Player did not validate generated Game types and references");
			return 0;
		}
		workspace.reset();
		const auto absoluteRoot = std::filesystem::canonical(root);
		Expect(absoluteRoot.parent_path() == std::filesystem::canonical(base),
		       "Workflow Library move escaped its test directory");
		const auto library = absoluteRoot / "Library";
		const auto saved = absoluteRoot / "CliLibrary";
		Expect(library.parent_path() == absoluteRoot && saved.parent_path() == absoluteRoot &&
		           !std::filesystem::exists(saved),
		       "Unsafe workflow Library move");
		std::filesystem::rename(library, saved);
		workspace = Take(AssetImportPipeline::OpenProject(root, std::filesystem::u8path(URSA_WORKFLOW_ENGINE_ASSETS)));
		Take(workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true));
		const auto editor = Take(workspace->Importer().Import(*workspace->Database(), source));
		const auto status = Take(workspace->Database()->CurrentImport(source));
		Expect(!editor.cacheHit && editor.guid == guid,
		       "Cold Editor import changed source identity or reused a missing cache");
		Compare(status, cli);
		for (const auto& processed : {textureSource, modelSource, gltfModelSource})
		{
			Expect(!Take(workspace->Importer().Import(*workspace->Database(), processed)).cacheHit,
			       "Cold Editor processing reused a missing cache");
		}
		Compare(Take(workspace->Database()->CurrentImport(textureSource)), cliTexture);
		Compare(Take(workspace->Database()->CurrentImport(modelSource)), cliModel);
		Compare(Take(workspace->Database()->CurrentImport(gltfModelSource)), cliGltfModel);
		const auto gltfGuid = Take(workspace->Database()->Metadata(gltfModelSource)).guid;
		const auto gltfMainPath =
		    Take(workspace->Database()->ResolveImportedArtifact(gltfGuid, cliGltfModel.mainObjectLocalFileId));
		const auto gltfMainBytes = Take(workspace->Index().Filesystem().Read(gltfMainPath.path));
		const auto gltfMain = nlohmann::json::from_cbor(gltfMainBytes.begin(), gltfMainBytes.end());
		const auto rootNodeId = gltfMain.at("rootNodeLocalFileId").get<UInt64>();
		const auto rootNode =
		    std::find_if(gltfMain.at("nodes").begin(), gltfMain.at("nodes").end(),
		                 [&](const auto& node) { return node.at("localFileId").get<UInt64>() == rootNodeId; });
		const std::string processedGltfRoot =
		    rootNode == gltfMain.at("nodes").end() ? std::string{} : rootNode->at("name").get<std::string>();
		Expect(processedGltfRoot.size() >= 13 &&
		           processedGltfRoot.compare(processedGltfRoot.size() - 13, 13, "-first-second") == 0,
		       "Ordered managed postprocessors did not preserve their sequence on glTF output");
		Expect(Take(workspace->Importer().Import(*workspace->Database(), source)).cacheHit,
		       "Unchanged worker rules missed their cache");
		const auto previousGltf = Take(workspace->Database()->CurrentImport(gltfModelSource));
		Write(root / "Assets/ModelConfig.txt", "fail");
		const auto rejectedGltf = workspace->Importer().Import(*workspace->Database(), gltfModelSource);
		Expect(!rejectedGltf && rejectedGltf.error.find("Injected glTF model postprocessor failure") != String::npos,
		       "A failing managed glTF postprocessor unexpectedly succeeded");
		ExpectPublished(*workspace->Database(), gltfGuid, previousGltf);
		Write(root / "Assets/ModelConfig.txt", "ok");
		Take(workspace->Importer().Import(*workspace->Database(), gltfModelSource));
		auto resources = Take(ResourceSession::Open(workspace->Index().Filesystem(), workspace->AssetResolver()));
		Expect(Take(LoadScriptableAssetGraph(*resources, {guid, 0})).assets.size() == 1,
		       "Generated main asset cannot be loaded through its runtime alias");
		const auto textureGuid = Take(workspace->Database()->Metadata(textureSource)).guid;
		const auto texture = Take(resources->LoadTexture(textureGuid));
		Expect(texture->GetWidth() <= 2 && texture->GetHeight() <= 2 && texture->GetPixels()[0] == 64,
		       "Published texture does not reflect ordered processor pixels and effective settings");
		resources.reset();
		if (argc == 2 && std::string(argv[1]) == "--inspector")
		{
			auto& files = workspace->Index().Filesystem();
			Done(files.Mount("frameworkui", std::filesystem::u8path(URSA_WORKFLOW_MANAGED_OUTPUT),
			                 MountAccess::ReadOnly));
			auto host = Take(Scripting::DotNetHost::Create());
			Scripting::ManagedHostFiles sdk;
			sdk.runtimeConfig = Take(VirtualPath::Parse("frameworkui:Ursa.ManagedHost.runtimeconfig.json"));
			sdk.entryAssembly = Take(VirtualPath::Parse("frameworkui:Ursa.ManagedHost.dll"));
			for (const char* name : {"Ursa.ManagedHost.deps.json", "Ursa.Runtime.dll", "Ursa.Rendering.dll",
			                         "Ursa.Bindings.dll", "Ursa.Editor.dll"})
			{
				sdk.dependencies.push_back(Take(VirtualPath::Parse("frameworkui:" + String(name))));
			}
			Done(host->Connect(files, sdk, [](UInt32, const String&) {}));
			Done(host->LoadEditorApi(Take(VirtualPath::Parse("frameworkui:Ursa.Editor.dll"))));
			const auto editing = Take(host->CreateScriptSession(true));
			Done(host->LoadScriptContent(editing, Take(VirtualPath::Parse("lib:ScriptBuild/Manifests/Game.json"))));
			Done(host->LoadScriptContent(editing, Take(VirtualPath::Parse("lib:ScriptBuild/Manifests/Editor.json"))));
			Take(host->PrepareEditorExtensions(editing));
			auto importer = Take(AssetImporterInspector::Open(*workspace, source));
			auto fields = Take(SerializedObject::Create(importer->Properties()));
			const auto metadataBytes = Take(files.Read(Take(VirtualPath::Parse(source.ToString() + ".meta"))));
			Done(Take(fields->FindProperty("/Fields/scale")).SetValue(2.0));
			Done(fields->Apply());
			ImGui::CreateContext();
			ImGui::GetIO().DisplaySize = {640, 480};
			ImGui::GetIO().DeltaTime = 1.f / 60.f;
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			UInt32 requests = 0;
			const auto frame = [&]
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowSize({600, 400}, ImGuiCond_Always);
				ImGui::Begin("Scripted importer workflow");
				Take(importer->DrawDefault(
				    [&](const VirtualPath& path)
				    {
					    Expect(path == source, "Scripted Inspector queued a different source");
					    ++requests;
				    },
				    true, workspace.get(),
				    [&](ManagedEditorContext& context) -> Result<bool>
				    {
					    const auto invoked = InvokeManagedEditorExtension(
					        *host, editing, "inspector:importer:" + importer->Identity(), context);
					    return invoked ? Result<bool>::Success(true) : Result<bool>::Failure(invoked.error);
				    }));
				Expect(importer->Diagnostic().empty(), "Compiled ScriptedImporterEditor callback failed");
				ImGui::End();
				ImGui::Render();
			};
			frame();
			frame();
			Expect(importer->HasModified() && requests == 0 &&
			           Take(files.Read(Take(VirtualPath::Parse(source.ToString() + ".meta")))) == metadataBytes,
			       "Scripted Inspector persisted unapplied settings");
			auto* window = ImGui::FindWindowByName("Scripted importer workflow");
			ImGui::ActivateItemByID(ImHashStr("Apply", 0, window->ID));
			frame();
			Expect(!importer->HasModified() && requests == 1 &&
			           Take(workspace->Database()->Metadata(source)).importSettings["Fields"]["scale"] == 2,
			       "ScriptedImporterEditor Apply did not save its settings or queued duplicate imports");
			Take(workspace->Importer().Import(*workspace->Database(), source));
			auto runtime = Take(ResourceSession::Open(files, workspace->AssetResolver()));
			Expect(Take(LoadScriptableAssetGraph(*runtime, {guid, 0})).assets.front().Payload()["Fields"]["size"] == 4,
			       "Scripted Inspector settings did not reach generated runtime data");
			Done(host->CloseScriptSession(editing));
			ImGui::DestroyContext();
			Done(host->CloseEndpoint());
			std::filesystem::remove(editorReport);
			const auto capture = root / "EditorImporter.png";
			const std::string openEditor = std::string("\"") + URSA_WORKFLOW_EDITOR + "\" --project \"" +
			                               root.u8string() +
			                               "\" --asset Example.ursadataset --view Inspector"
			                               " --hidden --frames 100000";
			Expect(Run(openEditor, editorReport, capture) == 0 && std::filesystem::exists(capture) &&
			           std::filesystem::exists(editorReport) && Read(editorReport) == "drawn",
			       "The documented example did not open and capture its compiled importer in the actual Editor");
			return 0;
		}
		const auto nativeSource = Take(VirtualPath::Parse("prj:Unrelated.bin"));
		Write(root / "Assets/Unrelated.bin", "native cache");
		Done(workspace->Database()->Refresh());
		Expect(!Take(workspace->Importer().Import(*workspace->Database(), nativeSource)).cacheHit,
		       "First native import unexpectedly hit cache");
		Write(root / "Assets/OrderConfig.txt", "0.8");
		Expect(!Take(workspace->Importer().Import(*workspace->Database(), textureSource)).cacheHit,
		       "Changed source dependency reused processed pixels");
		Expect(Take(workspace->Importer().Import(*workspace->Database(), source)).cacheHit,
		       "Unrelated source dependency invalidated the scripted importer");
		const auto rulesPath = root / "Assets/Editor/OrderedRules.cs";
		std::string rules = Read(rulesPath);
		const auto rebuild = [&](const std::string& next)
		{
			const auto previous = Take(workspace->Database()->CurrentImport(textureSource));
			Write(rulesPath, next);
			Expect(!workspace->Importer().Import(*workspace->Database(), textureSource),
			       "Obsolete worker accepted changed script inputs");
			Take(workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true));
			Expect(!Take(workspace->Importer().Import(*workspace->Database(), textureSource)).cacheHit,
			       "Changed rules reused an old processed artifact");
			Expect(Take(workspace->Database()->CurrentImport(textureSource)).fingerprint != previous.fingerprint,
			       "Changed rule closure did not alter input fingerprint");
			Expect(Take(workspace->Importer().Import(*workspace->Database(), textureSource)).cacheHit,
			       "Unchanged rule closure missed cache");
			Expect(Take(workspace->Importer().Import(*workspace->Database(), nativeSource)).cacheHit,
			       "Managed rules invalidated an unrelated native asset");
		};
		rules += "\n// Code dependency changes without a version increment.\n";
		rebuild(rules);
		rules = Replace(rules, "GetVersion() => 1", "GetVersion() => 2");
		rebuild(rules);
		rules = Replace(rules, "GetPostprocessOrder() => -30", "GetPostprocessOrder() => -31");
		rebuild(rules);
		rebuild(rules + "\npublic sealed class AddedRules : AssetPostprocessor { public override void "
		                "OnPostprocessTexture(Texture2D texture) {} }\n");
		rebuild(rules);
		const auto lastGood = Take(workspace->Database()->CurrentImport(textureSource));
		Write(rulesPath, rules + "\ninvalid C# syntax\n");
		Expect(!workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true),
		       "Invalid Editor script unexpectedly compiled");
		Expect(!workspace->Importer().Import(*workspace->Database(), textureSource),
		       "Compile failure fell back to old processors");
		Expect(!Take(workspace->Database()->CurrentImport(textureSource)).upToDate,
		       "Old artifacts were marked ready after a compiler failure");
		ExpectPublished(*workspace->Database(), textureGuid, lastGood);
		Write(rulesPath, rules + "\npublic sealed class CircularRules : AssetPostprocessor { public override void "
		                         "OnPreprocessTexture() { _ = new GeneratedByThisImporter(); } }\n");
		const auto circular = workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true);
		Expect(!circular && circular.error.find("compile/import dependency cycle") != String::npos,
		       "Same-round generated script dependency did not explain its compile/import cycle");
		ExpectPublished(*workspace->Database(), textureGuid, lastGood);
		Write(rulesPath, rules + "\npublic sealed class InvalidRules : AssetPostprocessor {}\n");
		Expect(!workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true),
		       "Malformed processor unexpectedly registered");
		Expect(!workspace->Importer().Import(*workspace->Database(), textureSource),
		       "Registration failure fell back to old processors");
		ExpectPublished(*workspace->Database(), textureGuid, lastGood);
		Write(rulesPath, rules);
		Take(workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true));
		Take(workspace->Importer().Import(*workspace->Database(), textureSource));
		Expect(Take(workspace->Importer().Import(*workspace->Database(), textureSource)).cacheHit,
		       "Recovered stable rules did not reuse their successful inputs");
		const auto gateRoot = root / "Gate";
		const std::string gate = "if (System.IO.File.Exists(@\"" + (gateRoot / "Pause").generic_string() +
		                         "\")) { "
		                         "System.IO.File.WriteAllText(@\"" +
		                         (gateRoot / "Entered").generic_string() +
		                         "\", \"entered\"); "
		                         "for (int attempt = 0; !System.IO.File.Exists(@\"" +
		                         (gateRoot / "Release").generic_string() +
		                         "\"); ++attempt) { "
		                         "if (attempt > 1500) throw new System.TimeoutException(\"Import gate timed out\"); "
		                         "System.Threading.Thread.Sleep(10); } }\n";
		const auto gatedRules = Replace(rules, "float value =", gate + "float value =");
		Write(rulesPath, gatedRules);
		Take(workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true));
		Take(workspace->Importer().Import(*workspace->Database(), textureSource));
		for (int change = 0; change != 3; ++change)
		{
			const auto before = Take(workspace->Database()->CurrentImport(textureSource));
			const auto originalImage = Read(root / "Assets/GeneratedTextures/Order.mask.png");
			std::filesystem::create_directories(gateRoot);
			if (std::filesystem::exists(gateRoot / "Entered"))
			{
				std::filesystem::remove(gateRoot / "Entered");
			}
			if (std::filesystem::exists(gateRoot / "Release"))
			{
				std::filesystem::remove(gateRoot / "Release");
			}
			Write(gateRoot / "Pause", "pause");
			std::future<Result<AssetImportResult>> pending;

			struct ReleaseGate
			{
				std::filesystem::path path;

				~ReleaseGate()
				{
					Write(path, "release");
				}
			} release{gateRoot / "Release"};

			pending = std::async(std::launch::async, [&]
			                     { return workspace->Importer().Import(*workspace->Database(), textureSource, true); });
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (!std::filesystem::exists(gateRoot / "Entered") && std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			Expect(std::filesystem::exists(gateRoot / "Entered"),
			       "Import callback did not enter its deterministic publication gate");
			if (change == 0)
			{
				Write(root / "Assets/GeneratedTextures/Order.mask.png", originalImage + "changed while importing");
			}
			else if (change == 1)
			{
				Write(rulesPath, gatedRules + "\n// Saved while the old callback is running.\n");
			}
			else
			{
				workspace->InvalidateManagedImports();
			}
			Write(gateRoot / "Release", "release");
			Expect(!pending.get(), "An obsolete or cancelled callback published its candidate");
			ExpectPublished(*workspace->Database(), textureGuid, before);
			Write(root / "Assets/GeneratedTextures/Order.mask.png", originalImage);
			Write(rulesPath, gatedRules);
			std::filesystem::remove(gateRoot / "Pause");
			Take(workspace->PrepareManagedImports(URSA_WORKFLOW_CONFIGURATION, true));
			Expect(Take(workspace->Importer().Import(*workspace->Database(), textureSource)).cacheHit,
			       "Cancelled import overwrote its last successful candidate");
		}
		{
			auto lease = Take(
			    workspace->Index().Filesystem().AcquireExclusiveLease(Take(VirtualPath::Parse("lib:Import.lock"))));
			Expect(Run(command) != 0, "Concurrent CLI writer ignored the active project import lease");
		}
		workspace.reset();
		std::cout << "Cold Editor/CLI imports match without a window or GPU: " << root.u8string() << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
