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
		host = Take(DotNetHost::Create());
		ManagedHostFiles bootstrap;
		bootstrap.runtimeConfig = Path("ursa:Managed/Ursa.ManagedHost.runtimeconfig.json");
		bootstrap.entryAssembly = Path("ursa:Managed/Ursa.ManagedHost.dll");
		bootstrap.dependencies = {Path("ursa:Managed/Ursa.ManagedHost.deps.json"),
		                          Path("ursa:Managed/Ursa.Runtime.dll"), Path("ursa:Managed/Ursa.Rendering.dll")};
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
		runtime = std::make_unique<ManagedScriptRuntime>(*scene, *host, *sceneApi, domain);
		return runtime->AttachSerializedComponents();
	}

	void OnStop() override
	{
		runtime.reset();
		sceneApi.reset();
		if (host && domain)
		{
			auto closed = host->CloseScriptSession(domain);
			if (!closed)
			{
				URSA_LOG_ERROR("Script", closed.error);
			}
			domain = 0;
		}
		resources.reset();
	}

	Result<void> OnFrame(Float32) override
	{
		++frame;
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
	String scenePath;
	String manifestPath;
	bool stressWindow = false;
	UInt32 frame = 0;
	std::unique_ptr<LinkedSceneResource> linkedScene;
	std::unique_ptr<ResourceSession> resources;
	std::unique_ptr<DotNetHost> host;
	std::unique_ptr<ManagedSceneApi> sceneApi;
	std::unique_ptr<ManagedScriptRuntime> runtime;
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
		String scenePath = "ScriptSample/ScriptPlayer.ursascene";
		bool stressWindow = false;
		for (int index = 1; index < argc; ++index)
		{
			const String argument = argv[index];
			if (argument == "--hidden")
			{
				visible = false;
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
				auto libraryMount = sources.Mount(editorLibraryScheme, project / "Library", MountAccess::ReadOnly);
				if (!libraryMount)
				{
					throw std::runtime_error(
					    "Project Library is missing; run UrsaAssetImport --project <project> before launching Player");
				}
				Done(sources.Mount("framework", EngineFrameworkDirectory(), MountAccess::ReadOnly));
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
		if (!captureOutput.empty())
		{
			Done(player.SetFrameCapture(captureOutput, captureFrame));
		}
		Done(player.Run(1024, 720, "Ursa Script Player", visible, frames));
		return 0;
	}
	catch (const std::exception& error)
	{
		URSA_LOG_ERROR("Player", ToUrsaString(error.what()));
		return 1;
	}
}
