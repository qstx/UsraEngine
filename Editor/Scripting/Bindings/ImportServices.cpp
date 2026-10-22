#include <Scripting/Bindings/ImportServices.hpp>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <stdexcept>

namespace Ursa::Editor::Bindings
{
namespace
{
std::atomic<UInt64> nextInvocation{1};

class ImportSceneApi final : public Scripting::ManagedSceneApi
{
public:
	bool PersistentResourcesReadOnly() const override
	{
		return true;
	}

	bool CpuBuiltinShaders() const override
	{
		return true;
	}

	ImportSceneApi(Scene& scene, ImportInvocation& invocation) : ManagedSceneApi(scene), invocation(invocation)
	{
	}

	void Require(UInt32 policy) const override
	{
		invocation.RequireLease(policy);
		ManagedSceneApi::Require(policy);
	}

	void RequireResourceAccess() const override
	{
		invocation.RequireCapability(0);
	}

	void RequireGameExecution() const override
	{
		Require(0);
		throw std::runtime_error("Import callbacks cannot use game, Play, window or untracked file operations");
	}

	void RequireRendering() const override
	{
		Require(0);
		throw std::runtime_error("Import callbacks cannot render or allocate GPU resources");
	}

private:
	ImportInvocation& invocation;
};

void RequirePhase(ImportPhase phase)
{
	if (phase < ImportPhase::ScriptedImport || phase > ImportPhase::ModelPostprocess)
	{
		throw std::runtime_error("Invalid import callback phase");
	}
}
} // namespace

ImportInvocation::ImportInvocation(UInt64 scriptSession, UInt64 ruleGeneration, VirtualPath sourcePath,
                                   ImportPhase stage, std::function<UInt64()> generationSource)
    : session(scriptSession), generation(ruleGeneration), identity(nextInvocation.fetch_add(1)),
      source(std::move(sourcePath)), phase(stage), currentGeneration(std::move(generationSource))
{
	RequirePhase(phase);
	if (!session || !generation || !identity || source.relative.empty() || !currentGeneration)
	{
		throw std::runtime_error("Import scope requires a session, generation, source and generation provider");
	}
	auto registry = Scripting::ManagedHandleRegistry::Create();
	if (!registry)
	{
		throw std::runtime_error(registry.error.c_str());
	}
	handles = std::move(registry.value);
	cpuApi = std::make_unique<ImportSceneApi>(scene, *this);
	Require(0);
}

ImportInvocation::~ImportInvocation()
{
	Finish();
}

void ImportInvocation::Require(UInt32 policy) const
{
	RequireLease(policy);
	if (currentGeneration() != generation)
	{
		throw std::runtime_error("Import scope is closed, obsolete or called outside its owner thread");
	}
}

void ImportInvocation::RequireLease(UInt32 policy) const
{
	B::BindingContext::Require(policy);
	if (policy != 0 || !active)
	{
		throw std::runtime_error("Import context is closed or called outside its owner thread");
	}
}

void ImportInvocation::RequireCapability(UInt32 policy) const
{
	Require(policy);
	if (managedCompleted)
	{
		throw std::runtime_error("Import managed invocation has already completed");
	}
}

void ImportInvocation::BeginManaged()
{
	RequireCapability(0);
	if (managedStarted)
	{
		throw std::runtime_error("Import invocation cannot be reused or reentered");
	}
	managedStarted = true;
}

void ImportInvocation::EndManaged()
{
	RequireCapability(0);
	if (!managedStarted)
	{
		throw std::runtime_error("Import managed invocation has not begun");
	}
	managedCompleted = true;
}

void ImportInvocation::SetSourceBytes(Vector<UInt8> bytes)
{
	Require(0);
	if (managedStarted || bytes.size() > B::maximumBytes)
	{
		throw std::runtime_error("Import source snapshot is already in use or exceeds its budget");
	}
	sourceBytes = std::move(bytes);
	sourceConfigured = true;
}

const Vector<UInt8>& ImportInvocation::SourceBytes() const
{
	RequireCapability(0);
	if (!sourceConfigured)
	{
		throw std::runtime_error("Import source snapshot has not been configured");
	}
	return sourceBytes;
}

void ImportInvocation::AddOutput(ImportObjectRegistration output)
{
	RequireCapability(0);
	if (phase != ImportPhase::ScriptedImport || output.key.empty() || output.key.size() > 1024 ||
	    output.key.find('\0') != String::npos || !output.token || output.kind < 1 || output.kind > 5)
	{
		throw std::runtime_error("Invalid import output key, object or registration phase");
	}
	if (output.kind == 1)
	{
		if (!output.instance || output.handle.sessionId || output.handle.index || output.handle.generation ||
		    static_cast<UInt32>(output.handle.kind) != 0 || output.handle.reserved)
		{
			throw std::runtime_error("Invalid ScriptableObject output identity");
		}
	}
	else
	{
		const auto expected = output.kind == 2   ? Scripting::ManagedHandleKind::Mesh
		                      : output.kind == 3 ? Scripting::ManagedHandleKind::Material
		                      : output.kind == 4 ? Scripting::ManagedHandleKind::Texture2D
		                                         : Scripting::ManagedHandleKind::GameObject;
		if (output.handle.kind != expected || output.instance)
		{
			throw std::runtime_error("Import output kind differs from its candidate");
		}
		Candidate(output.handle);
	}
	for (const auto& pair : outputs)
	{
		const auto& other = pair.second;
		if (other.key == output.key || other.token == output.token ||
		    (other.kind == output.kind && (output.kind == 1 ? other.instance == output.instance
		                                                    : other.handle.sessionId == output.handle.sessionId &&
		                                                          other.handle.index == output.handle.index &&
		                                                          other.handle.generation == output.handle.generation)))
		{
			throw std::runtime_error("Duplicate import output key or object: " + ToStdString(output.key));
		}
	}
	outputs.emplace(output.key, std::move(output));
}

void ImportInvocation::SetMainObject(UInt64 token)
{
	RequireCapability(0);
	if (phase != ImportPhase::ScriptedImport || !token ||
	    std::none_of(outputs.begin(), outputs.end(), [&](const auto& entry) { return entry.second.token == token; }))
	{
		throw std::runtime_error("Import main object must be registered in the scripted import phase");
	}
	mainObject = token;
}

const Map<String, ImportObjectRegistration>& ImportInvocation::Outputs() const
{
	Require(0);
	return outputs;
}

UInt64 ImportInvocation::MainObject() const
{
	Require(0);
	return mainObject;
}

void ImportInvocation::ConfigureDependencies(AssetDatabase& selectedDatabase, const AssetImportRules& selectedRules,
                                             ResourceId guid, String type)
{
	RequireCapability(0);
	if (managedStarted || database || !guid)
	{
		throw std::runtime_error("Import dependency context is already configured or invalid");
	}
	database = &selectedDatabase;
	importerSettings = B::TakeBindingResult(selectedDatabase.Metadata(source)).importSettings;
	rules = &selectedRules;
	sourceGuid = guid;
	ownerType = std::move(type);
	cpuApi->SetResourceProvider(
	    [this]() -> ResourceSession*
	    {
		    if (!artifactResources)
		    {
			    artifactResources = B::TakeBindingResult(ResourceSession::Open(
			        database->Filesystem(),
			        [this](ResourceId resource, UInt64 localFileId) -> Result<VirtualPath>
			        {
				        const auto completed = ReadArtifact({resource, localFileId});
				        if (!completed)
				        {
					        return Result<VirtualPath>::Failure(completed.error);
				        }
				        const auto artifact =
				            DependencyDatabase(resource).ResolveImportedArtifact(resource, localFileId);
				        return artifact ? Result<VirtualPath>::Success(artifact.value.path)
				                        : Result<VirtualPath>::Failure(artifact.error);
			        }));
			    artifactResources->RequireImportedArtifacts();
		    }
		    return artifactResources.get();
	    });
}

const nlohmann::json& ImportInvocation::ImporterSettings() const
{
	RequireCapability(0);
	return importerSettings;
}

Result<B::Utf8View> ImportServices::GetImporterSettings(B::BindingCall& call)
{
	return Result<B::Utf8View>::Success(call.arena.StringValue(ToUrsaString(Scope(call).ImporterSettings().dump())));
}

void ImportInvocation::LogDiagnostic(UInt32 severity, const String& message)
{
	RequireCapability(0);
	if (severity != 1 && severity != 2)
	{
		throw std::runtime_error("Invalid import diagnostic severity");
	}
	hasErrors = hasErrors || severity == 2;
	diagnostics.push_back(DiagnosticPrefix() + (severity == 2 ? "error: " : "warning: ") + message);
}

String ImportInvocation::DiagnosticPrefix() const
{
	RequireLease(0);
	const char* name = phase == ImportPhase::ScriptedImport       ? "ScriptedImport"
	                   : phase == ImportPhase::TexturePreprocess  ? "OnPreprocessTexture"
	                   : phase == ImportPhase::TexturePostprocess ? "OnPostprocessTexture"
	                   : phase == ImportPhase::ModelPreprocess    ? "OnPreprocessModel"
	                                                              : "OnPostprocessModel";
	return source.ToString() + " [" + ownerType + ", " + name + "]: ";
}

Vector<UInt8> ImportInvocation::ReadSourceAsset(const VirtualPath& path)
{
	RequireCapability(0);
	if (!database || path.scheme == editorLibraryScheme || path.scheme == userDataScheme || path.relative.empty())
	{
		throw std::runtime_error("Source dependencies require an asset path; use artifact reads for imported content");
	}
	auto bytes = database->Filesystem().Read(path);
	if (!bytes)
	{
		LogDiagnostic(2, "Missing source dependency " + path.ToString() + ": " + bytes.error);
		throw std::runtime_error(diagnostics.back().c_str());
	}
	const String fingerprint = BuildResourceFingerprint(bytes.value);
	const auto inserted = readFingerprints.emplace(path.ToString(), fingerprint);
	if (!inserted.second && inserted.first->second != fingerprint)
	{
		LogDiagnostic(2, "Source dependency changed during import: " + path.ToString());
		throw std::runtime_error(diagnostics.back().c_str());
	}
	if (path != source &&
	    std::find(sourceDependencies.begin(), sourceDependencies.end(), path) == sourceDependencies.end())
	{
		sourceDependencies.push_back(path);
	}
	return std::move(bytes.value);
}

AssetDatabase& ImportInvocation::DependencyDatabase(ResourceId guid)
{
	if (!database || !guid)
	{
		throw std::runtime_error("Artifact dependencies require a configured context and nonzero GUID");
	}
	if (database->Contains(guid))
	{
		return *database;
	}
	for (const auto& scheme : database->Filesystem().Schemes())
	{
		if (scheme == database->SourceScheme() || scheme == editorLibraryScheme || scheme == userDataScheme ||
		    scheme == projectSettingsScheme || scheme == "importframework" || scheme.find("framework") == 0)
		{
			continue;
		}
		auto& other = dependencyDatabases[scheme];
		if (!other)
		{
			other = B::TakeBindingResult(AssetDatabase::Open(database->Filesystem(), scheme));
			B::CompleteBindingResult(other->Refresh());
		}
		if (other->Contains(guid))
		{
			return *other;
		}
	}
	throw std::runtime_error("Missing artifact dependency GUID: " + ToStdString(guid.ToString()));
}

Result<Vector<UInt8>> ImportInvocation::ReadArtifact(AssetReference reference, bool track)
{
	RequireCapability(0);
	try
	{
		if (reference.guid == sourceGuid || !readingArtifacts.insert(reference.guid.ToString()).second)
		{
			throw std::runtime_error("Cyclic artifact dependency on current source");
		}

		struct EndRead
		{
			Set<String>& values;
			String guid;

			~EndRead()
			{
				values.erase(guid);
			}
		} endRead{readingArtifacts, reference.guid.ToString()};

		auto& owner = DependencyDatabase(reference.guid);
		const auto path = B::TakeBindingResult(owner.SourcePath(reference.guid));
		const auto status = B::TakeBindingResult(owner.CurrentImport(path));
		const String extension =
		    ToUrsaString(std::filesystem::u8path(ToStdString(path.relative)).extension().u8string());
		const auto metadata = B::TakeBindingResult(owner.Metadata(path));
		const auto importer = B::TakeBindingResult(
		    rules->FindImporter(extension, metadata.importer.find("script:") == 0 ? metadata.importer : String{}));
		bool ready =
		    status.upToDate && status.importer == importer->identity && status.importerVersion == importer->version;
		if (ready && (importer->scriptType || !rules->PostprocessorsFor(importer->identity, path.scheme).empty()))
		{
			ready =
			    status.settings.value("rules", std::string{}) ==
			    ToStdString(B::TakeBindingResult(rules->FingerprintFor(extension, path.scheme, importer->identity)));
			if (ready)
			{
				for (const auto& entry : status.settings.value("artifacts", nlohmann::json::array()))
				{
					const auto nested =
					    B::TakeBindingResult(AssetReferenceFromJson(entry.at("reference"), "artifact dependency"));
					const auto completed = ReadArtifact(nested, false);
					if (!completed)
					{
						ready = false;
						break;
					}
					const auto recorded =
					    DependencyDatabase(nested.guid).ResolveImportedArtifact(nested.guid, nested.localFileId);
					if (!recorded ||
					    recorded.value.contentFingerprint != ToUrsaString(entry.at("fingerprint").get<std::string>()))
					{
						ready = false;
						break;
					}
				}
			}
		}
		if (!ready)
		{
			if (std::none_of(pendingArtifacts.begin(), pendingArtifacts.end(),
			                 [&](const auto& item) { return item.source == path; }))
			{
				pendingArtifacts.push_back({path, reference});
			}
			return Result<Vector<UInt8>>::Failure("Artifact dependency requires a completed current import: " +
			                                      path.ToString());
		}
		const auto artifact =
		    B::TakeBindingResult(owner.ResolveImportedArtifact(reference.guid, reference.localFileId));
		const auto found = std::find_if(
		    artifactDependencies.begin(), artifactDependencies.end(), [&](const auto& item)
		    { return item.reference.guid == reference.guid && item.reference.localFileId == reference.localFileId; });
		if (track && found == artifactDependencies.end())
		{
			artifactDependencies.push_back({reference, artifact.contentFingerprint});
		}
		else if (track && found->contentFingerprint != artifact.contentFingerprint)
		{
			throw std::runtime_error("Artifact dependency changed during import");
		}
		return owner.Filesystem().Read(artifact.path);
	}
	catch (const std::exception& error)
	{
		LogDiagnostic(2, ToUrsaString(error.what()));
		return Result<Vector<UInt8>>::Failure(diagnostics.back());
	}
}

void ImportInvocation::ValidateReads() const
{
	Require(0);
	for (const auto& entry : readFingerprints)
	{
		const auto bytes = database->Filesystem().Read(B::TakeBindingResult(VirtualPath::Parse(entry.first)));
		if (!bytes || BuildResourceFingerprint(bytes.value) != entry.second)
		{
			throw std::runtime_error("Source dependency changed before publication: " + ToStdString(entry.first));
		}
	}
	for (const auto& dependency : artifactDependencies)
	{
		auto& owner = const_cast<ImportInvocation*>(this)->DependencyDatabase(dependency.reference.guid);
		const auto artifact =
		    owner.ResolveImportedArtifact(dependency.reference.guid, dependency.reference.localFileId);
		if (!artifact || artifact.value.contentFingerprint != dependency.contentFingerprint)
		{
			throw std::runtime_error("Artifact dependency changed before publication");
		}
	}
}

void ImportInvocation::SetOwnerType(String type)
{
	RequireCapability(0);
	ownerType = std::move(type);
}

void ImportInvocation::ConfigureTextureSettings(TextureImportSettings settings)
{
	RequireCapability(0);
	if (managedStarted || textureSettings)
	{
		throw std::runtime_error("Texture settings are already in use");
	}
	textureSettings = std::move(settings);
}

TextureImportSettings ImportInvocation::EffectiveTextureSettings() const
{
	RequireCapability(0);
	if (!textureSettings || (phase != ImportPhase::TexturePreprocess && phase != ImportPhase::TexturePostprocess))
	{
		throw std::runtime_error("Texture settings require a texture callback");
	}
	return *textureSettings;
}

void ImportInvocation::UpdateTextureSettings(TextureImportSettings settings)
{
	EffectiveTextureSettings();
	if (phase != ImportPhase::TexturePreprocess)
	{
		throw std::runtime_error("Effective settings can only change during preprocessing");
	}
	if (static_cast<UInt32>(settings.type) > 2 || static_cast<UInt32>(settings.filter) > 1 ||
	    static_cast<UInt32>(settings.wrap) > 1)
	{
		throw std::runtime_error("Invalid texture settings enumeration");
	}
	B::TakeBindingResult(TextureImportSettings::FromJson(settings.ToJson()));
	textureSettings = std::move(settings);
}

void ImportInvocation::SetPostprocessObject(Scripting::ManagedHandle handle)
{
	RequireCapability(0);
	Candidate(handle);
	postprocessObject = handle;
}

void ImportInvocation::ConfigureModelSettings(ModelImportSettings settings)
{
	RequireCapability(0);
	if (managedStarted || modelSettings)
	{
		throw std::runtime_error("Model settings are already in use");
	}
	modelSettings = std::move(settings);
}

ModelImportSettings ImportInvocation::EffectiveModelSettings() const
{
	RequireCapability(0);
	if (!modelSettings || (phase != ImportPhase::ModelPreprocess && phase != ImportPhase::ModelPostprocess))
	{
		throw std::runtime_error("Model settings require a model callback");
	}
	return *modelSettings;
}

void ImportInvocation::UpdateModelSettings(ModelImportSettings settings)
{
	EffectiveModelSettings();
	if (phase != ImportPhase::ModelPreprocess)
	{
		throw std::runtime_error("Effective settings can only change during preprocessing");
	}
	B::TakeBindingResult(ModelImportSettings::FromJson(settings.ToJson()));
	modelSettings = std::move(settings);
}

Scripting::ManagedHandle ImportInvocation::PostprocessObject() const
{
	RequireCapability(0);
	const auto kind = phase == ImportPhase::TexturePostprocess ? Scripting::ManagedHandleKind::Texture2D
	                                                           : Scripting::ManagedHandleKind::GameObject;
	if ((phase != ImportPhase::TexturePostprocess && phase != ImportPhase::ModelPostprocess) ||
	    postprocessObject.kind != kind)
	{
		throw std::runtime_error("Postprocess candidate is unavailable in this phase");
	}
	Candidate(postprocessObject);
	return postprocessObject;
}

UInt64 ImportInvocation::SessionIdentity() const
{
	return session;
}

void ImportInvocation::RequireInvocation(UInt64 scriptSession, UInt64 ruleGeneration, UInt64 invocation) const
{
	Require(0);
	if (session != scriptSession || generation != ruleGeneration || identity != invocation)
	{
		throw std::runtime_error("Import invocation belongs to another session or generation");
	}
}

void ImportInvocation::SetPhase(ImportPhase stage)
{
	Require(0);
	RequirePhase(stage);
	phase = stage;
}

Scripting::ManagedHandle ImportInvocation::AddCandidate(std::shared_ptr<void> object, Scripting::ManagedHandleKind kind)
{
	Require(0);
	if (!object)
	{
		throw std::runtime_error("An import candidate cannot be null");
	}
	auto handle = handles->Register(object.get(), kind);
	if (!handle)
	{
		throw std::runtime_error(handle.error.c_str());
	}
	candidates[handle.value.index] = std::move(object);
	return handle.value;
}

std::shared_ptr<void> ImportInvocation::Candidate(const Scripting::ManagedHandle& handle) const
{
	Require(0);
	auto found = handles->Resolve(handle, handle.kind);
	if (!found)
	{
		return B::TakeBindingResult(cpuApi->CandidateResource(handle));
	}
	return candidates.at(handle.index);
}

void ImportInvocation::RemoveCandidate(const Scripting::ManagedHandle& handle)
{
	Candidate(handle);
	auto invalidated = handles->Invalidate(handle);
	if (!invalidated)
	{
		throw std::runtime_error(invalidated.error.c_str());
	}
	candidates.erase(handle.index);
}

void ImportInvocation::Finish()
{
	if (active)
	{
		Deactivate();
		cpuApi.reset();
		handles->InvalidateAll();
		candidates.clear();
		active = false;
	}
}

Scripting::ManagedSceneApi& ImportInvocation::CpuApi()
{
	Require(0);
	return *cpuApi;
}

Scene& ImportInvocation::SceneGraph()
{
	Require(0);
	return scene;
}

Result<MaterialTexturePtr> ImportTextureCandidate::Texture() const
{
	return MaterialTexture::Create(width, height, linear ? TextureColorSpace::Linear : TextureColorSpace::Srgb, pixels);
}

ImportInvocation& ImportServices::Scope(B::BindingCall& call)
{
	auto* scope = dynamic_cast<ImportInvocation*>(&call.context);
	if (!scope)
	{
		throw std::runtime_error("Import bindings require a candidate transaction");
	}
	scope->RequireCapability(0);
	return *scope;
}

Result<B::Wire::Value_Ursa_ImportScopeValue> ImportServices::GetScope(B::BindingCall& call)
{
	auto& scope = Scope(call);
	return Result<B::Wire::Value_Ursa_ImportScopeValue>::Success(
	    {scope.SessionIdentity(), scope.Generation(), scope.Identity(), static_cast<UInt32>(scope.Phase()),
	     call.arena.StringValue(scope.SourcePath().ToString())});
}

Result<void> ImportServices::RequireInvocation(B::BindingCall& call, UInt64 session, UInt64 generation,
                                               UInt64 invocation)
{
	Scope(call).RequireInvocation(session, generation, invocation);
	return {};
}

Result<void> ImportServices::BeginInvocation(B::BindingCall& call)
{
	Scope(call).BeginManaged();
	return {};
}

Result<void> ImportServices::EndInvocation(B::BindingCall& call)
{
	Scope(call).EndManaged();
	return {};
}

Result<B::DataView> ImportServices::ReadSourceBytes(B::BindingCall& call)
{
	const auto& bytes = Scope(call).SourceBytes();
	return Result<B::DataView>::Success(call.arena.Array(bytes.data(), bytes.size()));
}

Result<void> ImportServices::AddObject(B::BindingCall& call, const B::Utf8View& key,
                                       const B::Wire::Value_Ursa_ImportReferenceInfo& reference)
{
	Scope(call).AddOutput({B::ReadString(key), reference.F_Token, reference.F_Kind, reference.F_Instance,
	                       B::ReadManagedHandle(reference.F_Handle)});
	return {};
}

Result<void> ImportServices::SetMainObject(B::BindingCall& call, UInt64 token)
{
	Scope(call).SetMainObject(token);
	return {};
}

Result<B::DataView> ImportServices::ReadSourceAsset(B::BindingCall& call, const B::Utf8View& path)
{
	const auto bytes = Scope(call).ReadSourceAsset(B::TakeBindingResult(VirtualPath::Parse(B::ReadString(path))));
	return Result<B::DataView>::Success(call.arena.Array(bytes.data(), bytes.size()));
}

Result<void> ImportServices::DependsOnSourceAsset(B::BindingCall& call, const B::Utf8View& path)
{
	Scope(call).ReadSourceAsset(B::TakeBindingResult(VirtualPath::Parse(B::ReadString(path))));
	return {};
}

Result<B::DataView> ImportServices::ReadArtifact(B::BindingCall& call,
                                                 const B::Wire::Value_Ursa_NativeAssetReference& reference)
{
	const auto bytes = Scope(call).ReadArtifact(B::ReadAssetValue(reference));
	return bytes ? Result<B::DataView>::Success(call.arena.Array(bytes.value.data(), bytes.value.size()))
	             : Result<B::DataView>::Failure(bytes.error);
}

Result<void> ImportServices::DependsOnArtifact(B::BindingCall& call,
                                               const B::Wire::Value_Ursa_NativeAssetReference& reference)
{
	const auto bytes = Scope(call).ReadArtifact(B::ReadAssetValue(reference));
	return bytes ? Result<void>{} : Result<void>::Failure(bytes.error);
}

Result<void> ImportServices::LogDiagnostic(B::BindingCall& call, UInt32 severity, const B::Utf8View& message)
{
	Scope(call).LogDiagnostic(severity, B::ReadString(message));
	return {};
}

Result<B::Wire::Value_Ursa_TextureImporterSettingsValue> ImportServices::GetTextureSettings(B::BindingCall& call)
{
	const auto settings = Scope(call).EffectiveTextureSettings();
	return Result<B::Wire::Value_Ursa_TextureImporterSettingsValue>::Success(
	    {static_cast<UInt32>(settings.type), settings.sRgb, settings.alpha, settings.generateMipmaps,
	     static_cast<UInt32>(settings.filter), static_cast<UInt32>(settings.wrap), settings.maxDimension});
}

Result<void> ImportServices::SetTextureSettings(B::BindingCall& call,
                                                const B::Wire::Value_Ursa_TextureImporterSettingsValue& value)
{
	Scope(call).UpdateTextureSettings({static_cast<TextureImportType>(value.F_Type), value.F_SRgb != 0,
	                                   value.F_Alpha != 0, value.F_GenerateMipmaps != 0,
	                                   static_cast<TextureFilterMode>(value.F_Filter),
	                                   static_cast<TextureWrapMode>(value.F_Wrap), value.F_MaxDimension});
	return {};
}

Result<B::Wire::Value_Ursa_NativeHandle> ImportServices::GetPostprocessObject(B::BindingCall& call)
{
	return Result<B::Wire::Value_Ursa_NativeHandle>::Success(
	    B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(Scope(call).PostprocessObject()));
}

Result<B::Wire::Value_Ursa_ModelImporterSettingsValue> ImportServices::GetModelSettings(B::BindingCall& call)
{
	const auto settings = Scope(call).EffectiveModelSettings();
	return Result<B::Wire::Value_Ursa_ModelImporterSettingsValue>::Success(
	    {settings.scaleFactor, settings.generateNormals, settings.generateTangents, settings.flipUvs,
	     settings.importMaterials, settings.importTextures, settings.maxBoneInfluences});
}

Result<void> ImportServices::SetModelSettings(B::BindingCall& call,
                                              const B::Wire::Value_Ursa_ModelImporterSettingsValue& value)
{
	Scope(call).UpdateModelSettings({value.F_ScaleFactor, value.F_GenerateNormals != 0, value.F_GenerateTangents != 0,
	                                 value.F_FlipUvs != 0, value.F_ImportMaterials != 0, value.F_ImportTextures != 0,
	                                 value.F_MaxBoneInfluences});
	return {};
}

Result<void> ImportServices::RequireCandidate(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& candidate)
{
	Scope(call).Candidate(B::ReadManagedHandle(candidate));
	return {};
}

namespace
{
std::shared_ptr<ImportTextureCandidate> TextureCandidate(B::BindingCall& call,
                                                         const B::Wire::Value_Ursa_NativeHandle& texture)
{
	if (texture.F_kind != static_cast<UInt32>(Scripting::ManagedHandleKind::Texture2D))
	{
		throw std::runtime_error("Import object is not a Texture2D");
	}
	return std::static_pointer_cast<ImportTextureCandidate>(
	    ImportServices::Scope(call).Candidate(B::ReadManagedHandle(texture)));
}
} // namespace

Result<B::Wire::Value_Ursa_NativeHandle> ImportServices::CreateTexture2D(B::BindingCall& call, UInt32 width,
                                                                         UInt32 height, bool linear)
{
	constexpr UInt64 maximumPixels = B::maximumBytes / 16;
	if (!width || !height || UInt64(width) * height > maximumPixels)
	{
		return Result<B::Wire::Value_Ursa_NativeHandle>::Failure(
		    "Texture2D dimensions are empty or exceed the transfer budget");
	}
	auto texture = std::make_shared<ImportTextureCandidate>();
	texture->width = width;
	texture->height = height;
	texture->linear = linear;
	texture->pixels.resize(Size(width) * height * 4, 0);
	return Result<B::Wire::Value_Ursa_NativeHandle>::Success(B::WriteManagedHandle<B::Wire::Value_Ursa_NativeHandle>(
	    Scope(call).AddCandidate(texture, Scripting::ManagedHandleKind::Texture2D)));
}

Result<B::Wire::Value_Ursa_Texture2DInfo>
ImportServices::GetTexture2DInfo(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& texture)
{
	const auto value = TextureCandidate(call, texture);
	return Result<B::Wire::Value_Ursa_Texture2DInfo>::Success({value->width, value->height, value->linear});
}

Result<B::DataView> ImportServices::GetTexture2DPixels(B::BindingCall& call,
                                                       const B::Wire::Value_Ursa_NativeHandle& texture)
{
	const auto value = TextureCandidate(call, texture);
	Vector<B::Wire::Value_Ursa_Color> colors;
	colors.reserve(Size(value->width) * value->height);
	for (Size offset = 0; offset < value->pixels.size(); offset += 4)
	{
		colors.push_back({value->pixels[offset] / 255.0f, value->pixels[offset + 1] / 255.0f,
		                  value->pixels[offset + 2] / 255.0f, value->pixels[offset + 3] / 255.0f});
	}
	return Result<B::DataView>::Success(call.arena.Array(colors.data(), colors.size()));
}

Result<void> ImportServices::SetTexture2DPixels(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& texture,
                                                const B::DataView& pixels)
{
	const auto value = TextureCandidate(call, texture);
	if (pixels.length != UInt64(value->width) * value->height)
	{
		return Result<void>::Failure("Texture2D pixel count differs from its dimensions");
	}
	Vector<UInt8> bytes;
	bytes.reserve(value->pixels.size());
	B::ReadArray<B::Wire::Value_Ursa_Color, UInt8>(
	    pixels,
	    [&](const auto& color)
	    {
		    for (Float32 channel : {color.F_R, color.F_G, color.F_B, color.F_A})
		    {
			    if (!std::isfinite(channel))
			    {
				    throw std::runtime_error("Texture2D pixels must be finite");
			    }
			    bytes.push_back(static_cast<UInt8>(std::round(std::clamp(channel, 0.0f, 1.0f) * 255.0f)));
		    }
		    return UInt8(0);
	    });
	value->pixels = std::move(bytes);
	return {};
}

void ImportInvocation::SetMaterialTexture(ImportMaterialTextureBinding binding)
{
	RequireCapability(0);
	if (binding.material.kind != Scripting::ManagedHandleKind::Material || binding.slot < 0 || binding.slot > 4)
	{
		throw std::runtime_error("Invalid import material or texture slot");
	}
	Candidate(binding.material);
	if (binding.texture.sessionId != 0)
	{
		if (binding.texture.kind != Scripting::ManagedHandleKind::Texture2D)
		{
			throw std::runtime_error("Expected an import Texture2D");
		}
		Candidate(binding.texture);
	}
	const auto existing = std::find_if(materialTextures.begin(), materialTextures.end(),
	                                   [&](const auto& value)
	                                   {
		                                   return value.material.index == binding.material.index &&
		                                          value.name == binding.name &&
		                                          (!binding.name.empty() || value.slot == binding.slot);
	                                   });
	if (existing != materialTextures.end())
	{
		materialTextures.erase(existing);
	}
	if (binding.texture.sessionId)
	{
		materialTextures.push_back(std::move(binding));
		return;
	}
	auto material = std::static_pointer_cast<MaterialInstance>(Candidate(binding.material));
	MaterialTexturePtr texture;
	if (binding.reference.guid)
	{
		texture = B::TakeBindingResult(
		    cpuApi->Resources()->LoadTexture(binding.reference.guid, binding.reference.localFileId));
	}
	if (binding.name.empty())
	{
		B::CompleteBindingResult(
		    material->SetTextureReference(MaterialTextureSlot(binding.slot), binding.reference, texture));
	}
	else
	{
		if (!material->Shader())
		{
			throw std::runtime_error("Material texture property requires Shader metadata");
		}
		const auto property = B::TakeBindingResult(material->PropertyValue(binding.name, *material->Shader()));
		if (property.type != Shader::PropertyType::Texture2D)
		{
			throw std::runtime_error("Shader property is not Texture2D");
		}
		if (!texture)
		{
			B::CompleteBindingResult(material->RemovePropertyOverride(binding.name));
		}
		else
		{
			MaterialPropertyValue value;
			value.type = Shader::PropertyType::Texture2D;
			value.textureId = binding.reference.guid;
			value.textureLocalFileId = binding.reference.localFileId;
			value.texture = texture;
			B::CompleteBindingResult(material->SetPropertyValue(binding.name, *material->Shader(), value));
		}
	}
}

void ImportInvocation::ResolveMaterialTextures(ResourceId guid, const String& importerIdentity)
{
	RequireCapability(0);
	for (const auto& binding : materialTextures)
	{
		auto material = std::static_pointer_cast<MaterialInstance>(Candidate(binding.material));
		AssetReference reference = binding.reference;
		MaterialTexturePtr texture;
		if (binding.texture.sessionId != 0)
		{
			const auto output = std::find_if(outputs.begin(), outputs.end(),
			                                 [&](const auto& value)
			                                 {
				                                 return value.second.kind == 4 &&
				                                        value.second.handle.sessionId == binding.texture.sessionId &&
				                                        value.second.handle.index == binding.texture.index &&
				                                        value.second.handle.generation == binding.texture.generation;
			                                 });
			if (output == outputs.end())
			{
				throw std::runtime_error("Material texture candidate was not registered as an output");
			}
			reference = {guid, B::TakeBindingResult(StableImportObjectId(importerIdentity, output->first))};
			texture = B::TakeBindingResult(
			    std::static_pointer_cast<ImportTextureCandidate>(Candidate(binding.texture))->Texture());
		}
		else if (reference.guid)
		{
			texture = B::TakeBindingResult(cpuApi->Resources()->LoadTexture(reference.guid, reference.localFileId));
		}
		if (binding.name.empty())
		{
			B::CompleteBindingResult(
			    material->SetTextureReference(MaterialTextureSlot(binding.slot), reference, texture));
		}
		else
		{
			if (!material->Shader())
			{
				throw std::runtime_error("Material texture property requires Shader metadata");
			}
			if (!texture)
			{
				B::CompleteBindingResult(material->RemovePropertyOverride(binding.name));
			}
			else
			{
				MaterialPropertyValue property;
				property.type = Shader::PropertyType::Texture2D;
				property.textureId = reference.guid;
				property.textureLocalFileId = reference.localFileId;
				property.texture = texture;
				B::CompleteBindingResult(material->SetPropertyValue(binding.name, *material->Shader(), property));
			}
		}
	}
}

Result<void> ImportServices::SetMaterialTexture(B::BindingCall& call, const B::Wire::Value_Ursa_NativeHandle& material,
                                                const B::Wire::Value_Ursa_NativeHandle& texture,
                                                const B::Wire::Value_Ursa_NativeAssetReference_& reference, Int32 slot,
                                                const B::Utf8View& name)
{
	Scope(call).SetMaterialTexture({B::ReadManagedHandle(material), B::ReadManagedHandle(texture),
	                                reference.present ? B::ReadAssetValue(reference.F_value) : AssetReference{}, slot,
	                                B::ReadString(name)});
	return {};
}
} // namespace Ursa::Editor::Bindings
