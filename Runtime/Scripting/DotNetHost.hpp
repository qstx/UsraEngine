#pragma once

#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <Scripting/NativeApi.hpp>
#include <Scripting/ManagedEditorApi.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Ursa::Scripting
{
class ManagedSceneApi;

enum class ManagedBehaviourEvent : UInt32
{
	Awake,
	OnEnable,
	Start,
	FixedUpdate,
	Update,
	LateUpdate,
	OnDisable,
	OnDestroy
};

struct ManagedHostFiles
{
	VirtualPath runtimeConfig;
	VirtualPath entryAssembly;
	Vector<VirtualPath> dependencies;
	Vector<VirtualPath> scriptAssemblies;
};

class ManagedHostBootstrap final
{
public:
	const VirtualPath& CacheDirectory() const
	{
		return cacheDirectory;
	}

private:
	friend class DotNetHost;
	friend Result<ManagedHostBootstrap> MaterializeManagedHostFiles(VirtualFileSystem&, const ManagedHostFiles&);

	std::filesystem::path runtimeConfigPath;
	std::filesystem::path entryAssemblyPath;
	VirtualPath cacheDirectory;
	String fingerprint;
};

Result<ManagedHostBootstrap> MaterializeManagedHostFiles(VirtualFileSystem& filesystem, const ManagedHostFiles& files);

class DotNetHost final
{
public:
	using LogCallback = std::function<void(UInt32, const String&)>;

	static Result<std::unique_ptr<DotNetHost>> Create();

	~DotNetHost();

	DotNetHost(const DotNetHost&) = delete;

	DotNetHost& operator=(const DotNetHost&) = delete;

	Result<void> Start(VirtualFileSystem& filesystem, const ManagedHostFiles& files, LogCallback logCallback,
	                   ManagedSceneApi* sceneApi = nullptr);

	Result<UInt64> CreateBehaviour(const String& typeName, const ManagedHandle& gameObject);

	Result<void> BindBehaviour(UInt64 instanceToken, const ManagedHandle& component);

	Result<void> BindSceneApi(ManagedSceneApi& sceneApi);

	Result<void> InvokeBehaviour(UInt64 instanceToken, ManagedBehaviourEvent event);

	Result<void> DestroyBehaviour(UInt64 instanceToken);

	Result<String> SerializeBehaviour(UInt64 instanceToken);

	Result<void> DeserializeBehaviour(UInt64 instanceToken, const String& payload);

	Result<String> PrepareEditorExtensions(UInt64 sessionId);

	Result<bool> InvokeEditorExtension(UInt64 sessionId, const String& extensionId, const ManagedEditorApi& api);

	Result<String> PrepareScriptReload(UInt64 sessionId, const String& snapshots);

	Result<void> LoadEditorApi(const VirtualPath& path);

	Result<void> LoadScriptContent(UInt64 sessionId, const VirtualPath& manifest);

	Result<void> LoadScriptAssembly(const Vector<UInt8>& assemblyBytes);

	Result<UInt64> CreateScriptSession(bool editor, ManagedSceneApi* sceneApi = nullptr);

	Result<void> SelectScriptSession(UInt64 sessionId);

	Result<void> BindScriptSession(UInt64 sessionId, ManagedSceneApi& sceneApi);

	Result<void> ValidateScriptSession(UInt64 sessionId);

	Result<void> CloseScriptSession(UInt64 sessionId);

	void SetFilesystem(VirtualFileSystem& value)
	{
		filesystem = &value;
	}

	bool IsSessionFaulted() const
	{
		return sessionFaulted;
	}

	bool IsInitialized() const
	{
		return initialized;
	}

private:
	DotNetHost() = default;

	static void LogFromManaged(void* context, UInt32 level, const UInt8* message, UInt64 length);

	static Int32 WriteLogFromManaged(void* context, UInt32 level, const UInt8* message, UInt64 length);

	static Int32 ReadFileFromManaged(void* context, const UInt8* path, UInt64 pathLength, UInt8* output,
	                                 UInt64 capacity, UInt64* required);

	static Int32 WriteFileFromManaged(void* context, const UInt8* path, UInt64 pathLength, const UInt8* bytes,
	                                  UInt64 length);

	NativeApiV4 BuildNativeApi(ManagedSceneApi* sceneApi);

#if defined(_WIN32)
	using CreateBehaviourFunction = Int32(__stdcall*)(const UInt8*, UInt64, const ManagedHandle*, UInt64*);
	using BindBehaviourFunction = Int32(__stdcall*)(UInt64, const ManagedHandle*);
	using InvokeBehaviourFunction = Int32(__stdcall*)(UInt64, UInt32);
	using DestroyBehaviourFunction = Int32(__stdcall*)(UInt64);
	using SerializeBehaviourFunction = Int32(__stdcall*)(UInt64, UInt8*, UInt64, UInt64*);
	using DeserializeBehaviourFunction = Int32(__stdcall*)(UInt64, const UInt8*, UInt64);
	using LoadAssemblyFunction = Int32(__stdcall*)(const UInt8*, UInt64);
	using InvokeEditorFunction = Int32(__stdcall*)(UInt64, const UInt8*, UInt64, const ManagedEditorApi*, Int32);
	using ReadSessionTextFunction = Int32(__stdcall*)(UInt64, UInt8*, UInt64, UInt64*);
	using LoadContentFunction = Int32(__stdcall*)(UInt64, const UInt8*, UInt64);
	using CreateSessionFunction = Int32(__stdcall*)(UInt32, const NativeApiV4*, Int32, UInt64*);
	using SessionFunction = Int32(__stdcall*)(UInt64);
	using BindSessionFunction = Int32(__stdcall*)(UInt64, const NativeApiV4*, Int32);
#else
	using CreateBehaviourFunction = Int32 (*)(const UInt8*, UInt64, const ManagedHandle*, UInt64*);
	using BindBehaviourFunction = Int32 (*)(UInt64, const ManagedHandle*);
	using InvokeBehaviourFunction = Int32 (*)(UInt64, UInt32);
	using DestroyBehaviourFunction = Int32 (*)(UInt64);
	using SerializeBehaviourFunction = Int32 (*)(UInt64, UInt8*, UInt64, UInt64*);
	using DeserializeBehaviourFunction = Int32 (*)(UInt64, const UInt8*, UInt64);
	using LoadAssemblyFunction = Int32 (*)(const UInt8*, UInt64);
	using InvokeEditorFunction = Int32 (*)(UInt64, const UInt8*, UInt64, const ManagedEditorApi*, Int32);
	using ReadSessionTextFunction = Int32 (*)(UInt64, UInt8*, UInt64, UInt64*);
	using LoadContentFunction = Int32 (*)(UInt64, const UInt8*, UInt64);
	using CreateSessionFunction = Int32 (*)(UInt32, const NativeApiV4*, Int32, UInt64*);
	using SessionFunction = Int32 (*)(UInt64);
	using BindSessionFunction = Int32 (*)(UInt64, const NativeApiV4*, Int32);
#endif

	LogCallback onLog;
	VirtualFileSystem* filesystem = nullptr;
	void* hostFxrLibrary = nullptr;
	void* managedEntryPoint = nullptr;
	CreateBehaviourFunction createBehaviour = nullptr;
	BindBehaviourFunction bindBehaviour = nullptr;
	InvokeBehaviourFunction invokeBehaviour = nullptr;
	DestroyBehaviourFunction destroyBehaviour = nullptr;
	SerializeBehaviourFunction serializeBehaviour = nullptr;
	DeserializeBehaviourFunction deserializeBehaviour = nullptr;
	LoadAssemblyFunction loadAssembly = nullptr;
	SessionFunction prepareExtensions = nullptr;
	ReadSessionTextFunction describeExtensions = nullptr;
	InvokeEditorFunction invokeExtension = nullptr;
	LoadContentFunction prepareReload = nullptr;
	ReadSessionTextFunction readReload = nullptr;
	LoadAssemblyFunction loadEditor = nullptr;
	LoadContentFunction loadContent = nullptr;
	CreateSessionFunction createSession = nullptr;
	SessionFunction selectSession = nullptr;
	BindSessionFunction bindSession = nullptr;
	SessionFunction validateSession = nullptr;
	SessionFunction closeSession = nullptr;
	bool sessionFaulted = false;
	bool initialized = false;
};
} // namespace Ursa::Scripting
