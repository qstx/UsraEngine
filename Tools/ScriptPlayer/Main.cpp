#include <Application/Application.hpp>
#include <Core/Log.hpp>
#include <Core/Profiler.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/PakArchive.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Gui/GuiCanvas.hpp>
#include <Gui/GuiInput.hpp>
#include <Application/RuntimeSceneManager.hpp>
#include <Application/FrameReplay.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

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

class ScriptPlayer final : public Application
{
public:
	std::unique_ptr<FrameReplay> replay;

	Result<void> FinishReplay()
	{
		return replay ? replay->Finish() : Result<void>{};
	}

	~ScriptPlayer() override
	{
		if (scene)
		{
			scene->Clear();
		}
		OnStop();
	}

	explicit ScriptPlayer(std::shared_ptr<VirtualFileSystem> files, String scenePath, String manifestPath,
	                      bool stressWindow)
	    : files(std::move(files)), scenePath(std::move(scenePath)), manifestPath(std::move(manifestPath)),
	      stressWindow(stressWindow)
	{
	}

private:
	Result<void> OnStart() override
	{
		URSA_LOG_INFO("RHI", "Vulkan adapter: " + device->AdapterInfo().name);
		auto builds = Take(ReadSceneBuildSettings(*files, {}, true));
		if (scenePath.empty())
		{
			if (!builds.configured || builds.scenes.empty())
			{
				return Result<void>::Failure(
				    "Player has no startup Scene. Configure enabled scenes in ProjectSettings/BuildSettings.json and "
				    "reimport, or pass --scene <asset path>.");
			}
			scenePath = builds.scenes.front().path.relative;
		}
		if (scenePath.rfind("prj:", 0) == 0)
		{
			scenePath.erase(0, 4);
		}
		if (scenePath.rfind("Assets/", 0) == 0)
		{
			scenePath.erase(0, 7);
		}
		if (const auto configured = builds.Resolve(scenePath); configured)
		{
			scenePath = configured.value.path.relative;
		}
		resources = Take(ResourceSession::Open(*files));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		ComponentLoadContext context(*resources);
		auto sceneVirtualPath = Take(VirtualPath::Parse(String("prj:") + scenePath));
		auto document = Take(ReadJsonResource(*files, sceneVirtualPath, ResourceKind::Scene));
		if (document.contains("instances"))
		{
			auto loaded = Take(LoadLinkedSceneResource(*files, sceneVirtualPath, registry, context));
			linkedScene = std::make_unique<LinkedSceneResource>(std::move(loaded));
			scene = std::shared_ptr<Scene>(linkedScene->scene.get(), [](Scene*) {});
		}
		else
		{
			auto loaded = Take(DecodeSceneDocument(sceneVirtualPath.ToString(), document, registry, context));
			scene = std::shared_ptr<Scene>(std::move(loaded.scene));
		}
		sceneApi = std::make_unique<ManagedSceneApi>(*scene, resources.get(),
		                                             [this](const String& title)
		                                             {
			                                             if (window)
			                                             {
				                                             window->SetTitle(title);
			                                             }
		                                             });
		sceneApi->SetRenderDevice(device);
		sceneApi->SetRenderWindow(swapchain);
		host = Take(DotNetHost::Create());
		ManagedHostFiles bootstrap;
		bootstrap.runtimeConfig = Path("ursa:Managed/Ursa.ManagedHost.runtimeconfig.json");
		bootstrap.entryAssembly = Path("ursa:Managed/Ursa.ManagedHost.dll");
		bootstrap.dependencies = {Path("ursa:Managed/Ursa.ManagedHost.deps.json"),
		                          Path("ursa:Managed/Ursa.Runtime.dll"), Path("ursa:Managed/Ursa.Rendering.dll"),
		                          Path("ursa:Managed/Ursa.Bindings.dll")};
		Done(host->Start(*files, bootstrap,
		                 [](UInt32 level, const String& message)
		                 {
			                 if (level >= 4)
			                 {
				                 URSA_LOG_ERROR("Script", message);
			                 }
			                 else
			                 {
				                 URSA_LOG_INFO("Script", message);
			                 }
		                 }));
		domain = Take(host->CreateScriptSession(false, sceneApi.get()));
		Done(host->LoadScriptContent(domain, Take(VirtualPath::Parse(manifestPath))));
		const auto graphics = Take(Rendering::ReadGraphicsSettings(*files, true));
		Done(host->ConfigureRenderPipeline(domain, graphics.renderPipeline));
		runtime = std::make_unique<ManagedScriptRuntime>(*scene, *host, *sceneApi, domain);
		Done(runtime->AttachSerializedComponents());
		auto configuredScene = builds.Resolve(scenePath);
		BuildScene initial;
		if (configuredScene)
		{
			initial = std::move(configuredScene.value);
		}
		else
		{
			const auto path = Take(VirtualPath::Parse("prj:" + scenePath));
			const auto header = Take(ReadResourceHeader(*files, path));
			String name = path.FileName();
			if (name.size() > 10)
			{
				name.resize(name.size() - 10);
			}
			initial = {{header.id, 0}, name, path, -1};
		}
		sceneManager = std::make_unique<RuntimeSceneManager>(*scene, *resources, std::move(builds), std::move(initial));
		sceneApi->SetSceneManager(sceneManager.get());
		sceneManager->validateCandidate = [this](Scene& candidate) { return runtime->ValidateCandidate(candidate); };
		sceneManager->invalidateOldObjects = [this]() { return sceneApi->InvalidateSceneObjects(); };
		sceneManager->notify = [this](const RuntimeScene& info, bool loaded)
		{ return host->NotifySceneChange(domain, info, sceneApi->SessionIdentity(), loaded); };
		sceneManager->cancelInput = [this]()
		{
			gui.Reset();
			guiInput.Cancel();
			if (guiCanvas)
			{
				guiCanvas->Discard();
			}
			FrameRunner().ResetAccumulator();
			FrameRunner().SynchronizeInput(FrameInput::Capture(*window));
		};
		guiCanvas = Take(Gui::Canvas::Create(device, *files));
		gui.measureText = [canvas = std::weak_ptr(guiCanvas)](const String& text)
		{
			auto owner = canvas.lock();
			if (!owner)
			{
				throw std::logic_error("Runtime GUI font session is closed");
			}
			return owner->MeasureText(text);
		};
		Done(guiInput.Attach(window));
		gui.readClipboard = [this]() { return window->ClipboardText(); };
		gui.writeClipboard = [this](const String& value) { window->SetClipboardText(value); };
		return {};
	}

