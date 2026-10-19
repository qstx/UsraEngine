#include <Application/PlaySession.hpp>
#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <stdexcept>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

Size CountObjects(const GameObject::Ptr& object)
{
	Size count = 1;
	for (const auto& child : object->Children())
	{
		count += CountObjects(child);
	}
	return count;
}

struct ManagedPlayServices
{
	std::shared_ptr<DotNetHost> host;
	std::unique_ptr<ManagedSceneApi> sceneApi;
	std::unique_ptr<ManagedScriptRuntime> runtime;
	String title;
	UInt64 session = 0;

	~ManagedPlayServices()
	{
		runtime.reset();
		if (host && session)
		{
			host->CloseScriptSession(session);
		}
		sceneApi.reset();
	}
};

} // namespace

int main()
{
	using namespace Ursa;
	using namespace Ursa::Editor;
	using namespace Ursa::Scripting;
	try
	{
		RuntimeMountProfileConfig profile;
		profile.mode = RuntimeMountMode::Editor;
		profile.applicationId = "UrsaSnakePlaySessionTests";
		profile.engineDirectories = Take(ResolveEngineAssetRoots());
		profile.engineDirectories.push_back(std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH).parent_path());
		profile.projectAssetsDirectory = std::filesystem::u8path(URSA_PROJECT_DIR) / "Assets";
		profile.projectLibraryDirectory = std::filesystem::u8path(URSA_PROJECT_DIR) / "Library";
		auto sources = Take(OpenRuntimeFileSystem(profile));
		auto filesystem = Take(OpenEngineFileSystem());
		for (const auto& provider : sources.Providers())
		{
			Done(filesystem.MountProvider(provider.scheme, provider.provider, provider.access));
		}
		ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(filesystem));
		auto resources = Take(ResourceSession::Open(filesystem));
		ComponentRegistry registry;
		Done(RegisterBuiltinComponents(registry));
		const auto scenePath = Take(VirtualPath::Parse("prj:Snake/Snake.ursascene"));
		auto draft = Take(SceneDraft::Load(filesystem, scenePath, ResourceKind::Scene, registry, *resources));
		auto root = draft->GetScene().RootGameObjects().front();
		auto* controller = root->FindComponent<ScriptBehaviourComponent>();
		Expect(controller && controller->TypeName() == "Ursa.Demo.SnakeController, Ursa.Game",
		       "Snake Scene does not contain its managed controller");
		auto payload = nlohmann::json::parse(ToStdString(controller->SerializedPayload()));
		payload["Fields"]["smokeGameplay"] = true;
		controller->SetSerializedPayload(ToUrsaString(payload.dump()));
		const String unsavedPayload = controller->SerializedPayload();
		const ResourceId sceneId = Take(ResourceId::Parse("75727361-0000-0000-0300-000000000002"));

		auto host = std::shared_ptr<DotNetHost>(Take(DotNetHost::Create()).release());
		ManagedHostFiles bootstrap;
		bootstrap.runtimeConfig = Take(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.runtimeconfig.json"));
		bootstrap.entryAssembly = Take(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.dll"));
		bootstrap.dependencies = {Take(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.deps.json")),
		                          Take(VirtualPath::Parse("ursa:Managed/Ursa.Runtime.dll")),
		                          Take(VirtualPath::Parse("ursa:Managed/Ursa.Rendering.dll")),
		                          Take(VirtualPath::Parse("ursa:Managed/Ursa.Bindings.dll"))};
		Done(host->Start(filesystem, bootstrap,
		                 [](UInt32 level, const String& message)
		                 {
			                 if (level >= 4)
			                 {
				                 std::cerr << ToStdString(message) << '\n';
			                 }
		                 }));

		auto services = std::make_shared<ManagedPlayServices>();
		services->host = host;
		const VirtualPath gameManifest = Take(VirtualPath::Parse("lib:ScriptBuild/Manifests/Game.json"));
		PlayContextSetup setup = [services, gameManifest](ComponentLoadContext& context)
		{
			auto session = services->host->CreateScriptSession(false);
			if (!session)
			{
				return Result<std::shared_ptr<void>>::Failure(session.error);
			}
			services->session = session.value;
			auto loaded = services->host->LoadScriptContent(session.value, gameManifest);
			if (!loaded)
			{
				return Result<std::shared_ptr<void>>::Failure(loaded.error);
			}
			auto validated = services->host->ValidateScriptSession(session.value);
			if (!validated)
			{
				return Result<std::shared_ptr<void>>::Failure(validated.error);
			}
			context.SetSceneReadyCallback(
			    [services](Scene& scene, ComponentLoadContext& sceneContext)
			    {
				    services->sceneApi = std::make_unique<ManagedSceneApi>(
				        scene, &sceneContext.Resources(), [services](const String& title) { services->title = title; });
				    auto bound = services->host->BindScriptSession(services->session, *services->sceneApi);
				    if (!bound)
				    {
					    return bound;
				    }
				    services->runtime = std::make_unique<ManagedScriptRuntime>(scene, *services->host,
				                                                               *services->sceneApi, services->session);
				    return services->runtime->AttachSerializedComponents();
			    });
			return Result<std::shared_ptr<void>>::Success(services);
		};

		const Size authoredObjects = CountObjects(root);
		PlaySession play;
		Done(play.Start(*draft, sceneId, registry, *resources, setup));
		const Float32 delta = 1.0f / 60.0f;
		Done(play.Tick(delta, nullptr, {}));
		if (services->title.find("Score: 1") == String::npos)
		{
			throw std::runtime_error("Play mode did not use the unsaved managed smoke field value; title was: " +
			                         ToStdString(services->title));
		}
		Done(play.Tick(delta, nullptr, {}));
		Expect(services->title.find("Paused") != String::npos, "Managed Snake pause feedback was missing");
		Done(play.Tick(delta, nullptr, {}));
		Expect(services->title.find("Score: 1") != String::npos && services->title.find("Paused") == String::npos,
		       "Managed Snake did not resume gameplay");
		Done(play.Tick(delta, nullptr, {}));
		Expect(services->title.find("Game Over") != String::npos, "Managed Snake collision feedback was missing");
		Done(play.Tick(delta, nullptr, {}));
		Expect(services->title.find("Score: 0") != String::npos,
		       "Managed Snake restart did not restore the initial score");
		Expect(CountObjects(play.GetScene()->RootGameObjects().front()) == authoredObjects,
		       "Snake gameplay must reuse authored objects when growing, losing, and restarting");
		play.Stop();
		if (controller->SerializedPayload() != unsavedPayload || draft->GetScene().RootGameObjects().size() != 1)
		{
			throw std::runtime_error("Stopping Play mode changed the unsaved edit Scene (payload=" +
			                         std::to_string(controller->SerializedPayload() == unsavedPayload) +
			                         ", roots=" + std::to_string(draft->GetScene().RootGameObjects().size()) + ")");
		}
		services.reset();
		host.reset();
		std::cout << "Managed Snake Play mode lifecycle and edit Scene restoration passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
