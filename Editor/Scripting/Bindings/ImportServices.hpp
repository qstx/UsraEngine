#pragma once

#include <BindingTypes.generated.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Vfs/VirtualPath.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/ModelImporter.hpp>
#include <Assets/AssetImportRules.hpp>
#include <functional>

namespace Ursa::Editor::Bindings
{
namespace B = Ursa::Scripting::Bindings;

enum class ImportPhase : UInt32
{
	ScriptedImport = 1,
	TexturePreprocess,
	TexturePostprocess,
	ModelPreprocess,
	ModelPostprocess
};

struct ImportTextureCandidate
{
	UInt32 width = 0;
	UInt32 height = 0;
	bool linear = false;
	Vector<UInt8> pixels;
	Result<MaterialTexturePtr> Texture() const;
};

struct ImportObjectRegistration
{
	String key;
	UInt64 token = 0;
	UInt32 kind = 0;
	UInt64 instance = 0;
	Scripting::ManagedHandle handle;
};

struct ImportMaterialTextureBinding
{
	Scripting::ManagedHandle material;
	Scripting::ManagedHandle texture;
	AssetReference reference;
	Int32 slot = 0;
	String name;
};

struct ImportArtifactDependency
{
	AssetReference reference;
	String contentFingerprint;
};

// One candidate transaction. Capability tokens and object handles never survive this scope.
class ImportInvocation final : public B::BindingContext
{
public:
	ImportInvocation(UInt64 scriptSession, UInt64 ruleGeneration, VirtualPath sourcePath, ImportPhase stage,
	                 std::function<UInt64()> currentGeneration);
	~ImportInvocation() override;
	void Require(UInt32 policy) const override;
	void RequireLease(UInt32 policy) const;
	void RequireCapability(UInt32 policy) const;
	void BeginManaged();
	void EndManaged();
	void SetSourceBytes(Vector<UInt8> bytes);
	const Vector<UInt8>& SourceBytes() const;
	void AddOutput(ImportObjectRegistration output);
	void SetMainObject(UInt64 token);
	const Map<String, ImportObjectRegistration>& Outputs() const;
	UInt64 MainObject() const;
	void ConfigureDependencies(AssetDatabase& database, const AssetImportRules& rules, ResourceId sourceGuid,
	                           String ownerType);
	const nlohmann::json& ImporterSettings() const;
	Vector<UInt8> ReadSourceAsset(const VirtualPath& path);
	Result<Vector<UInt8>> ReadArtifact(AssetReference reference, bool track = true);
	void LogDiagnostic(UInt32 severity, const String& message);
	String DiagnosticPrefix() const;
	void ValidateReads() const;
	void SetOwnerType(String type);
	void ConfigureTextureSettings(TextureImportSettings settings);
	TextureImportSettings EffectiveTextureSettings() const;
	void UpdateTextureSettings(TextureImportSettings settings);
	void ConfigureModelSettings(ModelImportSettings settings);
	ModelImportSettings EffectiveModelSettings() const;
	void UpdateModelSettings(ModelImportSettings settings);
	void SetPostprocessObject(Scripting::ManagedHandle handle);
	Scripting::ManagedHandle PostprocessObject() const;

	const Vector<VirtualPath>& SourceDependencies() const
	{
		return sourceDependencies;
	}

	const Vector<ImportArtifactDependency>& ArtifactDependencies() const
	{
		return artifactDependencies;
	}

	const Vector<ArtifactDependencyRequest>& PendingArtifacts() const
	{
		return pendingArtifacts;
	}

	const Vector<String>& Diagnostics() const
	{
		return diagnostics;
	}

	bool HasErrors() const
	{
		return hasErrors;
	}

	UInt64 SessionIdentity() const override;
	void RequireInvocation(UInt64 scriptSession, UInt64 ruleGeneration, UInt64 invocation) const;
	void SetPhase(ImportPhase stage);
	Scripting::ManagedHandle AddCandidate(std::shared_ptr<void> object, Scripting::ManagedHandleKind kind);
	std::shared_ptr<void> Candidate(const Scripting::ManagedHandle& handle) const;
	void RemoveCandidate(const Scripting::ManagedHandle& handle);
	void Finish();
	void SetMaterialTexture(ImportMaterialTextureBinding binding);
	void ResolveMaterialTextures(ResourceId guid, const String& importerIdentity);
	Scripting::ManagedSceneApi& CpuApi();
	Scene& SceneGraph();

	UInt64 Identity() const
	{
		return identity;
	}

	UInt64 Generation() const
	{
		return generation;
	}

	ImportPhase Phase() const
	{
		return phase;
	}

