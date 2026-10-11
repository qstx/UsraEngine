#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#if defined(URSA_DOTNET_TEST_EDITOR)
#include <Scripting/ManagedEditorBridge.hpp>
#include <Assets/SceneDraft.hpp>
#include <imgui.h>
#endif
#include <Vfs/PakArchive.hpp>
#include <Vfs/PakBuilder.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

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

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void TestManagedRenderingComponentSerialization(DotNetHost& host, VirtualFileSystem& filesystem,
                                                const VirtualPath& contentPath)
{
	auto resources = Take(ResourceSession::Open(filesystem));
	Scene scene;
	auto sceneApi = std::make_unique<ManagedSceneApi>(scene, resources.get());
	const UInt64 session = Take(host.CreateScriptSession(false, sceneApi.get()));
	Done(host.LoadScriptContent(session, contentPath));
	Done(host.ValidateScriptSession(session));
	auto runtime = std::make_unique<ManagedScriptRuntime>(scene, host, *sceneApi, session);
	auto owner = Take(scene.CreateGameObject("Managed Render Components"));
	Take(runtime->CreateAndAdd(owner, "Ursa.SessionFixtures.ManagedRenderComponentProbe, Ursa.SessionFixtures"));
	FrameContext frame;
	frame.deltaSeconds = 1.0f / 60.0f;
	frame.fixedDeltaSeconds = 1.0f / 50.0f;
	scene.BeginFrame();
	Done(scene.Update(frame));

	auto* camera = scene.FindComponent<CameraComponent>(owner);
	auto* box = scene.FindComponent<BoxRenderComponent>(owner);
	Expect(camera && box, "C# Awake did not add built-in Camera and Box Renderer components");
	Expect(std::abs(camera->verticalFovRadians - 1.1f) < 0.0001f && camera->nearPlane == 0.2f &&
	           camera->farPlane == 80.0f && box->color.red == 0.25f && box->color.green == 0.5f &&
	           box->color.blue == 0.75f,
	       "C# built-in component properties did not update native Scene data");

	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	const MaterialIdResolver resolveMaterial = [&resources](const std::shared_ptr<MaterialInstance>& material)
	{ return resources->IdentifyMaterial(material); };
	const ResourceId sceneId = ResourceId::Generate();
	auto sceneDocument =
	    Take(EncodeSceneDocument("ManagedRenderComponents.ursascene", sceneId, scene, registry, resolveMaterial));
	ComponentLoadContext sceneLoadContext(*resources);
	auto loadedScene =
	    Take(DecodeSceneDocument("ManagedRenderComponents.ursascene", sceneDocument, registry, sceneLoadContext));
	auto loadedRoot = loadedScene.scene->RootGameObjects().front();
	const auto* loadedCamera = loadedScene.scene->FindComponent<CameraComponent>(loadedRoot);
	const auto* loadedBox = loadedScene.scene->FindComponent<BoxRenderComponent>(loadedRoot);
	Expect(loadedCamera && std::abs(loadedCamera->verticalFovRadians - 1.1f) < 0.0001f &&
	           loadedCamera->nearPlane == 0.2f && loadedCamera->farPlane == 80.0f && loadedBox &&
	           loadedBox->color.red == 0.25f && loadedBox->color.green == 0.5f && loadedBox->color.blue == 0.75f,
	       "Managed built-in component properties did not survive Scene serialization");

	ComponentLoadContext prefabCaptureContext(*resources);
	const ResourceId prefabId = ResourceId::Generate();
	auto prefabRoot = GameObject::Create("Managed Render Components");
	auto* prefabCameraSource = owner->FindComponent<CameraComponent>();
	auto* prefabBoxSource = owner->FindComponent<BoxRenderComponent>();
	Expect(prefabCameraSource && prefabBoxSource,
	       "Managed rendering components were unavailable for Prefab serialization");
	auto* detachedCamera = Take(prefabRoot->AddComponent<CameraComponent>());
	detachedCamera->verticalFovRadians = prefabCameraSource->verticalFovRadians;
	detachedCamera->nearPlane = prefabCameraSource->nearPlane;
	detachedCamera->farPlane = prefabCameraSource->farPlane;
	auto* detachedBox = Take(prefabRoot->AddComponent<BoxRenderComponent>());
	detachedBox->color = prefabBoxSource->color;
	detachedBox->material = prefabBoxSource->material;
	PrefabAsset prefab(prefabId, std::move(prefabRoot));
	auto prefabDocument =
	    Take(EncodePrefabAssetDocument("ManagedRenderComponents.ursaprefab", prefab, registry, resolveMaterial));
	ComponentLoadContext prefabLoadContext(*resources);
	auto loadedPrefab =
	    Take(DecodePrefabAssetDocument("ManagedRenderComponents", prefabDocument, registry, prefabLoadContext));
	auto* prefabCamera = loadedPrefab.Root()->FindComponent<CameraComponent>();
	auto* prefabBox = loadedPrefab.Root()->FindComponent<BoxRenderComponent>();
	Expect(prefabCamera && std::abs(prefabCamera->verticalFovRadians - 1.1f) < 0.0001f &&
	           prefabCamera->nearPlane == 0.2f && prefabCamera->farPlane == 80.0f && prefabBox &&
	           prefabBox->color.red == 0.25f && prefabBox->color.green == 0.5f && prefabBox->color.blue == 0.75f,
	       "Managed built-in component properties did not survive Prefab serialization");

	loadedScene.scene->Clear();
	scene.Clear();
	runtime.reset();
	sceneApi.reset();
	Done(host.CloseScriptSession(session));
}

