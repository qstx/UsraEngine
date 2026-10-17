#include <Assets/AssetImportPipeline.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Scripting/Bindings/BindingRequirements.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

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

VirtualPath Path(const String& text)
{
	return Take(VirtualPath::Parse(text));
}

bool IsShader(const VirtualPath& path)
{
	const auto extension = PathFromUrsaString(path.relative).extension();
	return extension == ".ursashader" || extension == ".compute";
}

bool BootstrapAsset(const AssetRecord& asset)
{
	return asset.path.scheme == engineScheme && (asset.path.relative.rfind("Shaders/Editor/", 0) == 0 ||
	                                             asset.path.relative.rfind("Shaders/Renderer3D/", 0) == 0 ||
	                                             asset.path.relative.rfind("Fonts/Editor/", 0) == 0);
}

bool RuntimeAsset(const AssetRecord& asset)
{
	const auto extension = PathFromUrsaString(asset.path.relative).extension();
	return extension != ".cs" && extension != ".hlsli" && extension != ".hlsl" && extension != ".frag" &&
	       extension != ".vert" && extension != ".comp" && extension != ".md" && extension != ".mtl";
}

void AddEntry(RuntimeContentSnapshot& snapshot, const VirtualFileSystem& files, const VirtualPath& logical,
              const VirtualPath& storage)
{
	const auto bytes = Take(files.Read(storage));
	snapshot.entries.push_back({logical, storage, BuildResourceFingerprint(bytes), static_cast<UInt64>(bytes.size())});
}

VirtualPath StoreBlob(VirtualFileSystem& files, const Vector<UInt8>& bytes, const String& suffix)
{
	const auto path = Path("lib:RuntimeContent/Blobs/" + BuildResourceFingerprint(bytes) + suffix);
	Done(files.WriteAtomic(path, bytes));
	return path;
}

Vector<UInt8> JsonBytes(const Json& document)
{
	const auto text = document.dump(2);
	return Vector<UInt8>(text.data(), text.data() + text.size());
}

