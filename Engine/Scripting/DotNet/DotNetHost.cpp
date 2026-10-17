#include <Core/Types.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Scripting/Bindings/BindingRequirements.hpp>
#include <Runtime.generated.hpp>
#include <Rendering.generated.hpp>
#include <ManagedHost.generated.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <string>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>
#endif

namespace Ursa::Scripting
{
namespace HostBindings = Bindings::ManagedHost;

struct DotNetHost::BindingState
{
	Bindings::CallbackModule<HostBindings::Table> host;
};

#if defined(_WIN32)
namespace
{
std::atomic_bool runtimeStarted = false;

struct CacheFile
{
	VirtualPath source;
	String fileName;
	Vector<UInt8> bytes;
};

void AddFingerprint(UInt64& hash, const void* data, Size size)
{
	const auto* bytes = static_cast<const UInt8*>(data);
	for (Size index = 0; index < size; ++index)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ull;
	}
}

template <class T> T HostFunction(HMODULE library, const char* name)
{
	return reinterpret_cast<T>(GetProcAddress(library, name));
}
} // namespace
#endif

Result<ManagedHostBootstrap> MaterializeManagedHostFiles(VirtualFileSystem& filesystem, const ManagedHostFiles& files)
{
	if (files.runtimeConfig.scheme.empty() || files.entryAssembly.scheme.empty())
	{
		return Result<ManagedHostBootstrap>::Failure("Managed host bootstrap needs a runtimeconfig and entry assembly");
	}
	Vector<VirtualPath> sources{files.runtimeConfig, files.entryAssembly};
	sources.insert(sources.end(), files.dependencies.begin(), files.dependencies.end());
	sources.insert(sources.end(), files.scriptAssemblies.begin(), files.scriptAssemblies.end());
	std::sort(sources.begin(), sources.end(),
	          [](const VirtualPath& left, const VirtualPath& right) { return left.ToString() < right.ToString(); });

	Vector<CacheFile> cacheFiles;
	Set<String> sourcePaths;
	Set<String> fileNames;
	UInt64 totalBytes = 0;
	UInt64 hash = 14695981039346656037ull;
	const String domain = "Ursa.ManagedHostBootstrap.V1";
	AddFingerprint(hash, domain.data(), domain.size());
	for (const VirtualPath& source : sources)
	{
		const String sourceName = source.ToString();
		if (!sourcePaths.insert(sourceName).second)
		{
			return Result<ManagedHostBootstrap>::Failure("Duplicate source in managed host bootstrap: " + sourceName);
		}
		const String fileName = source.FileName();
		if (fileName.empty() || fileName == "." || fileName == ".." || fileName.find('/') != String::npos ||
		    fileName.find('\\') != String::npos || fileName.find('\0') != String::npos ||
		    !fileNames.insert(fileName).second)
		{
			return Result<ManagedHostBootstrap>::Failure("Invalid or duplicate managed bootstrap filename: " +
			                                             sourceName);
		}
		auto bytes = filesystem.Read(source);
		if (!bytes)
		{
			return Result<ManagedHostBootstrap>::Failure(bytes.error);
		}
		constexpr UInt64 maximumBootstrapBytes = 256ull * 1024ull * 1024ull;
		if (bytes.value.size() > maximumBootstrapBytes - totalBytes)
		{
			return Result<ManagedHostBootstrap>::Failure("Managed host bootstrap exceeds its size limit");
		}
		totalBytes += bytes.value.size();
		AddFingerprint(hash, sourceName.data(), sourceName.size());
		const UInt64 byteCount = bytes.value.size();
		AddFingerprint(hash, &byteCount, sizeof(byteCount));
		AddFingerprint(hash, bytes.value.data(), bytes.value.size());
		cacheFiles.push_back({source, fileName, std::move(bytes.value)});
	}

	std::ostringstream fingerprintText;
	fingerprintText << "fnv1a64-" << std::hex << std::setfill('0') << std::setw(16) << hash;
	const String fingerprint = ToUrsaString(fingerprintText.str());
	auto cacheDirectory = VirtualPath::Parse(String("user:ManagedBootstrap/") + fingerprint);
	if (!cacheDirectory)
	{
		return Result<ManagedHostBootstrap>::Failure(cacheDirectory.error);
	}

	for (const CacheFile& file : cacheFiles)
	{
		auto destination = VirtualPath::Parse(cacheDirectory.value.ToString() + "/" + file.fileName);
		if (!destination)
		{
			return Result<ManagedHostBootstrap>::Failure(destination.error);
		}
		auto written = filesystem.WriteAtomic(destination.value, file.bytes);
		if (!written)
		{
			return Result<ManagedHostBootstrap>::Failure("Cannot stage " + file.source.ToString() +
			                                             " for the native managed host: " + written.error);
		}
	}

	auto runtimeConfig = VirtualPath::Parse(cacheDirectory.value.ToString() + "/" + files.runtimeConfig.FileName());
	auto entryAssembly = VirtualPath::Parse(cacheDirectory.value.ToString() + "/" + files.entryAssembly.FileName());
	if (!runtimeConfig || !entryAssembly)
	{
		return Result<ManagedHostBootstrap>::Failure(!runtimeConfig ? runtimeConfig.error : entryAssembly.error);
	}
	auto runtimePath = filesystem.ResolveForNativeHostInterop(runtimeConfig.value);
	if (!runtimePath)
	{
		return Result<ManagedHostBootstrap>::Failure(runtimePath.error);
	}
	auto assemblyPath = filesystem.ResolveForNativeHostInterop(entryAssembly.value);
	if (!assemblyPath)
	{
		return Result<ManagedHostBootstrap>::Failure(assemblyPath.error);
	}

	ManagedHostBootstrap result;
	result.runtimeConfigPath = std::move(runtimePath.value);
	result.entryAssemblyPath = std::move(assemblyPath.value);
	result.cacheDirectory = std::move(cacheDirectory.value);
	result.fingerprint = fingerprint;
	return Result<ManagedHostBootstrap>::Success(std::move(result));
}