VirtualPath Path(const String& value)
{
	auto path = VirtualPath::Parse(value);
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	return path.value;
}

String Fingerprint(const Vector<UInt8>& bytes)
{
	UInt64 hash = 14695981039346656037ull;
	for (UInt8 value : bytes)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	std::ostringstream result;
	result << std::hex << std::setfill('0') << std::setw(16) << hash;
	return ToUrsaString(result.str());
}

Vector<UInt8> JsonBytes(const nlohmann::json& value)
{
	const std::string text = value.dump();
	const auto* bytes = reinterpret_cast<const UInt8*>(text.data());
	return Vector<UInt8>(bytes, bytes + text.size());
}

bool HasExpectedBytes(const Vector<UInt8>& actual, const Vector<UInt8>& expected)
{
	return actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
}
#if defined(URSA_DOTNET_TEST_EDITOR)
void TestEditorExtensions(DotNetHost& host, VirtualFileSystem& source, const std::filesystem::path& root)
{
	using namespace Ursa::Editor;
	VirtualFileSystem editorFiles;
	Done(editorFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	// Reuse the package's providers through a VFS copy, adding the writable project overlay first.
	auto archive = Take(PakArchive::Open(root / "package" / "Managed.pak"));
	Done(archive->Mount(editorFiles));
	Done(editorFiles.Mount(userDataScheme, root / "user", MountAccess::ReadWrite));
	auto workspace = Take(ProjectWorkspace::FromFilesystem(root / "EditorProject", editorFiles));
	auto documents = Take(EditorDocuments::Create(*workspace));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	auto document = Take(
	    NewSceneDocument(Path("prj:EditorFixture.ursascene"), ResourceKind::Scene, registry, documents->Session()));
	auto draft = document->Payload<SceneDraft>();
	auto owner = Take(draft->GetScene().CreateGameObject("Editor Probe"));
	const ComponentId scriptId = Take(draft->GetScene().AddComponent<ScriptBehaviourComponent>(
	    owner, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
	auto* script = static_cast<ScriptBehaviourComponent*>(draft->GetScene().GetComponent(scriptId));
	script->SetSerializedPayload(
	    R"({"FormatVersion":1,"ScriptType":"test.session-probe","Version":1,"Fields":{"value":7},"FieldTypes":{"value":{"Kind":"SignedInteger","ElementKind":"Unsupported","TypeName":"System.Int32"}}})");
	const ComponentId cameraId = Take(draft->GetScene().AddComponent<CameraComponent>(owner));
	auto* camera = static_cast<CameraComponent*>(draft->GetScene().GetComponent(cameraId));
	Done(documents->RequestOpen(std::move(document)));
	host.SetFilesystem(workspace->Index().Filesystem());
	Done(host.LoadEditorApi(Path("ursa:Ursa.Editor.dll")));
	const UInt64 domain = Take(host.CreateScriptSession(true));
	Done(host.LoadScriptContent(domain, Path("prj:Managed/Game.json")));
	Done(host.LoadScriptAssembly(Take(source.Read(Path("ursa:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host.ValidateScriptSession(domain));
	auto descriptors = nlohmann::json::parse(ToStdString(Take(host.PrepareEditorExtensions(domain))));
	Expect(descriptors.size() == 8,
	       "Managed editor descriptors did not contain the expected menus, window and Inspectors");
	Expect(!host.PrepareEditorExtensions(domain), "The same session registered editor callbacks twice");
	ComponentLoadContext loadContext(documents->Session());
	ManagedEditorContext context;
	context.document = documents->Current();
	context.documents = documents.get();
	context.workspace = workspace.get();
	context.registry = &registry;
	context.selection.object = owner;
	context.selection.roots = draft->GetScene().RootGameObjects();
	context.selection.scene = &draft->GetScene();
	context.selection.loadContext = &loadContext;
	const auto invoke = [&](const String& id) { return InvokeManagedEditorExtension(host, domain, id, context); };
	Done(invoke("menu:Tools/Test/Mark Dirty") ? Result<void>{} : Result<void>::Failure("Managed Dirty menu failed"));
	Expect(documents->Current()->Dirty(), "Managed menu did not mark the editing document Dirty");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Save")), "Managed Save menu failed");
	Expect(!documents->Current()->Dirty() &&
	           Take(workspace->Index().Filesystem().Exists(Path("prj:EditorFixture.ursascene"))),
	       "Managed Save did not persist the document through VFS");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Thread Guard")), "Managed background GUI guard failed");
	Expect(!invoke("menu:Tools/Test/Throw"), "Throwing managed menu was not isolated");
	Expect(static_cast<bool>(invoke("menu:Tools/Test/Mark Dirty")),
	       "A throwing menu disabled another editor extension");
	ImGui::CreateContext();
	ImGui::GetIO().DisplaySize = {800, 600};
	ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	const auto draw = [&](const String& id)
	{
		ImGui::NewFrame();
		ImGui::Begin("Managed editor fixture");
		auto result = invoke(id);
		ImGui::End();
		ImGui::Render();
		return result;
	};
	context.target = script;
	Expect(Take(draw("inspector:test.session-probe")), "Managed Inspector did not report its property Apply");
	auto payload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
	Expect(payload["Fields"]["value"] == 8 && documents->Current()->Dirty(),
	       "Managed Inspector did not use the unified serialized property transaction");
	Expect(!draw("inspector:test.session-probe"),
	       "A retained SerializedProperty was accepted in a later callback scope");
	Expect(nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["value"] == 8,
	       "A stale managed property modified the editing document");
	context.target = camera;
	Expect(Take(draw("inspector:ursa.Camera")) && camera->nearPlane == 0.25f,
	       "Managed Inspector could not edit an exposed native component");
	context.target = nullptr;
	Expect(static_cast<bool>(draw("window:Ursa.EditorFixtures.ProbeWindow")),
	       "Managed tool window could not query selection and draw controls");
	Expect(!draw("window:Ursa.EditorFixtures.ThrowWindow"), "Throwing tool window was not isolated");
	Expect(static_cast<bool>(draw("window:Ursa.EditorFixtures.ProbeWindow")),
	       "A throwing tool window disabled an unrelated window");
	// Edit a detached Prefab graph through the same custom Inspector, in a fresh context.
	Done(host.CloseScriptSession(domain));
	const UInt64 replacement = Take(host.CreateScriptSession(true));
	Done(host.LoadScriptContent(replacement, Path("prj:Managed/Game.json")));
	Done(host.LoadScriptAssembly(Take(source.Read(Path("ursa:TestScripts/Ursa.EditorFixtures.dll")))));
	Done(host.ValidateScriptSession(replacement));
	auto registrations = nlohmann::json::parse(ToStdString(Take(host.PrepareEditorExtensions(replacement))));
	Expect(registrations.size() == descriptors.size(), "Reload duplicated extension registrations");
	auto prefabRoot = GameObject::Create("Detached Prefab");
	auto prefabScript = Take(prefabRoot->AddComponent<ScriptBehaviourComponent>(script->TypeName()));
	prefabScript->SetSerializedPayload(script->SerializedPayload());
	context.target = prefabScript;
	context.selection.object = prefabRoot;
	context.selection.roots = {prefabRoot};
	context.selection.scene = nullptr;
	ImGui::NewFrame();
	ImGui::Begin("Detached Prefab fixture");
	auto prefabEdited = InvokeManagedEditorExtension(host, replacement, "inspector:test.session-probe", context);
	ImGui::End();
	ImGui::Render();
	Expect(prefabEdited && prefabEdited.value &&
	           nlohmann::json::parse(ToStdString(prefabScript->SerializedPayload()))["Fields"]["value"] == 9,
	       "Reloaded Inspector retained old state or failed to edit a detached Prefab");
	Done(host.CloseScriptSession(replacement));
	ImGui::DestroyContext();
	Expect(!invoke("menu:Tools/Test/Mark Dirty"), "Closed editing context retained a menu delegate");
	host.SetFilesystem(source);
}
#endif

} // namespace

int main(int argumentCount, char** arguments)
{
	const auto root = std::filesystem::u8path(argumentCount > 2 ? arguments[2] : "dotnet_host_smoke");
	try
	{
		Expect(argumentCount == 3, "Managed host smoke test requires managed output and writable cache directories");
		const auto managedOutput = std::filesystem::u8path(arguments[1]);
		const auto packageRoot = root / "package";
		const auto userRoot = root / "user";
		std::filesystem::create_directories(packageRoot);
		std::filesystem::create_directories(userRoot);

		const Vector<String> fileNames{"Ursa.ManagedHost.runtimeconfig.json", "Ursa.ManagedHost.deps.json",
		                               "Ursa.ManagedHost.dll", "Ursa.Runtime.dll", "Ursa.Editor.dll"};
		VirtualFileSystem looseSource;
		Done(looseSource.Mount(engineScheme, managedOutput, MountAccess::ReadOnly));
		Vector<PakEntryData> entries;
		for (const String& fileName : fileNames)
		{
			const VirtualPath source = Path(String(engineScheme) + ":" + fileName);
			auto bytes = Take(looseSource.Read(source));
			entries.push_back({source, std::move(bytes)});
		}
		const VirtualPath scriptAssemblyPath = Path("ursa:TestScripts/Ursa.ManagedHost.ProtocolTests.dll");
		entries.push_back({scriptAssemblyPath, Take(looseSource.Read(scriptAssemblyPath))});
		const VirtualPath sessionFixturePath = Path("ursa:TestScripts/Ursa.SessionFixtures.dll");
		const auto sessionFixtureBytes = Take(looseSource.Read(sessionFixturePath));
		entries.push_back({sessionFixturePath, sessionFixtureBytes});
		const VirtualPath dependencyPath = Path("ursa:TestScripts/Ursa.SessionDependency.dll");
		const auto dependencyBytes = Take(looseSource.Read(dependencyPath));
		entries.push_back({dependencyPath, dependencyBytes});
#if defined(URSA_DOTNET_TEST_EDITOR)
		const VirtualPath editorFixturePath = Path("ursa:TestScripts/Ursa.EditorFixtures.dll");
		entries.push_back({editorFixturePath, Take(looseSource.Read(editorFixturePath))});
#endif
		const VirtualPath sessionCatalogPath = Path("prj:Managed/ScriptTypeCatalog.json");
		const VirtualPath sessionContentPath = Path("prj:Managed/Game.json");
		nlohmann::json catalog{{"Version", 1u}};
		catalog["Scripts"] = nlohmann::json::array();
		for (const auto& type : Vector<std::pair<String, String>>{{"SessionProbe", "test.session-probe"},
		                                                          {"LeakingProbe", "test.leaking-probe"}})
		{
			catalog["Scripts"].push_back(
			    {{"SourceGuid", "11111111-1111-4111-8111-111111111111"},
			     {"LocalFileId", type.first == "SessionProbe" ? 1ull : 2ull},
			     {"ScriptType", ToStdString(type.second)},
			     {"Version", 1u},
			     {"Group", "Game"},
			     {"ClrTypeName", ToStdString("Ursa.SessionFixtures." + type.first + ", Ursa.SessionFixtures")}});
		}
		const auto catalogBytes = JsonBytes(catalog);
		nlohmann::json content{{"Version", 1u},
		                       {"Group", "Game"},
		                       {"TypeCatalog", ToStdString(sessionCatalogPath.ToString())},
		                       {"CatalogFingerprint", ToStdString(Fingerprint(catalogBytes))}};
		content["Assemblies"] =
		    nlohmann::json::array({{{"Name", "Ursa.SessionFixtures"},
		                            {"Path", ToStdString(sessionFixturePath.ToString())},
		                            {"Fingerprint", ToStdString(Fingerprint(sessionFixtureBytes))}}});
		auto missingDependency = content;
		entries.push_back({Path("prj:Managed/MissingDependency.json"), JsonBytes(missingDependency)});
		content["Assemblies"].push_back({{"Name", "Ursa.SessionDependency"},
		                                 {"Path", ToStdString(dependencyPath.ToString())},
		                                 {"Fingerprint", ToStdString(Fingerprint(dependencyBytes))}});
		entries.push_back({sessionCatalogPath, catalogBytes});
		entries.push_back({sessionContentPath, JsonBytes(content)});
		auto badContent = content;
		badContent["Assemblies"][0]["Fingerprint"] = "0000000000000000";
		entries.push_back({Path("prj:Managed/Invalid.json"), JsonBytes(badContent)});
		badContent = content;
		badContent["Group"] = "Editor";
		entries.push_back({Path("prj:Managed/Editor.json"), JsonBytes(badContent)});
		auto packageBytes = Take(PakBuilder::Encode(std::move(entries)));
		{
			std::ofstream packageOutput(packageRoot / "Managed.pak", std::ios::binary);
			packageOutput.write(reinterpret_cast<const char*>(packageBytes.data()),
			                    static_cast<std::streamsize>(packageBytes.size()));
			Expect(static_cast<bool>(packageOutput), "Cannot create the managed bootstrap package fixture");
		}

		auto archive = Take(PakArchive::Open(packageRoot / "Managed.pak"));
		VirtualFileSystem filesystem;
		Done(archive->Mount(filesystem));
		Done(filesystem.Mount(userDataScheme, userRoot, MountAccess::ReadWrite));
		ManagedHostFiles files;
		files.runtimeConfig = Path("ursa:Ursa.ManagedHost.runtimeconfig.json");
		files.entryAssembly = Path("ursa:Ursa.ManagedHost.dll");
		files.dependencies = {Path("ursa:Ursa.ManagedHost.deps.json"), Path("ursa:Ursa.Runtime.dll")};
		files.scriptAssemblies = {scriptAssemblyPath};

		auto staged = Take(MaterializeManagedHostFiles(filesystem, files));
		Expect(staged.CacheDirectory().scheme == userDataScheme &&
		           staged.CacheDirectory().relative.find("ManagedBootstrap/") == 0,
		       "Managed bootstrap was not staged in the writable user VFS mount");
		const VirtualPath stagedRuntimeConfig =
		    Path(staged.CacheDirectory().ToString() + "/Ursa.ManagedHost.runtimeconfig.json");
		const auto stagedRuntimeConfigBytes = Take(filesystem.Read(stagedRuntimeConfig));
		const auto sourceRuntimeConfigBytes = Take(filesystem.Read(files.runtimeConfig));
		Expect(HasExpectedBytes(stagedRuntimeConfigBytes, sourceRuntimeConfigBytes),
		       "VFS bootstrap staging changed runtime configuration bytes");
		auto runtimePath = Take(filesystem.ResolveForNativeHostInterop(stagedRuntimeConfig));
		std::error_code pathError;
		const auto relativeBootstrap = std::filesystem::relative(
		    std::filesystem::weakly_canonical(runtimePath), std::filesystem::weakly_canonical(userRoot), pathError);
		Expect(!pathError && std::filesystem::is_regular_file(runtimePath) && !relativeBootstrap.empty() &&
		           !relativeBootstrap.is_absolute() && *relativeBootstrap.begin() != "..",
		       "Native host path did not resolve inside the writable user cache");
		Expect(!filesystem.ResolveForNativeHostInterop(files.runtimeConfig),
		       "Native host path resolution accepted a package source file");

		ManagedHostFiles reordered = files;
		std::reverse(reordered.dependencies.begin(), reordered.dependencies.end());
		auto stagedAgain = Take(MaterializeManagedHostFiles(filesystem, reordered));
		Expect(stagedAgain.CacheDirectory() == staged.CacheDirectory(),
		       "Managed bootstrap fingerprint changed when dependencies were reordered");
		ManagedHostFiles missing = files;
		missing.entryAssembly = Path("ursa:MissingManagedHost.dll");
		auto missingResult = MaterializeManagedHostFiles(filesystem, missing);
		Expect(!missingResult && !missingResult.error.empty(), "Missing VFS bootstrap source was accepted");

		VirtualFileSystem readOnlyCache;
		Done(archive->Mount(readOnlyCache));
		auto memoryProvider = std::make_shared<MemoryFileProvider>();
		Done(readOnlyCache.MountProvider(userDataScheme, memoryProvider, MountAccess::ReadOnly));
		auto readOnlyResult = MaterializeManagedHostFiles(readOnlyCache, files);
		Expect(!readOnlyResult && readOnlyResult.error.find("read-only") != String::npos,
		       "Managed bootstrap wrote to a read-only user mount");

		Scene scene;
		ManagedSceneApi sceneApi(scene);
		auto host = Take(DotNetHost::Create());
		std::vector<std::string> messages;
		std::vector<UInt32> logLevels;
		const VirtualPath invalidRuntimeConfig = Path("user:ManagedBootstrap/invalid.runtimeconfig.json");
		const Vector<UInt8> invalidRuntimeConfigBytes{'{', 'n', 'o', 't', '-', 'j', 's', 'o', 'n', '}'};
		Done(filesystem.WriteAtomic(invalidRuntimeConfig, invalidRuntimeConfigBytes));
		ManagedHostFiles invalidRuntime = files;
		invalidRuntime.runtimeConfig = invalidRuntimeConfig;
		auto failedStart = host->Start(filesystem, invalidRuntime, [](UInt32, const String&) {});
		Expect(!failedStart && !host->IsInitialized(),
		       "Invalid runtime configuration unexpectedly initialized the managed host");

		const VirtualPath missingRuntimeConfig = Path("user:ManagedBootstrap/missing.runtimeconfig.json");
		Done(filesystem.WriteAtomic(
		    missingRuntimeConfig,
		    JsonBytes({{"runtimeOptions",
		                {{"tfm", "net99.0"},
		                 {"rollForward", "Disable"},
		                 {"framework", {{"name", "Microsoft.NETCore.App"}, {"version", "99.0.0"}}}}}})));
		ManagedHostFiles unavailableRuntime = files;
		unavailableRuntime.runtimeConfig = missingRuntimeConfig;
		auto unavailable = host->Start(filesystem, unavailableRuntime, [](UInt32, const String&) {});
		Expect(!unavailable && unavailable.error.find("Cannot initialize CoreCLR") != String::npos &&
		           !host->IsInitialized(),
		       "Unavailable framework produced no diagnostic or prevented a corrected startup");

		auto started = host->Start(
		    filesystem, files,
		    [&messages, &logLevels](UInt32 level, const String& message)
		    {
			    logLevels.push_back(level);
			    messages.emplace_back(message.c_str(), message.size());
		    },
		    &sceneApi);
		Expect(static_cast<bool>(started), ToStdString(started.error).c_str());
		Expect(host->IsInitialized() && logLevels == std::vector<UInt32>({1, 2}) && messages.size() == 2 &&
		           messages.front() == u8"Ursa 托管运行时已初始化" && messages.back().empty(),
		       "Native-to-managed initialization or UTF-8/empty-data callback failed");
		ManagedScriptRuntime scriptRuntime(scene, *host, sceneApi);
		const String probeType = "ProtocolBehaviour, Ursa.ManagedHost.ProtocolTests";
		auto scriptedObject = Take(scene.CreateGameObject("Mixed"));
		auto secondScriptedObject = Take(scene.CreateGameObject("Second"));
		Take(scriptRuntime.CreateAndAdd(scriptedObject, probeType));
		Take(scriptRuntime.CreateAndAdd(secondScriptedObject, probeType));
		const auto addFailureProbe = [&scene, &scriptRuntime](const String& objectName, const String& typeName)
		{
			auto object = Take(scene.CreateGameObject(objectName));
			const ComponentId component = Take(scriptRuntime.CreateAndAdd(object, typeName));
			return std::make_pair(std::move(object), component);
		};
		auto awakeFailure = addFailureProbe("AwakeFailure", "ThrowOnAwakeBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto fixedFailure =
		    addFailureProbe("FixedFailure", "ThrowOnFixedUpdateBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto updateFailure = addFailureProbe("UpdateFailure", "ThrowOnUpdateBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto serializedOwner = Take(scene.CreateGameObject("Serialized"));
		const ComponentId serializedComponent = Take(scene.AddComponent<ScriptBehaviourComponent>(
		    serializedOwner, "SerializedProbeBehaviour, Ursa.ManagedHost.ProtocolTests"));
		auto* serializedScript = static_cast<ScriptBehaviourComponent*>(scene.GetComponent(serializedComponent));
		serializedScript->SetSerializedPayload(
		    R"({"FormatVersion":1,"ScriptType":"test.serialized-probe","Version":1,"Fields":{"savedValue":17}})");
		Done(scriptRuntime.AttachSerializedComponents());
		auto disableFailure =
		    addFailureProbe("DisableFailure", "ThrowOnDisableBehaviour, Ursa.ManagedHost.ProtocolTests");
		auto destroyFailure =
		    addFailureProbe("DestroyFailure", "ThrowOnDestroyBehaviour, Ursa.ManagedHost.ProtocolTests");
		FrameContext frame;
		frame.deltaSeconds = 1.0f / 60.0f;
		frame.fixedDeltaSeconds = 1.0f / 50.0f;
		frame.frameIndex = 1;
		scene.BeginFrame();
		Done(scene.FixedUpdate(frame));
		Done(scene.Update(frame));
		Done(scene.LateUpdate(frame));
		const auto awakeEnabled = scene.IsComponentEnabled(awakeFailure.second);
		const auto fixedEnabled = scene.IsComponentEnabled(fixedFailure.second);
		const auto updateEnabled = scene.IsComponentEnabled(updateFailure.second);
		Expect(awakeEnabled && !awakeEnabled.value && fixedEnabled && !fixedEnabled.value && updateEnabled &&
		           !updateEnabled.value,
		       "Managed Awake/FixedUpdate/Update exceptions were not isolated by disabling their Behaviours");
		Expect(Take(scriptedObject->Name()) == "Mixed:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate",
		       "Mixed native/managed Scene did not dispatch callbacks with the GameObject handle bound");
		Expect(Take(secondScriptedObject->Name()) == "Second:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate",
		       "Two managed Behaviours did not retain independent instances and GameObject references");
		Expect(Take(serializedOwner->Name()) == "Serialized:Awake:17",
		       "Serialized script fields were not restored before managed Awake");
		Done(scene.SetActive(scriptedObject, false));
		Done(scene.SetActive(disableFailure.first, false));
		scene.BeginFrame();
		Done(scene.Update(frame));
		const std::string disabledName = ToStdString(Take(scriptedObject->Name()));
		Expect(disabledName.find(":OnDisable") != std::string::npos,
		       "Managed Behaviour did not receive OnDisable when its GameObject became inactive");
		Expect(ToStdString(Take(secondScriptedObject->Name())) ==
		           "Second:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate:Update",
		       "A disabled Behaviour stopped an independent script instance");
		Expect(static_cast<bool>(scene.DestroyGameObject(disableFailure.first)),
		       "OnDisable exception prevented owner destruction");
		Expect(static_cast<bool>(scene.DestroyGameObject(destroyFailure.first)),
		       "OnDestroy exception prevented owner destruction");
		Done(scene.DestroyGameObject(scriptedObject));
		auto firstDestroyMarker = Take(filesystem.Read(Path("user:ManagedBehaviourOnDestroy.txt")));
		Expect(
		    ToStdString(String(reinterpret_cast<const char*>(firstDestroyMarker.data()), firstDestroyMarker.size())) ==
		        "Mixed:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate:OnDisable:OnDestroy",
		    "Managed OnDestroy could not access its owner before native cleanup");
		Done(scene.DestroyGameObject(secondScriptedObject));
		auto secondDestroyMarker = Take(filesystem.Read(Path("user:ManagedBehaviourOnDestroy.txt")));
		const std::string secondDestroyName =
		    ToStdString(String(reinterpret_cast<const char*>(secondDestroyMarker.data()), secondDestroyMarker.size()));
		Expect(secondDestroyName ==
		           "Second:Awake:OnEnable:Start:FixedUpdate:Update:LateUpdate:Update:OnDisable:OnDestroy",
		       ("Second managed Behaviour did not retain independent OnDestroy state: " + secondDestroyName).c_str());
		Done(scene.DestroyGameObject(awakeFailure.first));
		Done(scene.DestroyGameObject(fixedFailure.first));
		Done(scene.DestroyGameObject(updateFailure.first));
		Done(scene.DestroyGameObject(serializedOwner));
		auto mover = Take(scene.CreateGameObject("Mover"));
		Take(scriptRuntime.CreateAndAdd(mover, "MoveWithInputBehaviour, Ursa.ManagedHost.ProtocolTests"));
		FrameContext moveFrame;
		moveFrame.deltaSeconds = 1.0f / 60.0f;
		moveFrame.fixedDeltaSeconds = 1.0f / 50.0f;
		moveFrame.frameIndex = 2;
		const Size rightIndex = static_cast<Size>(Key::Right);
		moveFrame.input.keys[rightIndex] = true;
		moveFrame.input.keyPressed[rightIndex] = true;
		scene.BeginFrame();
		Done(scene.Update(moveFrame));
		const auto movedTransform = Take(scene.GetLocalTransform(mover));
		Expect(movedTransform.position.x == 1.0f,
		       "Managed input did not move the native Transform through the backend-neutral API");
		Done(scene.DestroyGameObject(mover));
		Expect(messages.size() >= 2, "Managed lifecycle dispatch damaged the host logging callback");
		TestManagedRenderingComponentSerialization(*host, filesystem, sessionContentPath);

#if defined(URSA_DOTNET_TEST_EDITOR)
		TestEditorExtensions(*host, filesystem, root);
#endif
		Scene missingDependencyScene;
		ManagedSceneApi missingDependencyApi(missingDependencyScene);
		const UInt64 missingDependencyDomain = Take(host->CreateScriptSession(false, &missingDependencyApi));
		Expect(!host->LoadScriptContent(missingDependencyDomain, Path("prj:Managed/MissingDependency.json")),
		       "A project dependency missing from the manifest fell back to another session or the host");
		Done(host->CloseScriptSession(missingDependencyDomain));
		// Separate contexts retain independent statics, even while the edit session stays loaded.
		Scene editScene;
		ManagedSceneApi editApi(editScene);
		const UInt64 editDomain = Take(host->CreateScriptSession(true, &editApi));
		Done(host->LoadScriptContent(editDomain, sessionContentPath));
		Done(host->ValidateScriptSession(editDomain));
		nlohmann::json reloadSnapshots =
		    nlohmann::json::array({{{"Id", "22222222-2222-4222-8222-222222222222"},
		                            {"Payload",
		                             {{"FormatVersion", 1u},
		                              {"ScriptType", "test.session-probe"},
		                              {"Version", 1u},
		                              {"Fields", {{"value", 11}, {"unknownSavedField", 42}}}}}}});
		auto restoredSnapshots = Take(host->PrepareScriptReload(editDomain, ToUrsaString(reloadSnapshots.dump())));
		auto candidateFields = nlohmann::json::parse(ToStdString(restoredSnapshots))[0]["Payload"]["Fields"];
		Expect(candidateFields["value"] == 11 && candidateFields["position"] == nlohmann::json({1, 2, 3}) &&
		           candidateFields["unknownSavedField"] == 42,
		       "Editing reload lost saved fields, constructor defaults or unknown data");
		reloadSnapshots[0]["Payload"]["Version"] = 2u;
		Expect(!host->PrepareScriptReload(editDomain, ToUrsaString(reloadSnapshots.dump())),
		       "Candidate reload accepted an incompatible field version");
		Done(host->SelectScriptSession(editDomain));
		ManagedScriptRuntime editRuntime(editScene, *host, editApi, editDomain);
		auto editObject = Take(editScene.CreateGameObject("Edit"));
		Take(editRuntime.CreateAndAdd(editObject, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
		Expect(Take(editObject->Name()) == "Edit", "Editing a script invoked gameplay callbacks");
		for (UInt32 iteration = 0; iteration < 4; ++iteration)
		{
			Scene runScene;
			auto runApi = std::make_unique<ManagedSceneApi>(runScene);
			const UInt64 runDomain = Take(host->CreateScriptSession(false, runApi.get()));
			Expect(!host->LoadScriptContent(runDomain, Path("prj:Managed/Invalid.json")),
			       "Content fingerprint mismatch was accepted");
			Expect(!host->LoadScriptContent(runDomain, Path("prj:Managed/Editor.json")),
			       "Player session accepted Editor content");
			Done(host->LoadScriptContent(runDomain, sessionContentPath));
			Done(host->ValidateScriptSession(runDomain));
			ManagedScriptRuntime runRuntime(runScene, *host, *runApi, runDomain);
			auto runObject = Take(runScene.CreateGameObject("Run"));
			auto scriptId =
			    Take(runRuntime.CreateAndAdd(runObject, "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
			auto* script = static_cast<ScriptBehaviourComponent*>(runScene.GetComponent(scriptId));
			const UInt64 previousToken = script->InstanceToken();
			runScene.BeginFrame();
			Done(runScene.Update(frame));
			Expect(Take(runObject->Name()) == "Session:1:7", "Run reused previous session static state");
			auto fieldPayload = Take(host->SerializeBehaviour(previousToken));
			Expect(fieldPayload.find(R"("position":[1,2,3])") != String::npos,
			       "Managed vector fields disagree with the native Inspector array representation");
			Done(host->SelectScriptSession(editDomain));
			Expect(!host->InvokeBehaviour(previousToken, ManagedBehaviourEvent::Update),
			       "A Behaviour token was accepted in a different script session");
			Done(host->SelectScriptSession(runDomain));
			auto spawningOwner = Take(runScene.CreateGameObject("Spawner"));
			Take(runRuntime.CreateAndAdd(spawningOwner, "Ursa.SessionFixtures.SpawnerProbe, Ursa.SessionFixtures"));
			Done(runScene.Update(frame));
			Expect(Take(spawningOwner->Name()) == "Spawned:Bound:Awake",
			       "Runtime AddComponent did not bind identity before immediate callbacks");
			auto importedOwner = GameObject::Create("Imported");
			auto importedScript = Take(importedOwner->AddComponent<ScriptBehaviourComponent>(
			    "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
			importedScript->SetSerializedPayload(
			    R"({"FormatVersion":1,"ScriptType":"test.session-probe","Version":1,"Fields":{"value":13}})");
			Done(runScene.AttachGameObject(importedOwner));
			Expect(Take(importedOwner->Name()) == "Session:2:13",
			       "Runtime graph attachment did not restore script fields before immediate Awake");
			auto renderOwner = Take(runScene.CreateGameObject("Managed Render Components"));
			Take(runRuntime.CreateAndAdd(renderOwner,
			                             "Ursa.SessionFixtures.ManagedRenderComponentProbe, Ursa.SessionFixtures"));
			auto* managedCamera = runScene.FindComponent<CameraComponent>(renderOwner);
			auto* managedBox = runScene.FindComponent<BoxRenderComponent>(renderOwner);
			const std::string renderComponentFailure =
			    "C# built-in component wrappers did not mutate native render components: " +
			    ToStdString(Take(renderOwner->Name())) + " (" + std::to_string(renderOwner->Components().size()) +
			    " native components)";
			Expect(managedCamera && std::abs(managedCamera->verticalFovRadians - 1.1f) < 0.0001f &&
			           managedCamera->nearPlane == 0.2f && managedCamera->farPlane == 80.0f && managedBox &&
			           managedBox->color.red == 0.25f && managedBox->color.green == 0.5f &&
			           managedBox->color.blue == 0.75f,
			       renderComponentFailure.c_str());
			auto destroyer = Take(runScene.CreateGameObject("Destroyer"));
			Take(runRuntime.CreateAndAdd(destroyer, "Ursa.SessionFixtures.DestroyerProbe, Ursa.SessionFixtures"));
			Done(runScene.Update(frame));
			Expect(destroyer->Children().empty(), "Managed Destroy did not complete at the frame boundary");
			runScene.Clear();
			Done(host->CloseScriptSession(runDomain));
			Expect(!host->SelectScriptSession(runDomain), "A closed project context could be selected");
			Done(host->SelectScriptSession(editDomain));
			Expect(!host->InvokeBehaviour(previousToken, ManagedBehaviourEvent::Update),
			       "Stop retained an old managed Behaviour token");
			Expect(Take(editObject->Name()) == "Edit", "Run altered the editing session");
		}
		editScene.Clear();
		Done(host->CloseScriptSession(editDomain));

		Scene abandonedScene;
		ManagedSceneApi abandonedApi(abandonedScene);
		const UInt64 abandonedDomain = Take(host->CreateScriptSession(false, &abandonedApi));
		Done(host->LoadScriptContent(abandonedDomain, sessionContentPath));
		{
			ManagedScriptRuntime abandonedRuntime(abandonedScene, *host, abandonedApi, abandonedDomain);
			auto abandonedOwner = Take(abandonedScene.CreateGameObject("Abandoned"));
			Take(abandonedRuntime.CreateAndAdd(abandonedOwner,
			                                   "Ursa.SessionFixtures.SessionProbe, Ursa.SessionFixtures"));
			abandonedScene.BeginFrame();
			Done(abandonedScene.Update(frame));
		}
		Expect(abandonedScene.RootGameObjects().empty(),
		       "Runtime destruction left Scene components pointing to a destroyed dispatcher");
		Done(host->CloseScriptSession(abandonedDomain));

		Scene leakingScene;
		auto leakingApi = std::make_unique<ManagedSceneApi>(leakingScene);
		const UInt64 leakingDomain = Take(host->CreateScriptSession(false, leakingApi.get()));
		Done(host->LoadScriptContent(leakingDomain, sessionContentPath));
		Done(host->ValidateScriptSession(leakingDomain));
		ManagedScriptRuntime leakingRuntime(leakingScene, *host, *leakingApi, leakingDomain);
		auto leakingObject = Take(leakingScene.CreateGameObject("Leak"));
		Take(leakingRuntime.CreateAndAdd(leakingObject, "Ursa.SessionFixtures.LeakingProbe, Ursa.SessionFixtures"));
		leakingScene.BeginFrame();
		Done(leakingScene.Update(frame));
		leakingScene.Clear();
		Expect(!host->CloseScriptSession(leakingDomain) && host->IsSessionFaulted(),
		       "A strong reference retained by a process event did not fault the host after bounded checks");
		Expect(!host->CreateScriptSession(false), "Unload failure did not prevent another project session");

		auto startedAgain = host->Start(filesystem, files, {});
		Expect(!startedAgain, "The process initialized CoreCLR more than once");
		auto secondHost = Take(DotNetHost::Create());
		auto secondStart = secondHost->Start(filesystem, files, {});
		Expect(!secondStart, "A second DotNetHost instance initialized CoreCLR in the same process");
		// CoreCLR retains the bootstrap assembly for the process lifetime. Keep this fixture in build/temp
		// instead of trying to delete assembly files that are still loaded by the current process.
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
