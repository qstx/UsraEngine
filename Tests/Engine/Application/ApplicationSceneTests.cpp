#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Application/Application.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
class Probe final : public Ursa::Component
{
public:
	Probe(int& updateCount, int& removalCount, bool shouldFail)
	    : updates(updateCount), removals(removalCount), fail(shouldFail)
	{
	}

	Ursa::Result<void> Update(const Ursa::FrameContext& context) override
	{
		++updates;
		if (!context.window || context.frameIndex != static_cast<Ursa::UInt64>(updates))
		{
			return Ursa::Result<void>::Failure("Frame context was not populated");
		}
		if (updates == 2 && !fail)
		{
			context.window->Minimize();
			context.window->Restore();
		}
		if (fail)
		{
			return Ursa::Result<void>::Failure("Intentional component failure");
		}
		return {};
	}

	void OnRemove() override
	{
		++removals;
	}

private:
	int& updates;
	int& removals;
	bool fail;
};

class SceneApp final : public Ursa::Application
{
public:
	explicit SceneApp(bool shouldFail) : fail(shouldFail)
	{
	}

	int updates = 0;
	int removals = 0;
	int legacyFrames = 0;
	int stops = 0;

	bool Cleaned() const
	{
		return !window && !device && !swapchain;
	}

private:
	Ursa::Result<void> OnStart() override
	{
		if (!window || !device || !swapchain)
		{
			return Ursa::Result<void>::Failure("Application runtime resources were not retained before OnStart");
		}
		scene = std::make_shared<Ursa::Scene>();
		auto node = scene->CreateGameObject();
		if (!node)
		{
			return Ursa::Result<void>::Failure(node.error);
		}
		auto component = scene->AddComponent<Probe>(node.value, updates, removals, fail);
		if (!component)
		{
			return Ursa::Result<void>::Failure(component.error);
		}
		auto cameraNode = scene->CreateGameObject();
		if (!cameraNode)
		{
			return Ursa::Result<void>::Failure(cameraNode.error);
		}
		Ursa::LocalTransform cameraTransform;
		cameraTransform.position = {0, 0, 4};
		auto transformed = scene->SetLocalTransform(cameraNode.value, cameraTransform);
		if (!transformed)
		{
			return transformed;
		}
		auto camera = scene->AddComponent<Ursa::CameraComponent>(cameraNode.value);
		if (!camera)
		{
			return Ursa::Result<void>::Failure(camera.error);
		}
		auto selected = scene->SetActiveCamera(camera.value);
		if (!selected)
		{
			return selected;
		}
		return {};
	}

	Ursa::Result<void> OnFrame(Ursa::Float32) override
	{
		++legacyFrames;
		return {};
	}

	void OnStop() override
	{
		++stops;
	}

	bool fail;
};

class LegacyApp final : public Ursa::Application
{
public:
	int frames = 0;

private:
	Ursa::Result<void> OnStart() override
	{
		return {};
	}

	Ursa::Result<void> OnFrame(Ursa::Float32) override
	{
		++frames;
		return {};
	}
};

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		SceneApp normal(false);
		const auto normalResult = normal.Run(640, 480, "Scene application test", false, 4);
		Expect(static_cast<bool>(normalResult),
		       ("Scene application failed: " + (normalResult ? Ursa::String{} : normalResult.error)).c_str());
		Expect(normal.updates == 4 && normal.removals == 1 && normal.legacyFrames == 4,
		       "Normal scene lifecycle or legacy callback path was wrong");
		SceneApp failing(true);
		const auto failed = failing.Run(640, 480, "Scene failure test", false, 4);
		Expect(!failed && failed.error.find("Intentional component failure") != Ursa::String::npos,
		       "Component failure was not returned by Application");
		Expect(failing.updates == 1 && failing.removals == 1, "Failure cleanup did not run once");
		auto original = Ursa::OpenEngineFileSystem();
		Expect(static_cast<bool>(original), "Cannot read configured runtime files");
		Ursa::VirtualFileSystem configured;
		auto settings = std::make_shared<Ursa::MemoryFileProvider>();
		Expect(static_cast<bool>(configured.MountProvider(Ursa::projectScheme, settings, Ursa::MountAccess::ReadWrite)),
		       "Cannot mount pipeline selection fixture");
		for (const auto& provider : original.value.Providers())
		{
			Expect(static_cast<bool>(configured.MountProvider(provider.scheme, provider.provider, provider.access)),
			       "Cannot retain runtime resource providers");
		}
		auto engineFiles = Ursa::OpenEngineFileSystem(Ursa::ResolveEngineSourceRoot().value);
		Expect(static_cast<bool>(engineFiles), "Cannot mount builtin source shaders for the native Forward fixture");
		for (const auto& provider : engineFiles.value.Providers())
		{
			if (!configured.IsMounted(provider.scheme))
			{
				Expect(static_cast<bool>(configured.MountProvider(provider.scheme, provider.provider, provider.access)),
				       "Cannot mount builtin shader resources for native Forward");
			}
		}
		auto runtimeProject = std::make_shared<Ursa::MemoryFileProvider>();
		Expect(static_cast<bool>(
		           configured.MountProvider(Ursa::projectScheme, runtimeProject, Ursa::MountAccess::ReadWrite)),
		       "Cannot mount runtime configuration content");
		const auto settingsPath = Ursa::VirtualPath::Parse(Ursa::Rendering::graphicsSettingsRuntime).value;
		const auto defaultSettings = Ursa::Rendering::EncodeGraphicsSettings({});
		Expect(static_cast<bool>(defaultSettings) &&
		           static_cast<bool>(configured.WriteAtomic(settingsPath, defaultSettings.value)),
		       "Cannot prepare the native default Graphics settings");
		const auto pipeline = Ursa::ResourceId::Generate();
		const auto encoded = Ursa::Rendering::EncodeGraphicsSettings({{pipeline, 1}});
		Expect(static_cast<bool>(encoded) &&
		           static_cast<bool>(configured.WriteAtomic(
		               Ursa::VirtualPath::Parse(Ursa::Rendering::graphicsSettingsRuntime).value, encoded.value)),
		       "Cannot create pipeline selection fixture");
		Ursa::ConfigureEngineFileSystem(std::make_shared<Ursa::VirtualFileSystem>(configured));
		SceneApp unsupported(false);
		const auto unavailable = unsupported.Run(640, 480, "Unavailable managed pipeline", false, 1);
		Ursa::ConfigureEngineFileSystem(std::make_shared<Ursa::VirtualFileSystem>(original.value));
		Expect(!unavailable && unavailable.error.find(pipeline.ToString()) != Ursa::String::npos &&
		           unavailable.error.find("managed render host") != Ursa::String::npos && unsupported.updates == 0 &&
		           unsupported.removals == 1 && unsupported.stops == 1 && unsupported.Cleaned(),
		       "A native app silently replaced the project pipeline or failed initialization cleanup");
		LegacyApp legacy;
		const auto legacyResult = legacy.Run(640, 480, "Legacy application test", false, 3);
		Expect(static_cast<bool>(legacyResult) && legacy.frames == 3,
		       "Application without a scene did not use the legacy frame callback");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