Result<std::unique_ptr<DotNetHost>> DotNetHost::Create()
{
#if !defined(_WIN32) || !defined(_WIN64)
	return Result<std::unique_ptr<DotNetHost>>::Failure("The .NET host currently supports Windows x64 only");
#else
	return Result<std::unique_ptr<DotNetHost>>::Success(std::unique_ptr<DotNetHost>(new DotNetHost));
#endif
}

DotNetHost::~DotNetHost()
{
	Deactivate();
}

Result<void> DotNetHost::Start(VirtualFileSystem& filesystem, const ManagedHostFiles& files, LogCallback logCallback,
                               ManagedSceneApi* sceneApi)
{
#if !defined(_WIN32) || !defined(_WIN64)
	(void)filesystem;
	(void)files;
	(void)logCallback;
	(void)sceneApi;
	return Result<void>::Failure("The .NET host currently supports Windows x64 only");
#else
	if (initialized || hostFxrLibrary || bindingsEntryPoint || runtimeStarted.load())
	{
		return Result<void>::Failure("A .NET runtime can be initialized only once in this process");
	}
	if (!logCallback)
	{
		return Result<void>::Failure("The .NET host requires a log callback");
	}
	auto bootstrap = MaterializeManagedHostFiles(filesystem, files);
	if (!bootstrap)
	{
		return Result<void>::Failure(bootstrap.error);
	}

	std::vector<char_t> hostFxrPath(32768);
	size_t hostFxrPathSize = hostFxrPath.size();
	const int pathResult = get_hostfxr_path(hostFxrPath.data(), &hostFxrPathSize, nullptr);
	if (pathResult != 0 || hostFxrPathSize == 0 || hostFxrPathSize > hostFxrPath.size())
	{
		return Result<void>::Failure("Cannot locate the installed .NET hostfxr runtime");
	}

	HMODULE library = LoadLibraryW(reinterpret_cast<const wchar_t*>(hostFxrPath.data()));
	if (!library)
	{
		return Result<void>::Failure("Cannot load the installed .NET hostfxr library");
	}
	auto initialize =
	    HostFunction<hostfxr_initialize_for_runtime_config_fn>(library, "hostfxr_initialize_for_runtime_config");
	auto getDelegate = HostFunction<hostfxr_get_runtime_delegate_fn>(library, "hostfxr_get_runtime_delegate");
	auto close = HostFunction<hostfxr_close_fn>(library, "hostfxr_close");
	if (!initialize || !getDelegate || !close)
	{
		FreeLibrary(library);
		return Result<void>::Failure("The installed hostfxr is missing required hosting functions");
	}

	hostfxr_handle hostContext = nullptr;
	const int initializeResult =
	    initialize(reinterpret_cast<const char_t*>(bootstrap.value.runtimeConfigPath.c_str()), nullptr, &hostContext);
	if (initializeResult < 0 || !hostContext)
	{
		close(hostContext);
		FreeLibrary(library);
		return Result<void>::Failure("Cannot initialize CoreCLR from the managed runtime configuration");
	}

	void* loadAssemblyDelegate = nullptr;
	const int delegateResult =
	    getDelegate(hostContext, hdt_load_assembly_and_get_function_pointer, &loadAssemblyDelegate);
	close(hostContext);
	if (delegateResult < 0 || !loadAssemblyDelegate)
	{
		FreeLibrary(library);
		return Result<void>::Failure("CoreCLR did not provide the managed assembly loading delegate");
	}
	bool expected = false;
	if (!runtimeStarted.compare_exchange_strong(expected, true))
	{
		FreeLibrary(library);
		return Result<void>::Failure("Another DotNetHost already initialized CoreCLR in this process");
	}

	auto loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyDelegate);
	const auto loadBootstrapMethod = [&loadAssembly, &bootstrap](const wchar_t* method, void** output)
	{
		return loadAssembly(reinterpret_cast<const char_t*>(bootstrap.value.entryAssemblyPath.c_str()),
		                    reinterpret_cast<const char_t*>(L"Ursa.ManagedHost.Bootstrap, Ursa.ManagedHost"),
		                    reinterpret_cast<const char_t*>(method), UNMANAGEDCALLERSONLY_METHOD, nullptr, output);
	};
	void* bindingsEntry = nullptr;
	if (loadBootstrapMethod(L"InitializeBindings", &bindingsEntry) < 0 || !bindingsEntry)
	{
		FreeLibrary(library);
		return Result<void>::Failure("Cannot load the binding bootstrap entry");
	}

	hostFxrLibrary = library;
	bindingsEntryPoint = bindingsEntry;
	onLog = std::move(logCallback);
	this->filesystem = &filesystem;
	auto modules = BindModules(sceneApi);
	if (!modules)
	{
		return modules;
	}
	auto framework = HostBindings::Call_ManagedHostBindings_InitializeFramework(bindingState->host);
	if (!framework)
	{
		initialized = true;
		return Result<void>::Failure(framework.Error());
	}
	initialized = true;
	for (const VirtualPath& assemblyPath : files.scriptAssemblies)
	{
		auto bytes = filesystem.Read(assemblyPath);
		if (!bytes)
		{
			return Result<void>::Failure("Cannot read managed script assembly " + assemblyPath.ToString() + ": " +
			                             bytes.error);
		}
		auto loaded = LoadScriptAssembly(bytes.value);
		if (!loaded)
		{
			return Result<void>::Failure("Cannot load managed script assembly " + assemblyPath.ToString() + ": " +
			                             loaded.error);
		}
	}
	return {};
