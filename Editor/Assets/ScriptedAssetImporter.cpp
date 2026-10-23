#include <Assets/ScriptedAssetImporter.hpp>
#include <Assets/ModelImportScene.hpp>
#include <Assets/ModelImporter.hpp>

#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/Bindings/ImportServices.hpp>
#include <Import.generated.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <algorithm>

namespace Ursa::Editor
{
namespace B = Scripting::Bindings;

namespace
{
template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

bool SameHandle(const Scripting::ManagedHandle& left, const Scripting::ManagedHandle& right)
{
	return left.sessionId == right.sessionId && left.index == right.index && left.generation == right.generation &&
	       left.kind == right.kind && left.reserved == right.reserved;
}

ResourceId GraphIdentity(ResourceId guid, const String& key)
{
	ResourceId result;
	const UInt64 low = Take(StableImportObjectId(guid.ToString(), "graph-low:" + key));
	const UInt64 high = Take(StableImportObjectId(guid.ToString(), "graph-high:" + key));
	for (Size index = 0; index < 8; ++index)
	{
		result.bytes[index] = static_cast<UInt8>(low >> (8 * index));
		result.bytes[index + 8] = static_cast<UInt8>(high >> (8 * index));
	}
	result.bytes[6] = (result.bytes[6] & 0x0f) | 0x50;
	result.bytes[8] = (result.bytes[8] & 0x3f) | 0x80;
	return result;
}

void StabilizeHierarchy(Bindings::ImportInvocation& scope, ResourceId guid)
{
	Map<const GameObject*, String> explicitKeys;
	for (const auto& entry : scope.Outputs())
	{
		if (entry.second.kind == 5)
		{
			explicitKeys.emplace(std::static_pointer_cast<GameObject>(scope.Candidate(entry.second.handle)).get(),
			                     "output[" + ToUrsaString(std::to_string(entry.first.size())) + "]:" + entry.first);
		}
	}
	Map<Mesh*, std::pair<MeshPtr, String>> localMeshes;
	Map<MaterialInstance*, std::pair<std::shared_ptr<MaterialInstance>, String>> localMaterials;
	const auto retainMesh = [&](const MeshPtr& mesh, const String& key)
	{
		if (!mesh || mesh->AssetIdentity().guid)
		{
			return;
		}
		auto found = localMeshes.find(mesh.get());
		if (found == localMeshes.end())
		{
			localMeshes.emplace(mesh.get(), std::make_pair(mesh, key));
		}
		else if (key < found->second.second)
		{
			found->second.second = key;
		}
	};
	std::function<void(const Vector<GameObject::Ptr>&, const String&)> visit;
	visit = [&](const Vector<GameObject::Ptr>& nodes, const String& parent)
	{
		Map<String, UInt64> occurrences;
		for (const auto& node : nodes)
		{
			const String name = Take(node->Name());
			const auto explicitKey = explicitKeys.find(node.get());
			const String key = explicitKey != explicitKeys.end()
			                       ? explicitKey->second
			                       : parent + "/" + ToUrsaString(std::to_string(name.size())) + ":" + name + ":" +
			                             ToUrsaString(std::to_string(occurrences[name]++));
			Done(scope.SceneGraph().AssignGameObjectId(node, GraphIdentity(guid, key)));
			Map<String, UInt64> componentOccurrences;
			for (Component* component : node->Components())
			{
				const String type = component->SerializationType();
				const String componentKey =
				    key + "/component:" + type + ":" + ToUrsaString(std::to_string(componentOccurrences[type]++));
				Done(
				    scope.SceneGraph().AssignComponentPersistentId(component->Id(), GraphIdentity(guid, componentKey)));
				if (auto* filter = dynamic_cast<MeshFilterComponent*>(component))
				{
					retainMesh(filter->SharedMesh(), componentKey + "/mesh");
				}
				if (auto* renderer = dynamic_cast<SkinnedMeshRendererComponent*>(component))
				{
					retainMesh(renderer->SharedMesh(), componentKey + "/mesh");
				}
				if (auto* renderer = dynamic_cast<RendererComponent*>(component))
				{
					const auto materials = renderer->SharedMaterials();
					for (Size slot = 0; slot < materials.size(); ++slot)
					{
						if (!materials[slot] || materials[slot]->AssetIdentity().guid)
						{
							continue;
						}
						const String materialKey = componentKey + "/material:" + ToUrsaString(std::to_string(slot));
						auto found = localMaterials.find(materials[slot].get());
						if (found == localMaterials.end())
						{
							localMaterials.emplace(materials[slot].get(), std::make_pair(materials[slot], materialKey));
						}
						else if (materialKey < found->second.second)
						{
							found->second.second = materialKey;
						}
					}
				}
			}
			visit(node->Children(), key);
		}
	};
	visit(scope.SceneGraph().RootGameObjects(), "root");
	for (const auto& entry : localMeshes)
	{
		Done(entry.second.first->BindLocalIdentity(GraphIdentity(guid, entry.second.second)));
	}
	for (const auto& entry : localMaterials)
	{
		Done(entry.second.first->BindLocalIdentity(GraphIdentity(guid, entry.second.second)));
	}
}

class ImportLease final
{
public:
	ImportLease(Scripting::DotNetHost& host, UInt64 session, Bindings::ImportInvocation& scope)
	    : host(host), session(session), scope(scope)
	{
	}