void ValidateResourceClosure(const VirtualFileSystem& files, const RuntimeContentSnapshot& snapshot)
{
	const auto catalog =
	    Json::parse(ToStdString(Take(files.ReadText(Path("prj:__UrsaCooked/AssetCatalog.ursacatalog")))));
	Map<String, VirtualPath> assetsByIdentity;
	for (const auto& asset : catalog.at("assets"))
	{
		const auto guid = ToUrsaString(asset.at("guid").get<std::string>());
		const auto localFileId = ToUrsaString(std::to_string(asset.at("localFileId").get<UInt64>()));
		const String key = guid + ":" + localFileId;
		auto path = VirtualPath::Parse(ToUrsaString(asset.at("path").get<std::string>()));
		if (!path || !assetsByIdentity.emplace(key, std::move(path.value)).second)
		{
			throw std::runtime_error(
			    ToStdString("Runtime asset catalog has an invalid or duplicate asset identity: " + key));
		}
	}
	auto resources = Take(ResourceSession::Open(
	    files,
	    [&](ResourceId guid, UInt64 localFileId)
	    {
		    const String key = guid.ToString() + ":" + ToUrsaString(std::to_string(localFileId));
		    const auto found = assetsByIdentity.find(key);
		    return found != assetsByIdentity.end() ? Result<VirtualPath>::Success(found->second)
		                                           : Result<VirtualPath>::Failure("Missing runtime asset " + key);
	    }));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	for (const auto& asset : catalog.at("assets"))
	{
		const auto localId = asset.at("localFileId").get<UInt64>();
		if (!localId)
		{
			continue;
		}
		const auto guid = Take(ResourceId::Parse(ToUrsaString(asset.at("guid").get<std::string>())));
		const auto kind = asset.at("kind").get<std::string>();
		if (kind == "Texture")
		{
			Take(resources->LoadTexture(guid, localId));
		}
		else if (kind == "Material")
		{
			Take(resources->LoadMaterial(guid, localId));
		}
		else if (kind == "Mesh")
		{
			Take(resources->LoadMesh(guid, localId));
		}
		else if (kind == "Skeleton")
		{
			Take(resources->LoadSkeleton(guid, localId));
		}
		else if (kind == "AnimationClip")
		{
			Take(resources->LoadAnimationClip(guid, localId));
		}
		else if (kind == "ScriptableAsset")
		{
			if (localId == 0)
			{
				throw std::runtime_error("ScriptableAsset is missing its local file ID");
			}
			auto graph = Take(LoadScriptableAssetGraph(*resources, {guid, localId}));
			for (const auto& scriptableAsset : graph.assets)
			{
				auto dependencies = ScriptableAssetDependencies(scriptableAsset);
				if (!dependencies)
				{
					throw std::runtime_error(ToStdString(dependencies.error));
				}
				const auto validateDependency = [&](AssetReference reference)
				{
					if (reference.guid == scriptableAsset.SourceGuid() &&
					    reference.localFileId == scriptableAsset.LocalFileId())
					{
						return;
					}
					const auto& fields = scriptableAsset.Payload().at("Fields");
					std::function<void(const Json&)> visit = [&](const Json& value)
					{
						if (value.is_object() && value.contains("$ursaReference"))
						{
							const auto& record = value.at("$ursaReference");
							if (!record.contains("Resource") ||
							    record.at("Resource").value("guid", std::string{}) !=
							        ToStdString(reference.guid.ToString()) ||
							    record.at("Resource").value("localFileId", UInt64{0}) != reference.localFileId)
							{
								return;
							}
							const String referenceKind = ToUrsaString(record.at("Kind").get<std::string>());
							if (referenceKind == "Texture")
							{
								Take(resources->LoadTexture(reference.guid, reference.localFileId));
							}
							else if (referenceKind == "Material")
							{
								Take(resources->LoadMaterial(reference.guid, reference.localFileId));
							}
							else if (referenceKind == "Mesh")
							{
								Take(resources->LoadMesh(reference.guid, reference.localFileId));
							}
							else if (referenceKind == "Shader")
							{
								Take(resources->LoadShader(reference.guid));
							}
							else if (referenceKind == "ComputeShader")
							{
								Take(resources->LoadShader(reference.guid));
							}
							return;
						}
						if (value.is_structured())
						{
							for (const auto& child : value)
							{
								visit(child);
							}
						}
					};
					visit(fields);
				};
				for (const auto reference : dependencies.value)
				{
					validateDependency(reference);
				}
			}
		}
	}
	for (const auto& entry : snapshot.entries)
	{
		if (entry.path.relative.rfind("__UrsaCooked/", 0) == 0)
		{
			continue;
		}
		auto kind = ResourceKindForPath(entry.path);
		if (!kind)
		{
			continue;
		}
		const auto header = Take(ReadResourceHeader(files, entry.path));
		if (kind.value == ResourceKind::Texture)
		{
			Take(resources->LoadTexture(header.id));
		}
		else if (kind.value == ResourceKind::Material)
		{
			Take(resources->LoadMaterial(header.id));
		}
		else if (kind.value == ResourceKind::Mesh)
		{
			Take(resources->LoadMesh(header.id, 0));
		}
		else if (kind.value == ResourceKind::Shader)
		{
			Take(resources->LoadShader(header.id));
		}
		else
		{
			ComponentLoadContext context(*resources);
			if (kind.value == ResourceKind::Scene)
			{
				Take(LoadLinkedSceneResource(files, entry.path, registry, context));
			}
			else
			{
				Take(LoadPrefabResource(files, entry.path, registry, context));
			}
		}
	}
}

