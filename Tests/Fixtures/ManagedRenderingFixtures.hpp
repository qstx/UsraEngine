#pragma once

#include <Core/StringInterop.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <stdexcept>

namespace Ursa::Test
{
// Tests execute the production SRP; this fixture contains no rendering algorithm.
inline Result<std::unique_ptr<ResourceSession>> OpenDefaultPipelineResources(const VirtualFileSystem& files)
{
	return ResourceSession::Open(files,
	                             [](ResourceId guid, UInt64)
	                             {
		                             if (guid == Rendering::DefaultRenderPipelineReference().guid)
		                             {
			                             return VirtualPath::Parse("ursa:Rendering/DefaultUniversalPipeline.ursaasset");
		                             }
		                             if (guid == ResourceId::Parse("c51366e1-e954-4af7-b614-4a8b20b0aa16").value)
		                             {
			                             return VirtualPath::Parse("ursa:Rendering/DefaultUniversalRenderer.ursaasset");
		                             }
		                             return Result<VirtualPath>::Failure("Unknown default pipeline fixture asset: " +
		                                                                 guid.ToString());
	                             });
}

class ManagedSceneRenderer final
{
public:
	static Result<std::unique_ptr<ManagedSceneRenderer>>
	Create(Rhi::DevicePtr device, Scripting::DotNetHost* endpoint = nullptr, const VirtualPath& content = {})
	{
		try
		{
			auto result = std::unique_ptr<ManagedSceneRenderer>(new ManagedSceneRenderer());
			result->device = std::move(device);
			result->files = Require(OpenEngineFileSystem());
			if (!result->files.IsMounted("user"))
			{
				const auto cache = Require(ResolveUserDataDirectory("UrsaManagedRenderTests"));
				std::filesystem::create_directories(cache);
				Require(result->files.Mount("user", cache, MountAccess::ReadWrite));
			}
			result->resources = Require(OpenDefaultPipelineResources(result->files));
			result->content = content;
			result->host = endpoint;
			if (endpoint)
			{
				return Result<std::unique_ptr<ManagedSceneRenderer>>::Success(std::move(result));
			}
			result->ownedHost = Require(Scripting::DotNetHost::Create());
			result->host = result->ownedHost.get();
			Scripting::ManagedHostFiles framework;
			framework.runtimeConfig = Require(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.runtimeconfig.json"));
			framework.entryAssembly = Require(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.dll"));
			for (const String name :
			     {"Ursa.ManagedHost.deps.json", "Ursa.Runtime.dll", "Ursa.Rendering.dll", "Ursa.Bindings.dll"})
			{
				framework.dependencies.push_back(Require(VirtualPath::Parse("ursa:Managed/" + name)));
			}
			Require(result->host->Connect(result->files, framework, [](UInt32, const String&) {}));
			return Result<std::unique_ptr<ManagedSceneRenderer>>::Success(std::move(result));
		}
		catch (const std::exception& error)
		{
			return Result<std::unique_ptr<ManagedSceneRenderer>>::Failure(ToUrsaString(error.what()));
		}
	}

	Result<void> RenderOffscreen(Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                             Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead,
	                             std::optional<Rendering::CameraType> filter = {},
	                             std::optional<Rendering::CameraData> camera = {})
	{
		Scripting::ManagedSceneApi api(scene, resources.get());
		api.SetRenderDevice(device);
		api.SetRenderOutput(target, finalState);
		api.SetRenderCameraSelection(filter, std::move(camera));
		auto created = host->CreateScriptSession(false, &api);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		const auto session = created.value;
		Result<void> rendered;
		if (!content.scheme.empty())
		{
			rendered = host->LoadScriptContent(session, content);
		}
		if (rendered)
		{
			rendered = host->ConfigureRenderPipeline(session, {});
		}
		if (rendered)
		{
			rendered =
			    host->RenderScene(session, Float32(target->description.width) / Float32(target->description.height));
		}
		const auto closed = host->CloseScriptSession(session);
		return rendered ? closed : rendered;
	}

private:
	template <class T> static T Require(Result<T> result)
	{
		if (!result)
		{
			throw std::runtime_error(ToStdString(result.error));
		}
		return std::move(result.value);
	}

	static void Require(Result<void> result)
	{
		if (!result)
		{
			throw std::runtime_error(ToStdString(result.error));
		}
	}

	Rhi::DevicePtr device;
	VirtualFileSystem files;
	std::unique_ptr<ResourceSession> resources;
	std::unique_ptr<Scripting::DotNetHost> ownedHost;
	Scripting::DotNetHost* host = nullptr;
	VirtualPath content;
};
} // namespace Ursa::Test