	~ImportLease()
	{
		if (active)
		{
			Close();
		}
	}

	Result<void> Close()
	{
		active = false;
		auto ended = host.EndImportInvocation(session, scope.Generation(), scope.Identity());
		auto detached = host.DetachImportResources(session);
		return ended ? detached : ended;
	}

private:
	Scripting::DotNetHost& host;
	UInt64 session;
	Bindings::ImportInvocation& scope;
	bool active = true;
};

Result<AssetImportResult> TryImportCache(AssetDatabase& database, const VirtualPath& source, bool force,
                                         const nlohmann::json& settings, const AssetImportRules& rules,
                                         const String& identity, UInt32 version, const String& target, ResourceId guid,
                                         const String& ownerType, UInt64 session)
{
	Bindings::ImportInvocation scope(session, rules.Generation(), source, Bindings::ImportPhase::ScriptedImport,
	                                 [&] { return rules.Generation(); });
	scope.ConfigureDependencies(database, rules, guid, ownerType);
	const auto failure = [&](const String& error)
	{
		auto result =
		    Result<AssetImportResult>::Failure(source.ToString() + " [" + ownerType + ", ScriptedImport]: " + error);
		result.value.pendingArtifacts = scope.PendingArtifacts();
		result.value.diagnostics = scope.Diagnostics();
		return result;
	};
	// Reuse the last dependency declarations only while the source and rule inputs are unchanged.
	const auto previous = Take(database.CurrentImport(source));
	if (!force && previous.upToDate && previous.target == target && previous.importer == identity &&
	    previous.importerVersion == version &&
	    previous.settings.value("settings", nlohmann::json{}) == settings.at("settings") &&
	    previous.settings.value("rules", std::string{}) == settings.at("rules"))
	{
		bool unchanged = true;
		for (const auto& entry : previous.settings.value("artifacts", nlohmann::json::array()))
		{
			const auto reference = Take(AssetReferenceFromJson(entry.at("reference"), "scripted import cache"));
			const auto bytes = scope.ReadArtifact(reference);
			if (!bytes)
			{
				return failure(bytes.error);
			}
			const auto recorded = std::find_if(scope.ArtifactDependencies().begin(), scope.ArtifactDependencies().end(),
			                                   [&](const auto& item)
			                                   {
				                                   return item.reference.guid == reference.guid &&
				                                          item.reference.localFileId == reference.localFileId;
			                                   });
			unchanged = unchanged && recorded != scope.ArtifactDependencies().end() &&
			            recorded->contentFingerprint == ToUrsaString(entry.at("fingerprint").get<std::string>());
		}
		if (unchanged)
		{
			return Result<AssetImportResult>::Success(
			    {guid, previous.artifacts, previous.dependencies, true, {}, previous.mainObjectLocalFileId});
		}
	}
	return Result<AssetImportResult>::Success({});
}

Vector<ImportedArtifact> EncodeOutputs(Bindings::ImportInvocation& scope, const AssetImporterDescriptor& importer,
                                       ResourceId guid, Scripting::DotNetHost& host, UInt64 session)
{
	if (scope.Outputs().empty() || !scope.MainObject())
	{
		throw std::runtime_error("ScriptedImporter must register output objects and set a main object");
	}
	// Field traversal discovers references before any object receives a persistent identity.
	for (const auto& entry : scope.Outputs())
	{
		if (entry.second.kind == 1)
		{
			Take(host.SerializeImportScriptableObject(session, entry.second.instance));
		}
	}
	const auto references = Take(host.GetImportReferences(session));
	Map<UInt64, UInt64> identities;
	Set<UInt64> usedIds;
	Vector<Scripting::ManagedImportAssignment> assignments;
	for (const auto& entry : scope.Outputs())
	{
		const auto& output = entry.second;
		const auto found = std::find_if(references.begin(), references.end(),
		                                [&](const auto& value) { return value.token == output.token; });
		if (found == references.end() || found->kind != output.kind || found->instance != output.instance ||
		    !SameHandle(found->handle, output.handle))
		{
			throw std::runtime_error("Output object is not owned by the active managed invocation: " +
			                         ToStdString(output.key));
		}
		const UInt64 localId = Take(StableImportObjectId(importer.identity, output.key));
		if (!localId || !usedIds.insert(localId).second)
		{
			throw std::runtime_error("Import output identity collision");
		}
		identities.emplace(output.token, localId);
		assignments.push_back({output.token, {guid, localId}});
	}
	for (const auto& reference : references)
	{
		if (!identities.count(reference.token))
		{
			throw std::runtime_error("A candidate reference was not added to the output set");
		}
	}
	for (const auto& entry : scope.Outputs())
	{
		const auto& output = entry.second;
		const AssetReference identity{guid, identities.at(output.token)};
		if (output.kind == 2)
		{
			Done(std::static_pointer_cast<Mesh>(scope.Candidate(output.handle))->BindAssetIdentity(identity));
		}
		if (output.kind == 3)
		{
			Done(std::static_pointer_cast<MaterialInstance>(scope.Candidate(output.handle))
			         ->BindAssetIdentity(identity));
		}
	}
	Done(host.AssignImportReferences(session, assignments));
	scope.ResolveMaterialTextures(guid, importer.identity);
	StabilizeHierarchy(scope, guid);
	VirtualFileSystem files;
	Done(files.MountProvider("mem", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	ComponentRegistry components;
	Done(RegisterBuiltinComponents(components));
	Vector<ImportedArtifact> artifacts;
	for (const auto& entry : scope.Outputs())
	{
		const auto& output = entry.second;
		ImportedArtifact artifact;
		artifact.outputKey = output.key;
		artifact.localFileId = identities.at(output.token);
		if (output.kind == 1)
		{
			const auto payload = nlohmann::json::parse(
			    ToStdString(Take(host.SerializeImportScriptableObject(session, output.instance))));
			ScriptableAsset asset;
			Done(asset.SetDescription(guid, artifact.localFileId,
			                          ToUrsaString(payload.at("ScriptType").get<std::string>()),
			                          payload.at("Version").get<UInt32>(), payload));
			artifact.kind = "ScriptableAsset";
			artifact.bytes = Take(EncodeScriptableAssetArtifact(asset));
		}
		else if (output.kind == 2)
		{
			artifact.kind = "Mesh";
			artifact.bytes = Take(EncodeMeshResource(
			    guid, *std::static_pointer_cast<Mesh>(scope.Candidate(output.handle)), artifact.localFileId));
		}
		else if (output.kind == 3)
		{
			artifact.kind = "Material";
			Done(SaveMaterialResource(files, {"mem", "Output.ursamat"}, guid,
			                          *std::static_pointer_cast<MaterialInstance>(scope.Candidate(output.handle)), {}));
			artifact.bytes = Take(files.Read({"mem", "Output.ursamat"}));
		}
		else if (output.kind == 4)
		{
			artifact.kind = "Texture";
			const auto texture = Take(
			    std::static_pointer_cast<Bindings::ImportTextureCandidate>(scope.Candidate(output.handle))->Texture());
			Done(SaveTextureResource(files, {"mem", "Output.ursatex"}, guid, *texture));
			artifact.bytes = Take(files.Read({"mem", "Output.ursatex"}));
		}
		else if (output.kind == 5)
		{
			artifact.kind = "Prefab";
			Done(SavePrefabResource(files, {"mem", "Output.ursaprefab"}, guid, scope.SceneGraph(),
			                        std::static_pointer_cast<GameObject>(scope.Candidate(output.handle)), components));
			artifact.bytes = Take(files.Read({"mem", "Output.ursaprefab"}));
		}
		else
		{
			throw std::runtime_error("Unsupported import output kind");
		}
		artifacts.push_back(std::move(artifact));
	}
	return artifacts;
}
} // namespace

Result<AssetImportResult> ImportScriptedAsset(AssetDatabase& database, const VirtualPath& source, bool force,
                                              const AssetImportRules& rules, Scripting::DotNetHost& host,
                                              UInt64 session, const std::function<UInt64()>& generationProvider)
{
	try
	{
		const auto metadata = Take(database.Metadata(source));
		const String extension =
		    ToUrsaString(std::filesystem::u8path(ToStdString(source.relative)).extension().u8string());
		const String required = metadata.importer.find("script:") == 0 ? metadata.importer : String{};
		const auto importer = Take(rules.FindImporter(extension, required));
		if (!importer->scriptType)
		{
			return Result<AssetImportResult>::Failure("Source does not select a ScriptedImporter");
		}
		const auto definition = Take(database.SourcePath(importer->scriptType->sourceGuid));
		Vector<VirtualPath> dependencies{definition, Take(VirtualPath::Parse(definition.ToString() + ".meta"))};
		if (database.Filesystem().CheckWritable(source))
		{
			Done(database.SetImporterSettings(source, importer->identity, importer->version, metadata.importSettings));
		}
		nlohmann::json settings{{"settings", metadata.importSettings},
		                        {"rules", ToStdString(Take(rules.FingerprintFor(extension, source.scheme, required)))}};
		const String target = "scripted-v1";
		const auto cached =
		    TryImportCache(database, source, force, settings, rules, importer->identity, importer->version, target,
		                   metadata.guid, importer->scriptType->clrTypeName, session);
		if (!cached || cached.value.cacheHit)
		{
			return cached;
		}
		Bindings::ImportInvocation scope(
		    session, rules.Generation(), source, Bindings::ImportPhase::ScriptedImport,
		    generationProvider ? generationProvider : std::function<UInt64()>([&] { return rules.Generation(); }));
		scope.ConfigureDependencies(database, rules, metadata.guid, importer->scriptType->clrTypeName);
		const auto failure = [&](const String& error)
		{
			auto result = Result<AssetImportResult>::Failure(
			    source.ToString() + " [" + importer->scriptType->clrTypeName + ", ScriptedImport]: " + error);
			result.value.pendingArtifacts = scope.PendingArtifacts();
			result.value.diagnostics = scope.Diagnostics();
			return result;
		};
		scope.SetSourceBytes(Take(database.Filesystem().Read(source)));
		Done(host.BindScriptSession(session, scope.CpuApi()));
		const B::BindingModuleReference capability{B::Import::moduleId, &B::Import::NativeDescriptor(), scope.Token()};
		Done(host.BeginImportInvocation(session, rules.Generation(), scope.Identity(), capability));
		ImportLease lease(host, session, scope);
		try
		{
			Done(host.InvokeScriptedImporter(session, importer->identity));
			if (scope.HasErrors() || !scope.PendingArtifacts().empty())
			{
				throw std::runtime_error(scope.HasErrors() ? ToStdString(scope.Diagnostics().back())
				                                           : "Import has unfinished artifact dependencies");
			}
			const auto artifacts = EncodeOutputs(scope, *importer, metadata.guid, host, session);
			UInt64 mainId = 0;
			for (const auto& entry : scope.Outputs())
			{
				if (entry.second.token == scope.MainObject())
				{
					mainId = Take(StableImportObjectId(importer->identity, entry.first));
				}
			}
			for (const auto& path : scope.SourceDependencies())
			{
				if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
				{
					dependencies.push_back(path);
				}
			}
			settings["artifacts"] = nlohmann::json::array();
			for (const auto& dependency : scope.ArtifactDependencies())
			{
				settings["artifacts"].push_back({{"reference", AssetReferenceToJson(dependency.reference)},
				                                 {"fingerprint", ToStdString(dependency.contentFingerprint)}});
			}
			scope.ValidateReads();
			Done(lease.Close());
			Done(database.PublishImport(source, importer->identity, importer->version, target, settings, dependencies,
			                            artifacts, mainId));
		}
		catch (const std::exception& error)
		{
			return failure(ToUrsaString(error.what()));
		}
		const auto current = Take(database.CurrentImport(source));
		return Result<AssetImportResult>::Success({metadata.guid, current.artifacts, dependencies, false,
		                                           scope.Diagnostics(), current.mainObjectLocalFileId});
	}
	catch (const std::exception& error)
	{
		return Result<AssetImportResult>::Failure("Scripted import " + source.ToString() + ": " +
		                                          ToUrsaString(error.what()));
	}
}

namespace
{
String ProcessingTypes(const Vector<AssetPostprocessorDescriptor>& chain)
{
	String names;
	for (const auto& processor : chain)
	{
		if (!names.empty())
		{
			names += "; ";
		}
		names += processor.type.clrTypeName;
	}
	return names.empty() ? String("ModelImporter") : names;
}

Result<AssetImportResult> ImportProcessedTexture(AssetDatabase& database, const VirtualPath& source, bool force,
                                                 const AssetImportRules& rules, Scripting::DotNetHost& host,
                                                 UInt64 session, const std::function<UInt64()>& generationProvider)
{
	const auto chain = rules.PostprocessorsFor("ursa.Texture", source.scheme);
	if (chain.empty())
	{
		const auto saved = TextureImporter::Settings(database, source);
		if (!saved)
		{
			return Result<AssetImportResult>::Failure(saved.error);
		}
		const auto imported = TextureImporter::Import(database, source, saved.value, "texture-rgba8", force);
		if (!imported)
		{
			return Result<AssetImportResult>::Failure(imported.error);
		}
		auto current = ProjectAssetImporter::LoadCurrent(database, source);
		if (current)
		{
			current.value.cacheHit = imported.value.cacheHit;
		}
		return current;
	}
	try
	{
		const auto metadata = Take(database.Metadata(source));
		const auto saved = Take(TextureImporter::Settings(database, source));
		const String extension =
		    ToUrsaString(std::filesystem::u8path(ToStdString(source.relative)).extension().u8string());
		nlohmann::json settings{{"settings", saved.ToJson()},
		                        {"rules", ToStdString(Take(rules.FingerprintFor(extension, source.scheme)))}};
		const String target = "texture-processed-v1";
		const auto cache = TryImportCache(database, source, force, settings, rules, "ursa.Texture", 1, target,
		                                  metadata.guid, chain.front().type.clrTypeName, session);
		if (!cache || cache.value.cacheHit)
		{
			return cache;
		}
		Vector<VirtualPath> dependencies;
		for (const auto& processor : chain)
		{
			const auto definition = Take(database.SourcePath(processor.type.sourceGuid));
			for (const auto& path :
			     Vector<VirtualPath>{definition, Take(VirtualPath::Parse(definition.ToString() + ".meta"))})
			{
				if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
				{
					dependencies.push_back(path);
				}
			}
		}
		Bindings::ImportInvocation scope(
		    session, rules.Generation(), source, Bindings::ImportPhase::TexturePreprocess,
		    generationProvider ? generationProvider : std::function<UInt64()>([&] { return rules.Generation(); }));
		scope.SetSourceBytes(Take(database.Filesystem().Read(source)));
		scope.ConfigureDependencies(database, rules, metadata.guid, chain.front().type.clrTypeName);
		scope.ConfigureTextureSettings(saved);
		Done(host.BindScriptSession(session, scope.CpuApi()));
		const B::BindingModuleReference capability{B::Import::moduleId, &B::Import::NativeDescriptor(), scope.Token()};
		Done(host.BeginImportInvocation(session, rules.Generation(), scope.Identity(), capability));
		ImportLease lease(host, session, scope);
		try
		{
			const auto process = [&]
			{
				for (const auto& processor : chain)
				{
					scope.SetOwnerType(processor.type.clrTypeName);
					const auto result = host.InvokeAssetPostprocessor(session, Take(processor.type.StableIdentity()));
					if (!result)
					{
						throw std::runtime_error(ToStdString(processor.type.clrTypeName + ": " + result.error));
					}
					if (scope.HasErrors() || !scope.PendingArtifacts().empty())
					{
						throw std::runtime_error("Postprocessor reported an error or unfinished artifact dependency");
					}
				}
			};
			process();
			scope.SetOwnerType(ProcessingTypes(chain));
			const auto effective = scope.EffectiveTextureSettings();
			const auto decoded = Take(TextureImporter::DecodeCandidate(database.Filesystem(), source, effective));
			auto candidate = std::make_shared<Bindings::ImportTextureCandidate>();
			candidate->width = decoded->GetWidth();
			candidate->height = decoded->GetHeight();
			candidate->linear = decoded->GetColorSpace() == TextureColorSpace::Linear;
			candidate->pixels = decoded->GetPixels();
			scope.SetPostprocessObject(scope.AddCandidate(candidate, Scripting::ManagedHandleKind::Texture2D));
			scope.SetPhase(Bindings::ImportPhase::TexturePostprocess);
			process();
			scope.SetOwnerType(ProcessingTypes(chain));
			const auto processed = Take(candidate->Texture());
			const auto texture = Take(TextureImporter::FinalizeCandidate(*processed, effective));
			VirtualFileSystem staging;
			Done(staging.MountProvider("mem", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
			Done(SaveTextureResource(staging, {"mem", "Output.ursatex"}, metadata.guid, *texture));
			const auto bytes = Take(staging.Read({"mem", "Output.ursatex"}));
			for (const auto& path : scope.SourceDependencies())
			{
				if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
				{
					dependencies.push_back(path);
				}
			}
			settings["effective"] = effective.ToJson();
			settings["artifacts"] = nlohmann::json::array();
			for (const auto& dependency : scope.ArtifactDependencies())
			{
				settings["artifacts"].push_back({{"reference", AssetReferenceToJson(dependency.reference)},
				                                 {"fingerprint", ToStdString(dependency.contentFingerprint)}});
			}
			scope.ValidateReads();
			Done(lease.Close());
			Done(database.PublishImport(source, "ursa.Texture", 1, target, settings, dependencies,
			                            {{0, "Texture", bytes}}));
			const auto current = Take(database.CurrentImport(source));
			return Result<AssetImportResult>::Success(
			    {metadata.guid, current.artifacts, dependencies, false, scope.Diagnostics(), 0});
		}
		catch (const std::exception& error)
		{
			auto result = Result<AssetImportResult>::Failure(scope.DiagnosticPrefix() + ToUrsaString(error.what()));
			result.value.pendingArtifacts = scope.PendingArtifacts();
			result.value.diagnostics = scope.Diagnostics();
			for (const auto& diagnostic : scope.Diagnostics())
			{
				result.error += "\n" + diagnostic;
			}
			return result;
		}
	}
	catch (const std::exception& error)
	{
		return Result<AssetImportResult>::Failure(source.ToString() +
		                                          " [TextureImport]: " + ToUrsaString(error.what()));
	}
}

Result<AssetImportResult> ImportProcessedModel(AssetDatabase& database, const VirtualPath& source, bool force,
                                               const AssetImportRules& rules, Scripting::DotNetHost& host,
                                               UInt64 session, const std::function<UInt64()>& generationProvider)
{
	const auto chain = rules.PostprocessorsFor("ursa.Model", source.scheme);
	if (chain.empty() && rules.PostprocessorsFor("ursa.Texture", source.scheme).empty())
	{
		const auto saved = ModelImporter::Settings(database, source);
		if (!saved)
		{
			return Result<AssetImportResult>::Failure(saved.error);
		}
		const auto imported = ModelImporter::Import(database, source, saved.value, force);
		if (!imported)
		{
			return Result<AssetImportResult>::Failure(imported.error);
		}
		auto current = ProjectAssetImporter::LoadCurrent(database, source);
		if (current)
		{
			current.value.cacheHit = imported.value.cacheHit;
		}
		return current;
	}
	try
	{
		const auto metadata = Take(database.Metadata(source));
		const auto saved = Take(ModelImporter::Settings(database, source));
		const String extension =
		    ToUrsaString(std::filesystem::u8path(ToStdString(source.relative)).extension().u8string());
		nlohmann::json settings{{"settings", saved.ToJson()},
		                        {"rules", ToStdString(Take(rules.FingerprintFor(extension, source.scheme)))}};
		const String target = "model-processed-v1";
		const String ownerType = chain.empty() ? String("ModelImporter") : chain.front().type.clrTypeName;
		const auto cache = TryImportCache(database, source, force, settings, rules, "ursa.Model",
		                                  ModelImporter::Version, target, metadata.guid, ownerType, session);
		if (!cache || cache.value.cacheHit)
		{
			return cache;
		}
		Vector<VirtualPath> dependencies;
		for (const auto& processor : chain)
		{
			const auto definition = Take(database.SourcePath(processor.type.sourceGuid));
			for (const auto& path :
			     Vector<VirtualPath>{definition, Take(VirtualPath::Parse(definition.ToString() + ".meta"))})
			{
				if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
				{
					dependencies.push_back(path);
				}
			}
		}
		Bindings::ImportInvocation scope(
		    session, rules.Generation(), source, Bindings::ImportPhase::ModelPreprocess,
		    generationProvider ? generationProvider : std::function<UInt64()>([&] { return rules.Generation(); }));
		scope.SetSourceBytes(Take(database.Filesystem().Read(source)));
		scope.ConfigureDependencies(database, rules, metadata.guid, ownerType);
		scope.ConfigureModelSettings(saved);
		Done(host.BindScriptSession(session, scope.CpuApi()));
		const B::BindingModuleReference capability{B::Import::moduleId, &B::Import::NativeDescriptor(), scope.Token()};
		Done(host.BeginImportInvocation(session, rules.Generation(), scope.Identity(), capability));
		ImportLease lease(host, session, scope);
		Map<String, nlohmann::json> dependencyDefaults;
		Set<String> textureSources;
		try
		{
			const auto process = [&]
			{
				for (const auto& processor : chain)
				{
					scope.SetOwnerType(processor.type.clrTypeName);
					const auto result = host.InvokeAssetPostprocessor(session, Take(processor.type.StableIdentity()));
					if (!result)
					{
						throw std::runtime_error(ToStdString(processor.type.clrTypeName + ": " + result.error));
					}
					if (scope.Phase() == Bindings::ImportPhase::ModelPostprocess)
					{
						scope.PostprocessObject();
					}
					if (scope.HasErrors() || !scope.PendingArtifacts().empty())
					{
						throw std::runtime_error("Postprocessor reported an error or unfinished artifact dependency");
					}
				}
			};
			process();
			scope.SetOwnerType(ProcessingTypes(chain));
			const auto effective = scope.EffectiveModelSettings();
			auto candidate = Take(ModelImporter::BuildCandidate(
			    database, source, effective,
			    [&](const VirtualPath& path, const TextureImportSettings& defaults) -> Result<ResourceId>
			    {
				    textureSources.insert(path.ToString());
				    const auto textureMetadata = database.Metadata(path);
				    if (!textureMetadata)
				    {
					    return Result<ResourceId>::Failure(textureMetadata.error);
				    }
				    if (textureMetadata.value.importer.empty())
				    {
					    dependencyDefaults[path.ToString()] = defaults.ToJson();
				    }
				    const auto completed = scope.ReadArtifact({textureMetadata.value.guid, 0});
				    return completed ? Result<ResourceId>::Success(textureMetadata.value.guid)
				                     : Result<ResourceId>::Failure(completed.error);
			    }));
			ModelImportScene view(candidate, scope.SceneGraph());
			const auto root = Take(view.Build());
			for (const auto& mesh : candidate.meshes)
			{
				Take(scope.CpuApi().RegisterMesh(mesh.second, true));
			}
			for (const auto& material : candidate.materials)
			{
				Take(scope.CpuApi().RegisterMaterial(material.second.material, true));
			}
			scope.SetPostprocessObject(Take(scope.CpuApi().Register(root)));
			scope.SetPhase(Bindings::ImportPhase::ModelPostprocess);
			process();
			scope.SetOwnerType(ProcessingTypes(chain));
			scope.ResolveMaterialTextures(metadata.guid, "ursa.Model");
			Done(view.Apply());
			const auto artifacts = Take(candidate.Encode());
			for (const auto& path : candidate.dependencies)
			{
				if (!textureSources.count(path.ToString()) &&
				    std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
				{
					dependencies.push_back(path);
				}
			}
			for (const auto& path : scope.SourceDependencies())
			{
				if (std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
				{
					dependencies.push_back(path);
				}
			}
			settings["effective"] = effective.ToJson();
			settings["artifacts"] = nlohmann::json::array();
			for (const auto& dependency : scope.ArtifactDependencies())
			{
				settings["artifacts"].push_back({{"reference", AssetReferenceToJson(dependency.reference)},
				                                 {"fingerprint", ToStdString(dependency.contentFingerprint)}});
			}
			scope.ValidateReads();
			Done(lease.Close());
			Done(database.PublishImport(source, "ursa.Model", ModelImporter::Version, target, settings, dependencies,
			                            artifacts, candidate.mainObjectLocalFileId));
			const auto current = Take(database.CurrentImport(source));
			auto diagnostics = scope.Diagnostics();
			diagnostics.insert(diagnostics.end(), candidate.diagnostics.begin(), candidate.diagnostics.end());
			return Result<AssetImportResult>::Success(
			    {metadata.guid, current.artifacts, dependencies, false, diagnostics, candidate.mainObjectLocalFileId});
		}
		catch (const std::exception& error)
		{
			auto result = Result<AssetImportResult>::Failure(scope.DiagnosticPrefix() + ToUrsaString(error.what()));
			result.value.pendingArtifacts = scope.PendingArtifacts();
			for (auto& dependency : result.value.pendingArtifacts)
			{
				const auto defaults = dependencyDefaults.find(dependency.source.ToString());
				if (defaults != dependencyDefaults.end())
				{
					dependency.initialSettings = defaults->second;
				}
			}
			result.value.diagnostics = scope.Diagnostics();
			for (const auto& diagnostic : scope.Diagnostics())
			{
				result.error += "\n" + diagnostic;
			}
			return result;
		}
	}
	catch (const std::exception& error)
	{
		return Result<AssetImportResult>::Failure(source.ToString() + " [ModelImport]: " + ToUrsaString(error.what()));
	}
}

} // namespace

Result<void> RegisterScriptedAssetImporters(ProjectAssetImporter& importer,
                                            std::shared_ptr<const AssetImportRules> rules, Scripting::DotNetHost& host,
                                            UInt64 session, ImportDispatcher dispatcher,
                                            std::function<UInt64()> generationProvider)
{
	if (!rules)
	{
		return Result<void>::Failure("Managed importer registration requires a frozen rule snapshot");
	}
	const auto bind = [dispatcher](ProjectAssetImporter::ImportFunction callback,
	                               bool native = false) -> ProjectAssetImporter::ImportFunction
	{
		if (!dispatcher)
		{
			return callback;
		}
		return [dispatcher, callback = std::move(callback), native](AssetDatabase& database, const VirtualPath& source,
		                                                            bool force)
		{
			return native && source.scheme == engineScheme ? callback(database, source, force)
			                                               : dispatcher(callback, database, source, force);
		};
	};

	for (const auto& descriptor : rules->Importers())
	{
		if (!descriptor.scriptType)
		{
			continue;
		}
		for (const auto& extension : descriptor.extensions)
		{
			auto registered = importer.RegisterScripted(
			    extension, descriptor.identity, descriptor.version,
			    bind(
			        [rules, &host, session, generationProvider](AssetDatabase& database, const VirtualPath& source,
			                                                    bool force)
			        {
				        return ImportScriptedAsset(database, source, force, *rules, host, session, generationProvider);
			        }));
			if (!registered)
			{
				return registered;
			}
		}
	}
	if (!rules->PostprocessorsFor("ursa.Texture", projectScheme).empty())
	{
		const auto registered = importer.RegisterNativeProcessing(
		    "ursa.Texture", {".png", ".jpg", ".jpeg"},
		    bind([rules, &host, session, generationProvider](AssetDatabase& database, const VirtualPath& source,
		                                                     bool force)
		         { return ImportProcessedTexture(database, source, force, *rules, host, session, generationProvider); },
		         true));
		if (!registered)
		{
			return registered;
		}
	}

	if (!rules->PostprocessorsFor("ursa.Model", projectScheme).empty() ||
	    !rules->PostprocessorsFor("ursa.Texture", projectScheme).empty())
	{
		return importer.RegisterNativeProcessing(
		    "ursa.Model", ModelImporter::SupportedExtensions(),
		    bind([rules, &host, session, generationProvider](AssetDatabase& database, const VirtualPath& source,
		                                                     bool force)
		         { return ImportProcessedModel(database, source, force, *rules, host, session, generationProvider); },
		         true));
	}

	return {};
}
} // namespace Ursa::Editor
#endif
