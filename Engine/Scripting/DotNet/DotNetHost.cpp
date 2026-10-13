#include <Core/Types.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <string>
#include <sstream>
#include <vector>

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
namespace
{
Int32 RejectRenderBatchWithoutScene(void*, const UInt8*, UInt64, const UInt8*, UInt64)
{
	return 1;
}
} // namespace

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

DotNetHost::~DotNetHost() = default;

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
	if (initialized || hostFxrLibrary || managedEntryPoint || runtimeStarted.load())
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
	void* entryPoint = nullptr;
	const int loadResult =
	    loadAssembly(reinterpret_cast<const char_t*>(bootstrap.value.entryAssemblyPath.c_str()),
	                 reinterpret_cast<const char_t*>(L"Ursa.ManagedHost.Bootstrap, Ursa.ManagedHost"),
	                 reinterpret_cast<const char_t*>(L"Initialize"), UNMANAGEDCALLERSONLY_METHOD, nullptr, &entryPoint);
	if (loadResult < 0 || !entryPoint)
	{
		FreeLibrary(library);
		return Result<void>::Failure("Cannot load Ursa.ManagedHost.Bootstrap.Initialize");
	}
	void* createBehaviourEntry = nullptr;
	void* bindBehaviourEntry = nullptr;
	void* invokeBehaviourEntry = nullptr;
	void* destroyBehaviourEntry = nullptr;
	void* serializeBehaviourEntry = nullptr;
	void* deserializeBehaviourEntry = nullptr;
	void* loadScriptAssemblyEntry = nullptr;
	void* prepareExtensionsEntry = nullptr;
	void* describeExtensionsEntry = nullptr;
	void* invokeExtensionEntry = nullptr;
	void* prepareReloadEntry = nullptr;
	void* readReloadEntry = nullptr;
	void* loadEditorEntry = nullptr;
	void* loadContentEntry = nullptr;
	void* createSessionEntry = nullptr;
	void* selectSessionEntry = nullptr;
	void* bindSessionEntry = nullptr;
	void* validateSessionEntry = nullptr;
	void* closeSessionEntry = nullptr;
	const auto loadBootstrapMethod = [&loadAssembly, &bootstrap](const wchar_t* method, void** output)
	{
		return loadAssembly(reinterpret_cast<const char_t*>(bootstrap.value.entryAssemblyPath.c_str()),
		                    reinterpret_cast<const char_t*>(L"Ursa.ManagedHost.Bootstrap, Ursa.ManagedHost"),
		                    reinterpret_cast<const char_t*>(method), UNMANAGEDCALLERSONLY_METHOD, nullptr, output);
	};
	if (loadBootstrapMethod(L"CreateBehaviour", &createBehaviourEntry) < 0 || !createBehaviourEntry ||
	    loadBootstrapMethod(L"BindBehaviour", &bindBehaviourEntry) < 0 || !bindBehaviourEntry ||
	    loadBootstrapMethod(L"InvokeBehaviour", &invokeBehaviourEntry) < 0 || !invokeBehaviourEntry ||
	    loadBootstrapMethod(L"DestroyBehaviour", &destroyBehaviourEntry) < 0 || !destroyBehaviourEntry ||
	    loadBootstrapMethod(L"SerializeBehaviour", &serializeBehaviourEntry) < 0 || !serializeBehaviourEntry ||
	    loadBootstrapMethod(L"DeserializeBehaviour", &deserializeBehaviourEntry) < 0 || !deserializeBehaviourEntry ||
	    loadBootstrapMethod(L"LoadScriptAssembly", &loadScriptAssemblyEntry) < 0 || !loadScriptAssemblyEntry ||
	    loadBootstrapMethod(L"PrepareEditorExtensions", &prepareExtensionsEntry) < 0 || !prepareExtensionsEntry ||
	    loadBootstrapMethod(L"DescribeEditorExtensions", &describeExtensionsEntry) < 0 || !describeExtensionsEntry ||
	    loadBootstrapMethod(L"InvokeEditorExtension", &invokeExtensionEntry) < 0 || !invokeExtensionEntry ||
	    loadBootstrapMethod(L"PrepareScriptReload", &prepareReloadEntry) < 0 || !prepareReloadEntry ||
	    loadBootstrapMethod(L"ReadPreparedScriptReload", &readReloadEntry) < 0 || !readReloadEntry ||
	    loadBootstrapMethod(L"LoadEditorApi", &loadEditorEntry) < 0 || !loadEditorEntry ||
	    loadBootstrapMethod(L"LoadScriptContent", &loadContentEntry) < 0 || !loadContentEntry ||
	    loadBootstrapMethod(L"CreateScriptSession", &createSessionEntry) < 0 || !createSessionEntry ||
	    loadBootstrapMethod(L"SelectScriptSession", &selectSessionEntry) < 0 || !selectSessionEntry ||
	    loadBootstrapMethod(L"BindScriptSession", &bindSessionEntry) < 0 || !bindSessionEntry ||
	    loadBootstrapMethod(L"ValidateScriptSession", &validateSessionEntry) < 0 || !validateSessionEntry ||
	    loadBootstrapMethod(L"CloseScriptSession", &closeSessionEntry) < 0 || !closeSessionEntry)
	{
		FreeLibrary(library);
		return Result<void>::Failure("Cannot load the managed Behaviour lifecycle entry points");
	}

	hostFxrLibrary = library;
	managedEntryPoint = entryPoint;
	createBehaviour = reinterpret_cast<CreateBehaviourFunction>(createBehaviourEntry);
	bindBehaviour = reinterpret_cast<BindBehaviourFunction>(bindBehaviourEntry);
	invokeBehaviour = reinterpret_cast<InvokeBehaviourFunction>(invokeBehaviourEntry);
	destroyBehaviour = reinterpret_cast<DestroyBehaviourFunction>(destroyBehaviourEntry);
	serializeBehaviour = reinterpret_cast<SerializeBehaviourFunction>(serializeBehaviourEntry);
	deserializeBehaviour = reinterpret_cast<DeserializeBehaviourFunction>(deserializeBehaviourEntry);
	this->loadAssembly = reinterpret_cast<LoadAssemblyFunction>(loadScriptAssemblyEntry);
	prepareExtensions = reinterpret_cast<SessionFunction>(prepareExtensionsEntry);
	describeExtensions = reinterpret_cast<ReadSessionTextFunction>(describeExtensionsEntry);
	invokeExtension = reinterpret_cast<InvokeEditorFunction>(invokeExtensionEntry);
	prepareReload = reinterpret_cast<LoadContentFunction>(prepareReloadEntry);
	readReload = reinterpret_cast<ReadSessionTextFunction>(readReloadEntry);
	loadEditor = reinterpret_cast<LoadAssemblyFunction>(loadEditorEntry);
	loadContent = reinterpret_cast<LoadContentFunction>(loadContentEntry);
	createSession = reinterpret_cast<CreateSessionFunction>(createSessionEntry);
	selectSession = reinterpret_cast<SessionFunction>(selectSessionEntry);
	bindSession = reinterpret_cast<BindSessionFunction>(bindSessionEntry);
	validateSession = reinterpret_cast<SessionFunction>(validateSessionEntry);
	closeSession = reinterpret_cast<SessionFunction>(closeSessionEntry);
	onLog = std::move(logCallback);
	this->filesystem = &filesystem;
	NativeApiV6 api = BuildNativeApi(sceneApi);
	const auto invoke = reinterpret_cast<component_entry_point_fn>(managedEntryPoint);
	const int managedResult = invoke(&api, static_cast<int32_t>(sizeof(NativeApiV6)));
	if (managedResult != 0)
	{
		initialized = true;
		return Result<void>::Failure(String("Managed host initialization returned status ") +
		                             ToUrsaString(std::to_string(managedResult)));
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

NativeApiV6 DotNetHost::BuildNativeApi(ManagedSceneApi* sceneApi)
{
	NativeApiV6 api;
	api.size = static_cast<UInt32>(sizeof(NativeApiV6));
	api.context = this;
	api.logUtf8 = &DotNetHost::LogFromManaged;
	api.writeLog = &DotNetHost::WriteLogFromManaged;
	api.readFile = &DotNetHost::ReadFileFromManaged;
	api.writeFile = &DotNetHost::WriteFileFromManaged;
	api.submitRenderBatch = &RejectRenderBatchWithoutScene;
	if (sceneApi)
	{
		api = sceneApi->Bind(api);
	}
	return api;
}

Result<void> DotNetHost::BindSceneApi(ManagedSceneApi& sceneApi)
{
	if (!initialized || !managedEntryPoint)
	{
		return Result<void>::Failure("Managed host is not initialized");
	}
	NativeApiV6 api = BuildNativeApi(&sceneApi);
	const auto invoke = reinterpret_cast<component_entry_point_fn>(managedEntryPoint);
	const Int32 status = invoke(&api, static_cast<Int32>(sizeof(NativeApiV6)));
	return status == 0 ? Result<void>{}
	                   : Result<void>::Failure("Managed host rejected the active Scene API with status " +
	                                           ToUrsaString(std::to_string(status)));
}

Result<UInt64> DotNetHost::CreateBehaviour(const String& typeName, const ManagedHandle& gameObject)
{
	if (!initialized || !createBehaviour || typeName.empty())
	{
		return Result<UInt64>::Failure("The managed Behaviour runtime is not initialized or the type name is empty");
	}
	UInt64 instanceToken = 0;
	const Int32 status =
	    createBehaviour(reinterpret_cast<const UInt8*>(typeName.data()), typeName.size(), &gameObject, &instanceToken);
	if (status != 0 || instanceToken == 0)
	{
		return Result<UInt64>::Failure("Cannot create managed Behaviour '" + typeName + "' (status " +
		                               ToUrsaString(std::to_string(status)) + ")");
	}
	return Result<UInt64>::Success(instanceToken);
}

Result<void> DotNetHost::InvokeBehaviour(UInt64 instanceToken, ManagedBehaviourEvent event)
{
	if (!initialized || !invokeBehaviour || instanceToken == 0)
	{
		return Result<void>::Failure("Cannot invoke an invalid managed Behaviour instance");
	}
	const Int32 status = invokeBehaviour(instanceToken, static_cast<UInt32>(event));
	return status == 0 ? Result<void>{}
	                   : Result<void>::Failure("Managed Behaviour callback failed with status " +
	                                           ToUrsaString(std::to_string(status)));
}

Result<void> DotNetHost::BindBehaviour(UInt64 instanceToken, const ManagedHandle& component)
{
	if (!initialized || !bindBehaviour || instanceToken == 0 || component.kind != ManagedHandleKind::Component)
	{
		return Result<void>::Failure("Cannot bind an invalid managed Behaviour Component handle");
	}
	const Int32 status = bindBehaviour(instanceToken, &component);
	return status == 0 ? Result<void>{}
	                   : Result<void>::Failure("Managed Behaviour Component binding failed with status " +
	                                           ToUrsaString(std::to_string(status)));
}

Result<void> DotNetHost::DestroyBehaviour(UInt64 instanceToken)
{
	if (!initialized || !destroyBehaviour || instanceToken == 0)
	{
		return Result<void>::Failure("Cannot destroy an invalid managed Behaviour instance");
	}
	const Int32 status = destroyBehaviour(instanceToken);
	return status == 0 ? Result<void>{}
	                   : Result<void>::Failure("Managed Behaviour destruction failed with status " +
	                                           ToUrsaString(std::to_string(status)));
}

Result<String> DotNetHost::SerializeBehaviour(UInt64 instanceToken)
{
	if (!initialized || !serializeBehaviour || instanceToken == 0)
	{
		return Result<String>::Failure("Cannot serialize an invalid managed Behaviour instance");
	}
	UInt64 required = 0;
	Int32 status = serializeBehaviour(instanceToken, nullptr, 0, &required);
	if (status != 4 || required == 0 || required > 64ull * 1024ull * 1024ull)
	{
		return Result<String>::Failure("Managed Behaviour field serialization failed with status " +
		                               ToUrsaString(std::to_string(status)));
	}
	Vector<UInt8> bytes(static_cast<Size>(required));
	status = serializeBehaviour(instanceToken, bytes.data(), required, &required);
	if (status != 0)
	{
		return Result<String>::Failure("Managed Behaviour field serialization failed with status " +
		                               ToUrsaString(std::to_string(status)));
	}
	return Result<String>::Success(ToUrsaString(std::string(bytes.begin(), bytes.end())));
}

Result<void> DotNetHost::DeserializeBehaviour(UInt64 instanceToken, const String& payload)
{
	if (!initialized || !deserializeBehaviour || instanceToken == 0 || payload.empty() ||
	    payload.size() > 64ull * 1024ull * 1024ull)
	{
		return Result<void>::Failure("Cannot deserialize an invalid managed Behaviour payload");
	}
	const Int32 status =
	    deserializeBehaviour(instanceToken, reinterpret_cast<const UInt8*>(payload.data()), payload.size());
	return status == 0 ? Result<void>{}
	                   : Result<void>::Failure("Managed Behaviour field deserialization failed with status " +
	                                           ToUrsaString(std::to_string(status)));
}

Result<void> DotNetHost::LoadScriptAssembly(const Vector<UInt8>& assemblyBytes)
{
	if (!initialized || !loadAssembly || assemblyBytes.empty())
	{
		return Result<void>::Failure("Cannot load an empty script assembly or the managed host is not initialized");
	}
	const Int32 status = loadAssembly(assemblyBytes.data(), assemblyBytes.size());
	return status == 0 ? Result<void>{}
	                   : Result<void>::Failure("Managed assembly load failed with status " +
	                                           ToUrsaString(std::to_string(status)));
}

Result<String> DotNetHost::PrepareEditorExtensions(UInt64 sessionId)
{
	if (!prepareExtensions || !describeExtensions || prepareExtensions(sessionId) != 0)
	{
		return Result<String>::Failure("Candidate managed editor registrations failed validation");
	}
	UInt64 required = 0;
	if (describeExtensions(sessionId, nullptr, 0, &required) != 4 || required == 0 || required > 16ull * 1024 * 1024)
	{
		return Result<String>::Failure("Managed editor registration directory is invalid");
	}
	Vector<UInt8> bytes(static_cast<Size>(required));
	if (describeExtensions(sessionId, bytes.data(), bytes.size(), &required) != 0)
	{
		return Result<String>::Failure("Cannot read the managed editor registration directory");
	}
	return Result<String>::Success(String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

Result<bool> DotNetHost::InvokeEditorExtension(UInt64 sessionId, const String& extensionId, const ManagedEditorApi& api)
{
	if (sessionFaulted || !invokeExtension || api.version != 1 || api.size != sizeof(ManagedEditorApi) ||
	    !api.context || !api.exchange)
	{
		return Result<bool>::Failure("Managed editor environment or callback scope is invalid");
	}
	const Int32 status = invokeExtension(sessionId, reinterpret_cast<const UInt8*>(extensionId.data()),
	                                     extensionId.size(), &api, sizeof(api));
	return status == 0   ? Result<bool>::Success(true)
	       : status == 2 ? Result<bool>::Success(false)
	                     : Result<bool>::Failure("Managed editor protocol rejected this extension call");
}

Result<String> DotNetHost::PrepareScriptReload(UInt64 sessionId, const String& snapshots)
{
	if (!prepareReload || !readReload || snapshots.empty())
	{
		return Result<String>::Failure("Candidate reload bridge is unavailable");
	}
	if (prepareReload(sessionId, reinterpret_cast<const UInt8*>(snapshots.data()), snapshots.size()) != 0)
	{
		return Result<String>::Failure(
		    "Candidate script fields are missing or incompatible; existing editing state was retained");
	}
	UInt64 required = 0;
	if (readReload(sessionId, nullptr, 0, &required) != 4 || required == 0 || required > 64ull * 1024 * 1024)
	{
		return Result<String>::Failure("Candidate script snapshot output is invalid");
	}
	Vector<UInt8> bytes(static_cast<Size>(required));
	if (readReload(sessionId, bytes.data(), bytes.size(), &required) != 0)
	{
		return Result<String>::Failure("Cannot read candidate script snapshots");
	}
	return Result<String>::Success(String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

Result<void> DotNetHost::LoadEditorApi(const VirtualPath& path)
{
	if (!filesystem || !loadEditor)
	{
		return Result<void>::Failure("Shared editor API loader is unavailable");
	}
	auto bytes = filesystem->Read(path);
	if (!bytes)
	{
		return Result<void>::Failure(bytes.error);
	}
	return loadEditor(bytes.value.data(), bytes.value.size()) == 0
	           ? Result<void>{}
	           : Result<void>::Failure("Cannot load the shared Ursa.Editor API from VFS");
}

Result<void> DotNetHost::LoadScriptContent(UInt64 sessionId, const VirtualPath& manifest)
{
	if (!filesystem || !loadContent)
	{
		return Result<void>::Failure("Managed content loader is unavailable");
	}
	auto selected = SelectScriptSession(sessionId);
	if (!selected)
	{
		return selected;
	}
	auto bytes = filesystem->Read(manifest);
	if (!bytes)
	{
		return Result<void>::Failure(bytes.error);
	}
	return loadContent(sessionId, bytes.value.data(), bytes.value.size()) == 0
	           ? Result<void>{}
	           : Result<void>::Failure("Managed script content manifest or dependency validation failed: " +
	                                   manifest.ToString());
}

Result<UInt64> DotNetHost::CreateScriptSession(bool editor, ManagedSceneApi* sceneApi)
{
	if (!initialized || !createSession || sessionFaulted)
	{
		return Result<UInt64>::Failure("Managed host is unavailable or faulted after an unload failure");
	}
	NativeApiV6 api = BuildNativeApi(sceneApi);
	UInt64 sessionId = 0;
	const Int32 status = createSession(editor ? 1 : 0, &api, static_cast<Int32>(sizeof(api)), &sessionId);
	return status == 0 && sessionId != 0 ? Result<UInt64>::Success(sessionId)
	                                     : Result<UInt64>::Failure("Managed script session creation failed");
}

Result<void> DotNetHost::SelectScriptSession(UInt64 sessionId)
{
	return initialized && selectSession && selectSession(sessionId) == 0
	           ? Result<void>{}
	           : Result<void>::Failure("Managed script session is closed, unknown or on the wrong thread");
}

Result<void> DotNetHost::BindScriptSession(UInt64 sessionId, ManagedSceneApi& sceneApi)
{
	NativeApiV6 api = BuildNativeApi(&sceneApi);
	return initialized && bindSession && bindSession(sessionId, &api, static_cast<Int32>(sizeof(api))) == 0
	           ? Result<void>{}
	           : Result<void>::Failure("Cannot bind the Scene API to this managed script session");
}

Result<void> DotNetHost::ValidateScriptSession(UInt64 sessionId)
{
	return initialized && validateSession && validateSession(sessionId) == 0
	           ? Result<void>{}
	           : Result<void>::Failure("Managed script session type or dependency validation failed");
}

Result<void> DotNetHost::CloseScriptSession(UInt64 sessionId)
{
	if (!initialized || !closeSession)
	{
		return Result<void>::Failure("Managed host is unavailable");
	}
	const Int32 status = closeSession(sessionId);
	if (status == 3)
	{
		sessionFaulted = true;
		return Result<void>::Failure(
		    "Managed context still has strong references after bounded unload checks; restart the application");
	}
	return status == 0 ? Result<void>{} : Result<void>::Failure("Managed script session cleanup failed");
}

void DotNetHost::LogFromManaged(void* context, UInt32 level, const UInt8* message, UInt64 length)
{
	auto* host = static_cast<DotNetHost*>(context);
	if (!host || !host->onLog || (!message && length != 0) || length > std::numeric_limits<Size>::max())
	{
		return;
	}
	const String text =
	    length == 0 ? String{} : String(reinterpret_cast<const char*>(message), static_cast<Size>(length));
	host->onLog(level, text);
}

Int32 DotNetHost::WriteLogFromManaged(void*, UInt32 level, const UInt8* message, UInt64 length)
{
	if ((!message && length != 0) || length > std::numeric_limits<Size>::max() || level > 5)
	{
		return 1;
	}
	static const LogLevel levels[] = {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
	                                  LogLevel::Warn,  LogLevel::Error, LogLevel::Critical};
	const String text =
	    length == 0 ? String{} : String(reinterpret_cast<const char*>(message), static_cast<Size>(length));
	Log::Write(levels[level], "Script", text);
	return 0;
}

Int32 DotNetHost::ReadFileFromManaged(void* context, const UInt8* path, UInt64 pathLength, UInt8* output,
                                      UInt64 capacity, UInt64* required)
{
	auto* host = static_cast<DotNetHost*>(context);
	if (!host || !host->filesystem || !path || pathLength == 0 || pathLength > 4096 || (!output && capacity != 0) ||
	    !required)
	{
		return 1;
	}
	const auto parsed = VirtualPath::Parse(String(reinterpret_cast<const char*>(path), static_cast<Size>(pathLength)));
	if (!parsed)
	{
		return 2;
	}
	auto bytes = host->filesystem->Read(parsed.value);
	if (!bytes || bytes.value.size() > 256ull * 1024ull * 1024ull)
	{
		return 3;
	}
	*required = bytes.value.size();
	if (capacity < bytes.value.size())
	{
		return 4;
	}
	if (!bytes.value.empty())
	{
		std::memcpy(output, bytes.value.data(), bytes.value.size());
	}
	return 0;
}

Int32 DotNetHost::WriteFileFromManaged(void* context, const UInt8* path, UInt64 pathLength, const UInt8* bytes,
                                       UInt64 length)
{
	auto* host = static_cast<DotNetHost*>(context);
	if (!host || !host->filesystem || !path || pathLength == 0 || pathLength > 4096 || (!bytes && length != 0) ||
	    length > 256ull * 1024ull * 1024ull)
	{
		return 1;
	}
	const auto parsed = VirtualPath::Parse(String(reinterpret_cast<const char*>(path), static_cast<Size>(pathLength)));
	if (!parsed)
	{
		return 2;
	}
	Vector<UInt8> content;
	if (length != 0)
	{
		content.assign(bytes, bytes + static_cast<Size>(length));
	}
	return host->filesystem->WriteAtomic(parsed.value, content) ? 0 : 3;
}
} // namespace Ursa::Scripting
