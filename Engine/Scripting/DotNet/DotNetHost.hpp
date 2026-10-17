#pragma once

#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <Serialization/AssetReference.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Scripting/Bindings/BindingHostServices.hpp>
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

enum class ManagedEditorExtensionKind
{
	Menu,
	Window,
	Inspector
};

struct ManagedEditorExtension
{
	String id;
	ManagedEditorExtensionKind kind = ManagedEditorExtensionKind::Menu;
	String path;
	String target;
	String title;
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

class DotNetHost final : public Bindings::BindingContext, public Bindings::BindingHostServices
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

	Result<Vector<ManagedEditorExtension>> PrepareEditorExtensions(UInt64 sessionId);

	Result<bool> InvokeEditorExtension(UInt64 sessionId, const String& extensionId,
	                                   const Bindings::BindingModuleReference& capability);

	Result<String> PrepareScriptReload(UInt64 sessionId, const String& snapshots);
	Result<void> TransferScriptableAssets(UInt64 sourceSession, UInt64 candidateSession);
	Result<void> ConfigureRenderPipeline(UInt64 session, AssetReference pipeline = {});
	Result<void> RenderScene(UInt64 session, Float32 aspect);

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

	Result<void> BindModules(ManagedSceneApi* sceneApi);
	Result<void> WriteScriptLog(UInt32 level, const String& message) override;
	Result<void> ReportHostMessage(UInt32 level, const String& message) override;
	Result<Vector<UInt8>> ReadScriptFile(const String& path) override;
	Result<void> WriteScriptFile(const String& path, const Vector<UInt8>& bytes) override;

	LogCallback onLog;
	VirtualFileSystem* filesystem = nullptr;
	void* hostFxrLibrary = nullptr;
	void* bindingsEntryPoint = nullptr;
	struct BindingState;
	std::unique_ptr<BindingState> bindingState;
	bool sessionFaulted = false;
	bool initialized = false;
};
} // namespace Ursa::Scripting