void AddManagedContent(ProjectWorkspace& workspace, const AssetImportOptions& options, RuntimeContentSnapshot& snapshot)
{
#if defined(URSA_ENABLE_DOTNET)
	auto& files = workspace.Index().Filesystem();
	const auto frameworkRoot = std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH).parent_path().parent_path() /
	                           ToStdString(options.configuration);
	if (!files.IsMounted("framework"))
	{
		Done(files.Mount("framework", frameworkRoot, MountAccess::ReadOnly));
	}
	Vector<String> bootstrap{
	    "Ursa.ManagedHost.dll", "Ursa.ManagedHost.deps.json", "Ursa.ManagedHost.runtimeconfig.json",
	    "Ursa.Runtime.dll",     "Ursa.Rendering.dll",         "Ursa.Bindings.dll"};
	if (options.profile == "Editor")
	{
		bootstrap.push_back("Ursa.Editor.dll");
	}
	for (const auto& name : bootstrap)
	{
		const auto source = Path("framework:" + name);
		const auto bytes = Take(files.Read(source));
		const auto blob = StoreBlob(files, bytes, ".managed");
		AddEntry(snapshot, files, Path("ursa:Managed/" + name), blob);
		if (options.profile == "Editor")
		{
			AddEntry(snapshot, files, Path("ursa:" + name), blob);
		}
		snapshot.inputs.push_back(Take(CaptureRuntimeContentInput(files, source)));
	}
	if (options.bootstrapOnly || !workspace.Database() || workspace.Database()->ScriptSources().empty())
	{
		return;
	}
	auto service = Take(ScriptBuildService::Open(files, *workspace.Database(),
	                                             std::filesystem::u8path(URSA_DOTNET_EXECUTABLE_PATH),
	                                             frameworkRoot / "Ursa.Runtime.dll", frameworkRoot / "Ursa.Editor.dll",
	                                             std::filesystem::u8path(URSA_DOTNET_CATALOG_GENERATOR_PATH),
	                                             std::filesystem::u8path(URSA_DOTNET_REFERENCE_PACK_PATH)));
	const auto group = options.profile == "Editor" ? ScriptAssemblyGroup::Editor : ScriptAssemblyGroup::Game;
	if (!options.buildScripts && !Take(service->IsBuildCurrent(group, options.configuration)))
	{
		throw std::runtime_error("Current script content is not ready; run UrsaAssetImport without --no-scripts");
	}
	const auto built = Take(service->Build(group, options.configuration));
	if (!built.succeeded)
	{
		throw std::runtime_error(ToStdString(built.diagnostics));
	}
	const String groupName = options.profile == "Editor" ? "Editor" : "Game";
	auto manifest =
	    Json::parse(ToStdString(Take(files.ReadText(Path("lib:ScriptBuild/Manifests/" + groupName + ".json")))));
	if (manifest.value("Version", 0u) != 2)
	{
		throw std::runtime_error("Script content must be rebuilt with the current binding contract");
	}
	Done(Scripting::Bindings::ValidateScriptBindingRequirements(manifest.at("RequiredBindings"),
	                                                            group == ScriptAssemblyGroup::Editor));
	for (auto& assembly : manifest["Assemblies"])
	{
		for (const char* field : {"Path", "PdbPath"})
		{
			if (!assembly.contains(field))
			{
				continue;
			}
			const auto source = Path(ToUrsaString(assembly[field].get<std::string>()));
			const auto logical = Path("prj:__UrsaManaged/" + groupName + "/" + source.FileName());
			AddEntry(snapshot, files, logical, source);
			assembly[field] = ToStdString(logical.ToString());
		}
	}
	const auto sourceCatalog = Path(ToUrsaString(manifest["TypeCatalog"].get<std::string>()));
	const auto logicalCatalog = Path("prj:__UrsaManaged/" + groupName + "/ScriptTypeCatalog.json");
	AddEntry(snapshot, files, logicalCatalog, sourceCatalog);
	manifest["TypeCatalog"] = ToStdString(logicalCatalog.ToString());
	const auto manifestBlob = StoreBlob(files, JsonBytes(manifest), ".manifest");
	AddEntry(snapshot, files, Path("prj:__UrsaManaged/" + groupName + ".json"), manifestBlob);
#else
	(void)workspace;
	(void)options;
	(void)snapshot;
#endif
}
} // namespace