	Result<void> OnSceneFrame(Float32 elapsed) override
	{
		auto result = RunSceneFrame(elapsed);
		if (!result || !replay)
		{
			return result;
		}
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		auto graph = Take(EncodeSceneDocument(sceneManager->ActiveScene().asset.path.ToString(),
		                                      sceneManager->ActiveScene().asset.reference.guid, *scene, registry));
		nlohmann::json state = {{"scene", ToStdString(sceneManager->ActiveScene().asset.name)},
		                        {"buildIndex", sceneManager->ActiveScene().asset.buildIndex},
		                        {"objects", graph["gameObjects"].size()},
		                        {"hasLoadError", !sceneManager->LastLoadError().empty()},
		                        {"hasTextFocus", gui.HasTextFocus()},
		                        {"hasPointerCapture", gui.HasPointerCapture()},
		                        {"token", sceneManager->ActiveScene().token},
		                        {"behaviours", nlohmann::json::object()}};
		for (const auto& object : graph["gameObjects"])
		{
			for (const auto& component : object["components"])
			{
				if (component["type"] == "ursa.ScriptBehaviour")
				{
					state["behaviours"][component["data"]["typeName"].get<std::string>()] =
					    component["data"]["scriptPayload"]["Fields"];
				}
			}
		}
		return replay->Verify(frame, state);
	}

	Result<void> RunSceneFrame(Float32 elapsed)
	{
		const auto beforeLoad = FrameRunner().CurrentContext(window.get(), FrameInput::Capture(*window));
		if (!sceneInitialized)
		{
			Done(sceneManager->Initialize(beforeLoad));
			sceneInitialized = true;
		}
		else if (sceneManager->IsLoading())
		{
			auto activated = sceneManager->ActivatePending(beforeLoad);
			RestartFrameClock();
			if (!activated)
			{
				return Result<void>::Failure(activated.error);
			}
			if (activated.value)
			{
				elapsed = 0;
			}
		}
		auto events = guiInput.Drain();
		if (guiInput.TakeFocusLost())
		{
			gui.CancelInput();
			FrameRunner().SynchronizeInput(FrameInput::Capture(*window));
		}
		auto input = FrameInput::Capture(*window);
		const Gui::Point viewport{Float32(window->FramebufferWidth()), Float32(window->FramebufferHeight())};
		if (viewport.x <= 0 || viewport.y <= 0 || !scene->CollectRenderData(viewport.x / viewport.y))
		{
			gui.CancelInput();
			guiCanvas->Discard();
			return FrameRunner().Tick(*scene, elapsed, window.get(), input);
		}
		const Float32 scaleX = viewport.x / Float32(std::max<UInt32>(1, window->Width()));
		const Float32 scaleY = viewport.y / Float32(std::max<UInt32>(1, window->Height()));
		for (auto& event : events)
		{
			event.mousePosition.x *= scaleX;
			event.mousePosition.y *= scaleY;
			if (event.type != Gui::EventType::ScrollWheel)
			{
				event.delta.x *= scaleX;
				event.delta.y *= scaleY;
			}
		}
		input.mouseX *= scaleX;
		input.mouseY *= scaleY;
		Done(FrameRunner().TickGui(*scene, elapsed, window.get(), input, gui, events, viewport));
		return guiCanvas->Build(gui, viewport);
	}