	const VirtualPath& SourcePath() const
	{
		return source;
	}

private:
	AssetDatabase& DependencyDatabase(ResourceId guid);
	UInt64 session;
	UInt64 generation;
	UInt64 identity;
	VirtualPath source;
	ImportPhase phase;
	std::function<UInt64()> currentGeneration;
	std::unique_ptr<Scripting::ManagedHandleRegistry> handles;
	Map<UInt32, std::shared_ptr<void>> candidates;
	Scene scene;
	std::unique_ptr<ResourceSession> artifactResources;
	std::unique_ptr<Scripting::ManagedSceneApi> cpuApi;
	bool active = true;
	bool managedStarted = false;
	bool managedCompleted = false;
	bool sourceConfigured = false;
	Vector<UInt8> sourceBytes;
	Map<String, ImportObjectRegistration> outputs;
	UInt64 mainObject = 0;
	AssetDatabase* database = nullptr;
	nlohmann::json importerSettings = nlohmann::json::object();
	Map<String, std::unique_ptr<AssetDatabase>> dependencyDatabases;
	const AssetImportRules* rules = nullptr;
	ResourceId sourceGuid;
	String ownerType;
	Vector<VirtualPath> sourceDependencies;
	Vector<ImportArtifactDependency> artifactDependencies;
	Vector<ImportMaterialTextureBinding> materialTextures;
	Vector<ArtifactDependencyRequest> pendingArtifacts;
	Set<String> readingArtifacts;
	Vector<String> diagnostics;
	Map<String, String> readFingerprints;
	bool hasErrors = false;
	std::optional<TextureImportSettings> textureSettings;
	std::optional<ModelImportSettings> modelSettings;
	Scripting::ManagedHandle postprocessObject;
};

struct ImportServices
{
	static Result<void> SetMaterialTexture(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& material,
	                                       const B::Wire::Value_Ursa_NativeHandle& texture,
	                                       const B::Wire::Value_Ursa_NativeAssetReference_& reference, Int32 slot,
	                                       const B::Utf8View& name);
	static Result<B::Utf8View> GetImporterSettings(B::BindingCall& call);
	static ImportInvocation& Scope(B::BindingCall& call);
	static Result<B::Wire::Value_Ursa_ImportScopeValue> GetScope(B::BindingCall& call);
	static Result<void> RequireInvocation(B::BindingCall& call, UInt64 session, UInt64 generation, UInt64 invocation);
	static Result<void> BeginInvocation(B::BindingCall& call);
	static Result<void> EndInvocation(B::BindingCall& call);
	static Result<B::DataView> ReadSourceBytes(B::BindingCall& call);
	static Result<void> AddObject(B::BindingCall& call, const B::Utf8View& key,
	                              const B::Wire::Value_Ursa_ImportReferenceInfo& reference);
	static Result<void> SetMainObject(B::BindingCall& call, UInt64 token);
	static Result<B::DataView> ReadSourceAsset(B::BindingCall& call, const B::Utf8View& path);
	static Result<void> DependsOnSourceAsset(B::BindingCall& call, const B::Utf8View& path);
	static Result<B::DataView> ReadArtifact(B::BindingCall& call,
	                                        const B::Wire::Value_Ursa_NativeAssetReference& reference);
	static Result<void> DependsOnArtifact(B::BindingCall& call,
	                                      const B::Wire::Value_Ursa_NativeAssetReference& reference);
	static Result<void> LogDiagnostic(B::BindingCall& call, UInt32 severity, const B::Utf8View& message);
	static Result<B::Wire::Value_Ursa_TextureImporterSettingsValue> GetTextureSettings(B::BindingCall& call);
	static Result<void> SetTextureSettings(B::BindingCall& call,
	                                       const B::Wire::Value_Ursa_TextureImporterSettingsValue& settings);
	static Result<B::Wire::Value_Ursa_NativeHandle> GetPostprocessObject(B::BindingCall& call);
	static Result<B::Wire::Value_Ursa_ModelImporterSettingsValue> GetModelSettings(B::BindingCall& call);
	static Result<void> SetModelSettings(B::BindingCall& call,
	                                     const B::Wire::Value_Ursa_ModelImporterSettingsValue& settings);
	static Result<void> RequireCandidate(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& candidate);
	static Result<B::Wire::Value_Ursa_NativeHandle> CreateTexture2D(B::BindingCall& call, UInt32 width, UInt32 height,
	                                                                bool linear);
	static Result<B::Wire::Value_Ursa_Texture2DInfo> GetTexture2DInfo(B::BindingCall& call,
	                                                                  const B::Wire::Value_Ursa_NativeHandle& texture);
	static Result<B::DataView> GetTexture2DPixels(B::BindingCall& call,
	                                              const B::Wire::Value_Ursa_NativeHandle& texture);
	static Result<void> SetTexture2DPixels(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& texture,
	                                       const B::DataView& pixels);
};
} // namespace Ursa::Editor::Bindings