Result<std::unique_ptr<ProjectWorkspace>> AssetImportPipeline::OpenProject(const std::filesystem::path& project,
                                                                           const std::filesystem::path& builtinSource)
{
	try
	{
		RuntimeMountProfileConfig profile;
		profile.mode = RuntimeMountMode::Editor;
		profile.applicationId = "UrsaAssetImport";
		profile.engineDirectories = {Take(ResolveEngineSourceRoot(builtinSource))};
		profile.projectAssetsDirectory = project / "Assets";
		profile.projectLibraryDirectory = project / "Library";
		auto files = Take(OpenRuntimeFileSystem(profile));
		auto lease = Take(files.AcquireExclusiveLease(Path("lib:Import.lock")));
		auto workspace = Take(ProjectWorkspace::FromFilesystem(project, files));
		Done(workspace->InitializeBuiltinAssets());
		return Result<std::unique_ptr<ProjectWorkspace>>::Success(std::move(workspace));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<ProjectWorkspace>>::Failure(ToUrsaString(error.what()));
	}
}

Result<AssetImportReport> AssetImportPipeline::Prepare(ProjectWorkspace& workspace, const AssetImportOptions& options,
                                                       Progress progress)
{
	try
	{
		if (options.target != "WindowsVulkan" || (options.profile != "Editor" && options.profile != "Player"))
		{
			return Result<AssetImportReport>::Failure("Unsupported asset content target or profile");
		}
		Take(RuntimeContentManifestPath(options.configuration, options.profile, options.target));
		auto& files = workspace.Index().Filesystem();
		auto lease = Take(files.AcquireExclusiveLease(Path("lib:Import.lock")));
		Done(workspace.Database()->ReloadImports());
		if (!workspace.BuiltinDatabase())
		{
			Done(workspace.InitializeBuiltinAssets());
		}
		Done(workspace.BuiltinDatabase()->ReloadImports());
		Vector<AssetRecord> assets = workspace.BuiltinDatabase()->Assets();
		const auto projectAssets = workspace.Database()->Assets();
		if (!options.bootstrapOnly)
		{
			assets.insert(assets.end(), projectAssets.begin(), projectAssets.end());
		}
		bool hasScriptableAssets = false;
		for (const auto& asset : assets)
		{
			hasScriptableAssets |=
			    !asset.metadata.folder && PathFromUrsaString(asset.path.relative).extension() == ".ursaasset";
		}
		Vector<ScriptTypeRecord> scriptTypes;
		if (hasScriptableAssets)
		{
#if defined(URSA_ENABLE_DOTNET)
			const auto frameworkRoot =
			    std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH).parent_path().parent_path() /
			    ToStdString(options.configuration);
			auto service = Take(ScriptBuildService::Open(
			    files, *workspace.Database(), std::filesystem::u8path(URSA_DOTNET_EXECUTABLE_PATH),
			    frameworkRoot / "Ursa.Runtime.dll", frameworkRoot / "Ursa.Editor.dll",
			    std::filesystem::u8path(URSA_DOTNET_CATALOG_GENERATOR_PATH),
			    std::filesystem::u8path(URSA_DOTNET_REFERENCE_PACK_PATH)));
			Take(service->GenerateProjects());
			scriptTypes = service->ScriptTypes();
#else
			return Result<AssetImportReport>::Failure("ScriptableAsset content requires the .NET script type catalog");
#endif
		}
		Vector<RuntimeContentInput> settingsInputs;
		if (!options.bootstrapOnly && files.IsMounted(projectSettingsScheme))
		{
			for (const auto& entry : Take(files.List(projectSettingsScheme)))
			{
				settingsInputs.push_back(Take(CaptureRuntimeContentInput(files, entry.path)));
			}
		}
		const auto graphics =
		    options.bootstrapOnly ? Rendering::GraphicsSettings{} : Take(Rendering::ReadGraphicsSettings(files));
		Done(Rendering::ValidatePipelineAsset(files, workspace.AssetResolver(), scriptTypes, graphics.renderPipeline));
		Set<String> sourceInventory;
		for (const String scheme : {String(engineScheme), String(projectScheme)})
		{
			if (options.bootstrapOnly && scheme == projectScheme)
			{
				continue;
			}
			for (const auto& entry : Take(files.List(scheme)))
			{
				sourceInventory.insert(entry.path.ToString());
			}
		}
		std::stable_sort(assets.begin(), assets.end(), [](const auto& left, const auto& right)
		                 { return IsShader(left.path) && !IsShader(right.path); });
		AssetImportReport report;
		report.snapshot.configuration = options.configuration;
		report.snapshot.profile = options.profile;
		if (options.bootstrapOnly)
		{
			report.snapshot.profile = "EditorBootstrap";
		}
		report.snapshot.target = options.target;
		const auto total = static_cast<UInt64>(
		    std::count_if(assets.begin(), assets.end(),
		                  [&](const AssetRecord& asset)
		                  {
			                  return !asset.metadata.folder && RuntimeAsset(asset) &&
			                         (!options.bootstrapOnly || BootstrapAsset(asset)) &&
			                         !(options.profile == "Player" && asset.path.scheme == engineScheme &&
			                           (asset.path.relative.rfind("Shaders/Editor/", 0) == 0 ||
			                            asset.path.relative.rfind("Fonts/Editor/", 0) == 0));
		                  }));
		ProjectAssetImporter importer;
		if (hasScriptableAssets)
		{
			Done(importer.RegisterScriptableAssets(std::move(scriptTypes)));
		}
		Vector<std::pair<AssetDatabase*, VirtualPath>> importedSources;
		Json catalog{{"format", "UrsaAssetCatalog"}, {"version", 1u}, {"assets", Json::array()}};
		UInt64 processed = 0;
		for (const auto& asset : assets)
		{
			if (asset.metadata.folder || !RuntimeAsset(asset) || (options.bootstrapOnly && !BootstrapAsset(asset)))
			{
				continue;
			}
			if (options.profile == "Player" && asset.path.scheme == engineScheme &&
			    (asset.path.relative.rfind("Shaders/Editor/", 0) == 0 ||
			     asset.path.relative.rfind("Fonts/Editor/", 0) == 0))
			{
				continue;
			}
			auto* database = asset.path.scheme == engineScheme ? workspace.BuiltinDatabase() : workspace.Database();
			const auto current = Take(database->CurrentImport(asset.path));
			bool selected = options.scope.relative.empty() || asset.path == options.scope ||
			                (asset.path.scheme == options.scope.scheme &&
			                 asset.path.relative.rfind(options.scope.relative + "/", 0) == 0);
			for (const auto& dependency : current.dependencies)
			{
				selected = selected || dependency == options.scope;
			}
			AssetImportResult imported;
			if (selected)
			{
				imported = Take(importer.Import(*database, asset.path, options.force));
			}
			else
			{
				if (!current.upToDate)
				{
					return Result<AssetImportReport>::Failure(
					    "Runtime content outside requested scope is not ready: " + asset.path.ToString() +
					    "; run a full project import first");
				}
				imported = {asset.metadata.guid, current.artifacts, current.dependencies, true};
			}
			report.importedAssets += imported.cacheHit ? 0 : 1;
			importedSources.push_back({database, asset.path});
			report.cacheHits += imported.cacheHit ? 1 : 0;
			if (progress)
			{
				progress(++processed, total, asset.path);
			}
			for (const auto& artifact : imported.artifacts)
			{
				String relative = asset.path.relative;
				if (artifact.localFileId == 0)
				{
					const char* extension = artifact.kind == "Shader"    ? ".ursashaderasset"
					                        : artifact.kind == "Model"   ? ".ursamodel"
					                        : artifact.kind == "Texture" ? ".ursatex"
					                                                     : nullptr;
					if (extension)
					{
						auto output = PathFromUrsaString(relative);
						output.replace_extension(extension);
						relative = PathToUrsaString(output);
					}
				}
				else
				{
					relative = "__UrsaCooked/" + imported.guid.ToString() + "/" +
					           ToUrsaString(std::to_string(artifact.localFileId)) +
					           PathToUrsaString(PathFromUrsaString(artifact.path.relative).extension());
				}
				const auto logical = Path(asset.path.scheme + ":" + relative);
				AddEntry(report.snapshot, files, logical, artifact.path);
				catalog["assets"].push_back({{"guid", ToStdString(imported.guid.ToString())},
				                             {"localFileId", artifact.localFileId},
				                             {"kind", ToStdString(artifact.kind)},
				                             {"path", ToStdString(logical.ToString())}});
			}
		}
		const auto catalogBlob = StoreBlob(files, JsonBytes(catalog), ".catalog");
		AddEntry(report.snapshot, files, Path("prj:__UrsaCooked/AssetCatalog.ursacatalog"), catalogBlob);
		AddManagedContent(workspace, options, report.snapshot);
		if (!options.bootstrapOnly)
		{
			const auto graphicsBlob = StoreBlob(files, Take(Rendering::EncodeGraphicsSettings(graphics)), ".graphics");
			AddEntry(report.snapshot, files, Path(Rendering::graphicsSettingsRuntime), graphicsBlob);
			if (files.IsMounted(projectSettingsScheme))
			{
				const auto currentSettings = Take(files.List(projectSettingsScheme));
				if (currentSettings.size() != settingsInputs.size())
				{
					return Result<AssetImportReport>::Failure(
					    "Project settings changed during import; refresh and retry");
				}
				for (const auto& input : settingsInputs)
				{
					const auto current = Take(CaptureRuntimeContentInput(files, input.path));
					if (current.fingerprint != input.fingerprint)
					{
						return Result<AssetImportReport>::Failure("Project settings changed during import: " +
						                                          input.path.ToString());
					}
					report.snapshot.inputs.push_back(input);
				}
				report.snapshot.completeSourceSchemes.push_back(projectSettingsScheme);
			}
		}
		for (const String scheme : {String(engineScheme), String(projectScheme)})
		{
			if (options.bootstrapOnly && scheme == projectScheme)
			{
				continue;
			}
			for (const auto& entry : Take(files.List(scheme)))
			{
				if (!sourceInventory.erase(entry.path.ToString()))
				{
					return Result<AssetImportReport>::Failure(
					    "Source inventory changed during import; refresh and retry");
				}
				report.snapshot.inputs.push_back(Take(CaptureRuntimeContentInput(files, entry.path)));
			}
			report.snapshot.completeSourceSchemes.push_back(scheme);
		}
		if (!sourceInventory.empty())
		{
			return Result<AssetImportReport>::Failure("Source deleted during import; refresh and retry");
		}
		for (const auto& source : importedSources)
		{
			if (!Take(source.first->CurrentImport(source.second)).upToDate)
			{
				return Result<AssetImportReport>::Failure("Source changed during import: " + source.second.ToString() +
				                                          "; refresh and retry");
			}
		}
		report.runtimeFiles = Take(OpenRuntimeContentView(std::make_shared<VirtualFileSystem>(files), report.snapshot));
		ValidateResourceClosure(report.runtimeFiles, report.snapshot);
		Done(PublishRuntimeContent(files, report.snapshot));
		return Result<AssetImportReport>::Success(std::move(report));
	}
	catch (const std::exception& error)
	{
		return Result<AssetImportReport>::Failure(ToUrsaString(error.what()));
	}
}

Result<String> AssetImportPipeline::SourceFingerprint(const ProjectWorkspace& workspace)
{
	return SourceFingerprint(workspace.Index().Filesystem());
}

Result<String> AssetImportPipeline::SourceFingerprint(const VirtualFileSystem& files)
{
	try
	{
		Vector<UInt8> data;
		for (const String scheme : {String(engineScheme), String(projectScheme), String(projectSettingsScheme)})
		{
			if (!files.IsMounted(scheme))
			{
				continue;
			}
			for (const auto& entry : Take(files.List(scheme)))
			{
				const auto input = Take(CaptureRuntimeContentInput(files, entry.path));
				const auto signature = input.path.ToString() + ":" + input.fingerprint;
				data.insert(data.end(), signature.data(), signature.data() + signature.size());
			}
		}
		return Result<String>::Success(BuildResourceFingerprint(data));
	}
	catch (const std::exception& error)
	{
		return Result<String>::Failure(ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