	Result<void> OnRender(Rendering::FrameCapture* capture) override
	{
		const auto width = window->FramebufferWidth();
		const auto height = window->FramebufferHeight();
		if (!width || !height)
		{
			return {};
		}
		sceneApi->SetFrameCapture(capture);
		sceneApi->SetGameGuiOutput(++guiSequence, guiCanvas);
		auto result = host->RenderScene(domain, Float32(width) / Float32(height));
		// The surface can become suspended after the size sample but before image acquisition.
		if (!result &&
		    result.error.find("Window render target is suspended while its framebuffer is zero sized") != String::npos)
		{
			return {};
		}
		return result;
	}

	Result<void> InitializeRenderPipeline() override
	{
		return {};
	}

	void OnSuspended() override
	{
		guiInput.Cancel();
		gui.CancelInput();
		if (guiCanvas)
		{
			guiCanvas->Discard();
		}
		FrameRunner().SynchronizeInput(FrameInput::Capture(*window));
	}

	void OnStop() override
	{
		if (sceneManager)
		{
			sceneManager->Close();
		}
		if (sceneApi)
		{
			sceneApi->SetSceneManager(nullptr);
		}
		sceneManager.reset();
		sceneInitialized = false;
		guiInput.Detach();
		gui.Reset();
		guiCanvas.reset();
		runtime.reset();
		if (host && domain)
		{
			auto closed = host->CloseScriptSession(domain);
			if (!closed)
			{
				URSA_LOG_ERROR("Script", closed.error);
			}
			domain = 0;
		}
		sceneApi.reset();
		resources.reset();
	}

	Result<void> OnFrame(Float32) override
	{
		++frame;
		if (replay)
		{
			Done(replay->Feed(frame, *window));
		}
		if (stressWindow && frame == 5)
		{
			window->Resize(800, 600);
		}
		if (stressWindow && frame == 20)
		{
			window->Resize(960, 720);
		}
		if (stressWindow && frame == 35)
		{
			window->Resize(1280, 720);
		}
		if (stressWindow && frame == 50)
		{
			window->Resize(1024, 768);
		}
		if (stressWindow && frame == 10)
		{
			window->Minimize();
			window->Restore();
		}
		return {};
	}

	std::shared_ptr<VirtualFileSystem> files;
	Gui::Session gui;
	Gui::InputQueue guiInput;
	std::shared_ptr<Gui::Canvas> guiCanvas;
	UInt64 guiSequence = 0;
	String scenePath;
	String manifestPath;
	bool stressWindow = false;
	UInt32 frame = 0;
	std::unique_ptr<LinkedSceneResource> linkedScene;
	std::unique_ptr<ResourceSession> resources;
	std::unique_ptr<DotNetHost> host;
	std::unique_ptr<ManagedSceneApi> sceneApi;
	std::unique_ptr<ManagedScriptRuntime> runtime;
	std::unique_ptr<RuntimeSceneManager> sceneManager;
	bool sceneInitialized = false;
	UInt64 domain = 0;
};
} // namespace