#endif
}

Result<void> DotNetHost::BindModules(ManagedSceneApi* sceneApi)
{
	if (!bindingsEntryPoint)
	{
		return Result<void>::Failure("Binding bootstrap is unavailable");
	}
	if (sceneApi)
	{
		auto services = sceneApi->BindHostServices(Token());
		if (!services)
		{
			return services;
		}
	}
	void* sceneContext = sceneApi ? sceneApi->Token() : Token();
	Bindings::BindingModuleReference modules[]{
	    {Bindings::Runtime::moduleId, &Bindings::Runtime::NativeDescriptor(), sceneContext},
	    {Bindings::Rendering::moduleId, &Bindings::Rendering::NativeDescriptor(), sceneContext},
	    {Bindings::ManagedHost::moduleId, &Bindings::ManagedHost::NativeDescriptor(), Token()}};
	Bindings::BindingModuleReference callbacks[4]{};
	Bindings::BindingBootstrap request;
	request.modules = modules;
	request.moduleCount = std::size(modules);
	request.callbacks = callbacks;
	request.callbackCapacity = std::size(callbacks);
	const auto invoke = reinterpret_cast<component_entry_point_fn>(bindingsEntryPoint);
	const Int32 status = invoke(&request, sizeof(request));
	String error;
	if (status != 0)
	{
		error = Bindings::ReadString(request.outcome.error, true);
	}
	Bindings::Release(request.outcome);
	if (status != 0)
	{
		return Result<void>::Failure("Binding module installation failed: " + error);
	}
	if (request.callbackCount != 1 || callbacks[0].id != HostBindings::moduleId)
	{
		return Result<void>::Failure("ManagedHost callback module was not supplied");
	}
	auto connected = HostBindings::ConnectCallbacks(callbacks[0].descriptor, callbacks[0].context);
	if (!connected)
	{
		return Result<void>::Failure(connected.error);
	}
	auto candidate = std::make_unique<BindingState>();
	candidate->host = std::move(connected.value);
	bindingState = std::move(candidate);
	return {};
}

