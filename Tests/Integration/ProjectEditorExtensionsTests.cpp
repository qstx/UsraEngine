#include <Core/StringInterop.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void ValidateExtensions(const String& serialized)
{
	const auto descriptors = nlohmann::json::parse(ToStdString(serialized));
	Expect(descriptors.is_array(), "Managed Editor extension index is invalid");
	const auto has = [&](const std::string& kind, const std::string& path, const std::string& target)
	{
		for (const auto& descriptor : descriptors)
		{
			if (descriptor.value("Kind", std::string{}) == kind && descriptor.value("Path", std::string{}) == path &&
			    descriptor.value("Target", std::string{}) == target)
			{
				return true;
			}
		}
		return false;
	};
	Expect(has("Inspector", "", "demo.snake-controller") && has("Inspector", "", "demo.snake-board"),
	       "Managed Snake Inspector extensions were not registered");
	Expect(has("Menu", "Tools/Snake/Save Current Scene", "") && has("Window", "Tools/Snake/Controls", ""),
	       "Managed Snake Tools menu or Editor window was not registered");
}
} // namespace

int main()
{
	using namespace Ursa;
	using namespace Ursa::Scripting;
	try
	{
		RuntimeMountProfileConfig profile;
		profile.mode = RuntimeMountMode::Editor;
		profile.applicationId = "UrsaProjectEditorExtensionTests";
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
		auto host = Take(DotNetHost::Create());
		ManagedHostFiles bootstrap;
		bootstrap.runtimeConfig = Take(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.runtimeconfig.json"));
		bootstrap.entryAssembly = Take(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.dll"));
		bootstrap.dependencies = {Take(VirtualPath::Parse("ursa:Managed/Ursa.ManagedHost.deps.json")),
		                          Take(VirtualPath::Parse("ursa:Managed/Ursa.Runtime.dll")),
		                          Take(VirtualPath::Parse("ursa:Managed/Ursa.Rendering.dll"))};
		Done(host->Start(filesystem, bootstrap,
		                 [](UInt32 level, const String& message)
		                 {
			                 if (level >= 4)
			                 {
				                 std::cerr << ToStdString(message) << '\n';
			                 }
		                 }));
		Done(host->LoadEditorApi(Take(VirtualPath::Parse("ursa:Ursa.Editor.dll"))));
		const VirtualPath editorManifest = Take(VirtualPath::Parse("lib:ScriptBuild/Manifests/Editor.json"));
		for (int reload = 0; reload < 2; ++reload)
		{
			const UInt64 session = Take(host->CreateScriptSession(true));
			Done(host->LoadScriptContent(session, editorManifest));
			Done(host->ValidateScriptSession(session));
			ValidateExtensions(Take(host->PrepareEditorExtensions(session)));
			Done(host->CloseScriptSession(session));
		}
		std::cout << "Managed Snake Inspector, menu, window registration, and extension reload passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