int main(int argc, char** argv)
{
	URSA_PROFILE_APP("UrsaScriptPlayer");
	LogSession logging;
	try
	{
		bool visible = true;
		UInt32 frames = 0;
		std::filesystem::path captureOutput;
		UInt32 captureFrame = 1;
		std::filesystem::path content = CurrentExecutablePath().parent_path() / "Content";
		std::filesystem::path project;
		std::filesystem::path engineAssets;
		std::filesystem::path package;
		String scenePath;
		std::filesystem::path replayPath;
		bool stressWindow = false;
		for (int index = 1; index < argc; ++index)
		{
			const String argument = argv[index];
			if (argument == "--hidden")
			{
				visible = false;
			}
			else if (argument == "--replay" && index + 1 < argc)
			{
				replayPath = std::filesystem::u8path(argv[++index]);
			}
			else if (argument == "--frames" && index + 1 < argc)
			{
				frames = static_cast<UInt32>(std::stoul(argv[++index]));
			}
			else if (argument == "--pak" && index + 1 < argc)
			{
				package = std::filesystem::absolute(std::filesystem::u8path(argv[++index]));
			}
			else if (argument == "--capture" && index + 1 < argc)
			{
				captureOutput = std::filesystem::u8path(argv[++index]);
			}
			else if (argument == "--capture-frame" && index + 1 < argc)
			{
				const std::string text = argv[++index];
				std::size_t end = 0;
				const auto value = std::stoull(text, &end);
				if (!value || value > UINT32_MAX || end != text.size())
				{
					throw std::runtime_error("--capture-frame must be between 1 and 4294967295");
				}
				captureFrame = static_cast<UInt32>(value);
			}
			else if (argument == "--content" && index + 1 < argc)
			{
				content = std::filesystem::absolute(std::filesystem::u8path(argv[++index]));
			}
			else if (argument == "--project" && index + 1 < argc)
			{
				project = std::filesystem::absolute(std::filesystem::u8path(argv[++index]));
			}
			else if (argument == "--engine" && index + 1 < argc)
			{
				engineAssets = std::filesystem::absolute(std::filesystem::u8path(argv[++index]));
			}
			else if (argument == "--scene" && index + 1 < argc)
			{
				scenePath = ToUrsaString(std::filesystem::u8path(argv[++index]).generic_string());
			}
			else if (argument == "--stress-window")
			{
				stressWindow = true;
			}
			else
			{
				throw std::runtime_error("Usage: UrsaScriptPlayer [--project <directory> | --content <directory> | "
				                         "--pak <Content.pak>] [--engine <Engine/Assets>] [--scene <asset path>] "
				                         "[--hidden] [--frames N] [--capture <output.png>] [--capture-frame N]");
			}
		}
		if (!package.empty() && !project.empty())
		{
			throw std::runtime_error("Choose either --project or --pak");
		}
		RuntimeMountProfileConfig profile;
		profile.applicationId = "UrsaScriptPlayer";
		profile.mode = package.empty() ? RuntimeMountMode::LooseRuntime : RuntimeMountMode::PackagedRuntime;
		String manifestPath = "prj:__UrsaManaged/Game.json";
		if (package.empty())
		{
			if (project.empty())
			{
				profile.engineDirectories = {content / "Engine"};
				profile.projectAssetsDirectory = content / "Project";
			}
			else
			{
				if (engineAssets.empty())
				{
					const auto adjacentEngine = project.parent_path() / "Engine" / "Assets";
					engineAssets = std::filesystem::is_directory(adjacentEngine)
					                   ? adjacentEngine
					                   : std::filesystem::path(URSA_SCRIPT_PLAYER_ENGINE_ASSETS);
				}
				VirtualFileSystem sources;
				Done(sources.Mount(engineScheme, Take(ResolveEngineSourceRoot(engineAssets)), MountAccess::ReadOnly));
				Done(sources.Mount(projectScheme, project / "Assets", MountAccess::ReadOnly));
				if (std::filesystem::is_directory(project / "ProjectSettings"))
				{
					Done(sources.Mount(projectSettingsScheme, project / "ProjectSettings", MountAccess::ReadOnly));
				}
				auto libraryMount = sources.Mount(editorLibraryScheme, project / "Library", MountAccess::ReadOnly);
				if (!libraryMount)
				{
					throw std::runtime_error(
					    "Project Library is missing; run UrsaAssetImport --project <project> before launching Player");
				}
				Done(sources.Mount(EngineFrameworkScheme(EngineBuildConfiguration()), EngineFrameworkDirectory(),
				                   MountAccess::ReadOnly));
				const auto ready =
				    Take(ReadRuntimeContent(sources, EngineBuildConfiguration(), "Player", "WindowsVulkan", &sources));
				const auto runtimeView =
				    Take(OpenRuntimeContentView(std::make_shared<VirtualFileSystem>(sources), ready));
				profile.mode = RuntimeMountMode::PackagedRuntime;
				for (const auto& provider : runtimeView.Providers())
				{
					profile.packagedProviders.push_back({provider.scheme, provider.provider});
				}
			}
		}
		else
		{
			auto archive = Take(PakArchive::Open(package));
			profile.packagedProviders = {{engineScheme, Take(archive->View(engineScheme))},
			                             {projectScheme, Take(archive->View(projectScheme))}};
		}
		auto files = std::make_shared<VirtualFileSystem>(Take(OpenRuntimeFileSystem(profile)));
		ConfigureEngineFileSystem(files);
		ScriptPlayer player(files, scenePath, manifestPath, stressWindow);
		if (!replayPath.empty())
		{
			player.replay = Take(FrameReplay::Open(replayPath));
		}
		if (!captureOutput.empty())
		{
			Done(player.SetFrameCapture(captureOutput, captureFrame));
		}
		Done(player.Run(1024, 720, "Ursa Script Player", visible, frames));
		Done(player.FinishReplay());
		return 0;
	}
	catch (const std::exception& error)
	{
		URSA_LOG_ERROR("Player", ToUrsaString(error.what()));
		return 1;
	}
}