Result<void> DotNetHost::BindSceneApi(ManagedSceneApi& sceneApi)
{
	if (!initialized || !bindingsEntryPoint)
	{
		return Result<void>::Failure("Managed host is not initialized");
	}
	return BindModules(&sceneApi);
}

Result<UInt64> DotNetHost::CreateBehaviour(const String& typeName, const ManagedHandle& gameObject)
{
	if (!initialized || !bindingState || typeName.empty())
	{
		return Result<UInt64>::Failure("The managed Behaviour runtime is not initialized or the type name is empty");
	}
	Bindings::Utf8View name{reinterpret_cast<const UInt8*>(typeName.data()), typeName.size(), 1, 0};
	Bindings::Wire::Value_Ursa_NativeHandle owner{gameObject.sessionId, gameObject.index, gameObject.generation,
	                                              static_cast<UInt32>(gameObject.kind), gameObject.reserved};
	auto result = HostBindings::Call_ManagedHostBindings_CreateBehaviour(bindingState->host, name, owner);
	if (!result || result.value == 0)
	{
		return Result<UInt64>::Failure(result.Error());
	}
	return Result<UInt64>::Success(result.value);
}

Result<void> DotNetHost::InvokeBehaviour(UInt64 instanceToken, ManagedBehaviourEvent event)
{
	if (!initialized || !bindingState || instanceToken == 0)
	{
		return Result<void>::Failure("Cannot invoke an invalid managed Behaviour instance");
	}
	auto result = HostBindings::Call_ManagedHostBindings_InvokeBehaviour(bindingState->host, instanceToken,
	                                                                     static_cast<UInt32>(event));
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::BindBehaviour(UInt64 instanceToken, const ManagedHandle& component)
{
	if (!initialized || !bindingState || instanceToken == 0 || component.kind != ManagedHandleKind::Component)
	{
		return Result<void>::Failure("Cannot bind an invalid managed Behaviour Component handle");
	}
	Bindings::Wire::Value_Ursa_NativeHandle handle{component.sessionId, component.index, component.generation,
	                                               static_cast<UInt32>(component.kind), component.reserved};
	auto result = HostBindings::Call_ManagedHostBindings_BindBehaviour(bindingState->host, instanceToken, handle);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::DestroyBehaviour(UInt64 instanceToken)
{
	if (!initialized || !bindingState || instanceToken == 0)
	{
		return Result<void>::Failure("Cannot destroy an invalid managed Behaviour instance");
	}
	auto result = HostBindings::Call_ManagedHostBindings_DestroyBehaviour(bindingState->host, instanceToken);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<String> DotNetHost::SerializeBehaviour(UInt64 instanceToken)
{
	if (!initialized || !bindingState || instanceToken == 0)
	{
		return Result<String>::Failure("Cannot serialize an invalid managed Behaviour instance");
	}
	auto result = HostBindings::Call_ManagedHostBindings_SerializeBehaviour(bindingState->host, instanceToken);
	if (!result)
	{
		return Result<String>::Failure(result.Error());
	}
	if (result.value.F_Version != 1)
	{
		return Result<String>::Failure("Unsupported managed Behaviour field archive version");
	}
	return Result<String>::Success(Bindings::ReadString(result.value.F_Json));
}

Result<void> DotNetHost::DeserializeBehaviour(UInt64 instanceToken, const String& payload)
{
	if (!initialized || !bindingState || instanceToken == 0 || payload.empty() ||
	    payload.size() > 64ull * 1024ull * 1024ull)
	{
		return Result<void>::Failure("Cannot deserialize an invalid managed Behaviour payload");
	}
	Bindings::Wire::Value_Ursa_ManagedHost_BehaviourFieldArchive archive;
	archive.F_Version = 1;
	archive.F_Json = {reinterpret_cast<const UInt8*>(payload.data()), payload.size(), 1, 0};
	auto result =
	    HostBindings::Call_ManagedHostBindings_DeserializeBehaviour(bindingState->host, instanceToken, archive);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::LoadScriptAssembly(const Vector<UInt8>& assemblyBytes)
{
	if (!initialized || !bindingState || assemblyBytes.empty())
	{
		return Result<void>::Failure("Cannot load an empty script assembly or the managed host is not initialized");
	}
	Bindings::DataView image{assemblyBytes.data(), assemblyBytes.size(), 1, 0};
	auto result = HostBindings::Call_ManagedHostBindings_LoadScriptAssembly(bindingState->host, image);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<Vector<ManagedEditorExtension>> DotNetHost::PrepareEditorExtensions(UInt64 sessionId)
{
	if (sessionFaulted || !bindingState)
	{
		return Result<Vector<ManagedEditorExtension>>::Failure("Managed editing environment is unavailable");
	}
	auto result = HostBindings::Call_ManagedHostBindings_PrepareEditorExtensions(bindingState->host, sessionId);
	if (!result)
	{
		return Result<Vector<ManagedEditorExtension>>::Failure(result.Error());
	}
	try
	{
		return Result<Vector<ManagedEditorExtension>>::Success(
		    Bindings::ReadArray<Bindings::Wire::Value_Ursa_EditorExtensionInfo, ManagedEditorExtension>(
		        result.value,
		        [](const auto& value)
		        {
			        using K = Bindings::Wire::Enum_Ursa_EditorExtensionKind;
			        ManagedEditorExtensionKind kind;
			        switch (value.F_Kind)
			        {
				        case K::V_Menu:
					        kind = ManagedEditorExtensionKind::Menu;
					        break;
				        case K::V_Window:
					        kind = ManagedEditorExtensionKind::Window;
					        break;
				        case K::V_Inspector:
					        kind = ManagedEditorExtensionKind::Inspector;
					        break;
				        default:
					        throw std::runtime_error("Unknown managed Editor extension kind");
			        }
			        return ManagedEditorExtension{
			            Bindings::ReadString(value.F_Id), kind, Bindings::ReadString(value.F_Path),
			            Bindings::ReadString(value.F_Target), Bindings::ReadString(value.F_Title)};
		        }));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<ManagedEditorExtension>>::Failure(ToUrsaString(error.what()));
	}
}

Result<bool> DotNetHost::InvokeEditorExtension(UInt64 sessionId, const String& extensionId,
                                               const Bindings::BindingModuleReference& capability)
{
	if (sessionFaulted || !bindingState || !capability.descriptor || !capability.context)
	{
		return Result<bool>::Failure("Managed editing environment or callback scope is unavailable");
	}
	const Bindings::Wire::Value_Ursa_EditorBindingCapability transfer{
	    capability.id, static_cast<UInt64>(reinterpret_cast<UIntPtr>(capability.descriptor)),
	    static_cast<UInt64>(reinterpret_cast<UIntPtr>(capability.context))};
	const Bindings::Utf8View id{reinterpret_cast<const UInt8*>(extensionId.data()), extensionId.size(), 1, 0};
	auto result =
	    HostBindings::Call_ManagedHostBindings_InvokeEditorExtension(bindingState->host, sessionId, id, transfer);
	return result ? Result<bool>::Success(result.value != 0) : Result<bool>::Failure(result.Error());
}

Result<String> DotNetHost::PrepareScriptReload(UInt64 sessionId, const String& snapshots)
{
	if (!bindingState || snapshots.empty())
	{
		return Result<String>::Failure("Candidate reload bridge is unavailable");
	}
	Bindings::Wire::Value_Ursa_ManagedHost_ScriptReloadArchive archive;
	archive.F_Version = 1;
	archive.F_Json = {reinterpret_cast<const UInt8*>(snapshots.data()), snapshots.size(), 1, 0};
	auto result = HostBindings::Call_ManagedHostBindings_PrepareScriptReload(bindingState->host, sessionId, archive);
	if (!result)
	{
		return Result<String>::Failure(result.Error());
	}
	if (result.value.F_Version != 1)
	{
		return Result<String>::Failure("Unsupported script reload archive version");
	}
	return Result<String>::Success(Bindings::ReadString(result.value.F_Json));
}

Result<void> DotNetHost::TransferScriptableAssets(UInt64 sourceSession, UInt64 candidateSession)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("ScriptableAsset reload bridge is unavailable");
	}
	auto result = HostBindings::Call_ManagedHostBindings_TransferScriptableAssets(bindingState->host, sourceSession,
	                                                                              candidateSession);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::ConfigureRenderPipeline(UInt64 session, AssetReference pipeline)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("Managed pipeline configuration is unavailable");
	}
	Bindings::Wire::Value_Ursa_ManagedHost_PipelineAssetReference_ asset;
	const String guid = pipeline.guid ? pipeline.guid.ToString() : String{};
	if (pipeline.guid)
	{
		asset.present = 1;
		asset.F_value.F_Guid = {reinterpret_cast<const UInt8*>(guid.data()), guid.size(), 1, 0};
		asset.F_value.F_LocalFileId = pipeline.localFileId;
	}
	auto result = HostBindings::Call_ManagedHostBindings_ConfigureRenderPipeline(bindingState->host, session, asset);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::RenderScene(UInt64 session, Float32 aspect)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("Managed pipeline rendering is unavailable");
	}
	auto result = HostBindings::Call_ManagedHostBindings_RenderScene(bindingState->host, session, aspect);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::LoadEditorApi(const VirtualPath& path)
{
	if (!filesystem || !bindingState)
	{
		return Result<void>::Failure("Shared editor API loader is unavailable");
	}
	auto bytes = filesystem->Read(path);
	if (!bytes)
	{
		return Result<void>::Failure(bytes.error);
	}
	Bindings::DataView image{bytes.value.data(), bytes.value.size(), 1, 0};
	auto result = HostBindings::Call_ManagedHostBindings_LoadEditorApi(bindingState->host, image);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::LoadScriptContent(UInt64 sessionId, const VirtualPath& manifest)
{
	if (!filesystem || !bindingState)
	{
		return Result<void>::Failure("Managed content loader is unavailable");
	}
	auto bytes = filesystem->Read(manifest);
	if (!bytes)
	{
		return Result<void>::Failure(bytes.error);
	}
	try
	{
		const auto content = nlohmann::json::parse(bytes.value.begin(), bytes.value.end());
		const auto group = content.at("Group").get<std::string>();
		if (content.at("Version").get<UInt32>() != 2 || (group != "Game" && group != "Editor"))
		{
			return Result<void>::Failure("Unsupported script content version/group; rebuild the project content");
		}
		auto requirements =
		    Bindings::ValidateScriptBindingRequirements(content.at("RequiredBindings"), group == "Editor");
		if (!requirements)
		{
			return requirements;
		}
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Invalid script content manifest: " + ToUrsaString(error.what()));
	}
	auto selected = SelectScriptSession(sessionId);
	if (!selected)
	{
		return selected;
	}
	Bindings::Wire::Value_Ursa_ManagedHost_ScriptContentArchive archive;
	archive.F_Version = 1;
	archive.F_Json = {bytes.value.data(), bytes.value.size(), 1, 0};
	auto result = HostBindings::Call_ManagedHostBindings_LoadScriptContent(bindingState->host, sessionId, archive);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<UInt64> DotNetHost::CreateScriptSession(bool editor, ManagedSceneApi* sceneApi)
{
	if (!initialized || !bindingState || sessionFaulted)
	{
		return Result<UInt64>::Failure("Managed host is unavailable or faulted after an unload failure");
	}
	auto modules = BindModules(sceneApi);
	if (!modules)
	{
		return Result<UInt64>::Failure(modules.error);
	}
	auto result = HostBindings::Call_ManagedHostBindings_CreateScriptSession(bindingState->host, editor ? 1u : 0u);
	return result && result.value != 0 ? Result<UInt64>::Success(result.value)
	                                   : Result<UInt64>::Failure(result.Error());
}

Result<void> DotNetHost::SelectScriptSession(UInt64 sessionId)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("Managed host is unavailable");
	}
	auto result = HostBindings::Call_ManagedHostBindings_SelectScriptSession(bindingState->host, sessionId);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::BindScriptSession(UInt64 sessionId, ManagedSceneApi& sceneApi)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("Managed host is unavailable");
	}
	auto modules = BindModules(&sceneApi);
	if (!modules)
	{
		return modules;
	}
	auto result = HostBindings::Call_ManagedHostBindings_BindScriptSession(bindingState->host, sessionId);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::ValidateScriptSession(UInt64 sessionId)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("Managed host is unavailable");
	}
	auto result = HostBindings::Call_ManagedHostBindings_ValidateScriptSession(bindingState->host, sessionId);
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::CloseScriptSession(UInt64 sessionId)
{
	if (!initialized || !bindingState)
	{
		return Result<void>::Failure("Managed host is unavailable");
	}
	auto result = HostBindings::Call_ManagedHostBindings_CloseScriptSession(bindingState->host, sessionId);
	if (result && result.value == 0)
	{
		sessionFaulted = true;
		return Result<void>::Failure(
		    "Managed context still has strong references after bounded unload checks; restart the application");
	}
	return result ? Result<void>{} : Result<void>::Failure(result.Error());
}

Result<void> DotNetHost::WriteScriptLog(UInt32 level, const String& message)
{
	if (level > 5)
	{
		return Result<void>::Failure("Unknown Script log level");
	}
	static const LogLevel levels[] = {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
	                                  LogLevel::Warn,  LogLevel::Error, LogLevel::Critical};
	Log::Write(levels[level], "Script", message);
	return {};
}

Result<void> DotNetHost::ReportHostMessage(UInt32 level, const String& message)
{
	if (!onLog)
	{
		return Result<void>::Failure("Host log callback is unavailable");
	}
	onLog(level, message);
	return {};
}

Result<Vector<UInt8>> DotNetHost::ReadScriptFile(const String& path)
{
	if (!filesystem)
	{
		return Result<Vector<UInt8>>::Failure("Host VFS is unavailable");
	}
	const auto parsed = VirtualPath::Parse(path);
	return parsed ? filesystem->Read(parsed.value) : Result<Vector<UInt8>>::Failure(parsed.error);
}

Result<void> DotNetHost::WriteScriptFile(const String& path, const Vector<UInt8>& bytes)
{
	if (!filesystem)
	{
		return Result<void>::Failure("Host VFS is unavailable");
	}
	const auto parsed = VirtualPath::Parse(path);
	return parsed ? filesystem->WriteAtomic(parsed.value, bytes) : Result<void>::Failure(parsed.error);
}

} // namespace Ursa::Scripting
