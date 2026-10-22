#include <Assets/AssetImportPipeline.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <Rendering/Shader/ComputeKernel.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/PakArchive.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

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

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* text)
{
	return Take(VirtualPath::Parse(text));
}

void Text(VirtualFileSystem& files, const VirtualPath& path, const String& text)
{
	Done(files.WriteAtomic(path, Vector<UInt8>(text.data(), text.data() + text.size())));
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem sources;
		auto builtin = std::make_shared<MemoryFileProvider>();
		Done(sources.MountProvider(engineScheme, builtin, MountAccess::ReadWrite));
		Text(sources, Path("ursa:Builtin.txt"), "immutable builtin");
		const auto builtinGuid = ResourceId::Generate();
		Text(sources, Path("ursa:Builtin.txt.meta"),
		     ToUrsaString(
		         nlohmann::json{{"version", 1}, {"guid", ToStdString(builtinGuid.ToString())}, {"folder", false}}
		             .dump()));
		for (const char* file : {"Icons.meta", "Icons/Editor.meta", "Icons/Editor/EditorIcons.png.meta",
		                         "Icons/Editor/EditorIcons.json.meta", "Icons/Editor/Unused.svg.meta"})
		{
			const String name(file);
			Text(sources, Path((String("ursa:") + file).c_str()),
			     ToUrsaString(nlohmann::json{{"version", 1},
			                                 {"guid", ToStdString(ResourceId::Generate().ToString())},
			                                 {"folder", name == "Icons.meta" || name == "Icons/Editor.meta"}}
			                      .dump()));
		}
		Text(sources, Path("ursa:Icons/Editor/EditorIcons.png"), "raw PNG payload");
		Text(sources, Path("ursa:Icons/Editor/EditorIcons.json"), "icon UV manifest");
		Text(sources, Path("ursa:Icons/Editor/Unused.svg"), "maintenance-only vector source");
		sources = {};
		Done(sources.MountProvider(engineScheme, builtin, MountAccess::ReadOnly));
		Done(sources.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(
		    sources.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto textureGuid = ResourceId::Generate();
		const auto materialGuid = ResourceId::Generate();
		auto texture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 0, 0, 255}));
		Done(SaveTextureResource(sources, Path("prj:Color.ursatex"), textureGuid, *texture));
		MaterialInstance material(MaterialType::Unlit);
		Done(material.SetTexture(MaterialTextureSlot::BaseColor, texture));
		Done(SaveMaterialResource(sources, Path("prj:Color.ursamat"), materialGuid, material,
		                          [textureGuid](MaterialTexturePtr)
		                          { return Result<ResourceId>::Success(textureGuid); }));
		Text(sources, Path("prj:Other.txt"), "unrelated");
		auto workspace = Take(ProjectWorkspace::FromFilesystem({}, sources));
		Done(workspace->InitializeBuiltinAssets());
		UInt32 projectImporterCalls = 0;
		ProjectAssetImporter nativeImporter;
		Done(workspace->Importer().Register(".txt",
		                                    [&](AssetDatabase& database, const VirtualPath& path, bool force)
		                                    {
			                                    ++projectImporterCalls;
			                                    return nativeImporter.Import(database, path, force);
		                                    }));
		AssetImportOptions options;
		options.buildScripts = false;
		auto first = Take(AssetImportPipeline::Prepare(*workspace, options));
		Expect(projectImporterCalls == 2, "Project import pipeline bypassed the workspace importer service");
		Expect(Take(first.runtimeFiles.Read(Path("ursa:Icons/Editor/EditorIcons.png"))) ==
		               Take(sources.Read(Path("ursa:Icons/Editor/EditorIcons.png"))) &&
		           Take(first.runtimeFiles.Exists(Path("ursa:Icons/Editor/EditorIcons.json"))) &&
		           !Take(first.runtimeFiles.Exists(Path("ursa:Icons/Editor/Unused.svg"))),
		       "Editor icon publication must retain raw payload paths and exclude vector originals");
		AssetImportOptions iconOptions = options;
		iconOptions.bootstrapOnly = true;
		const auto bootstrap = Take(AssetImportPipeline::Prepare(*workspace, iconOptions));
		Expect(Take(bootstrap.runtimeFiles.Exists(Path("ursa:Icons/Editor/EditorIcons.png"))),
		       "Editor bootstrap omitted its icon resources");
		iconOptions.bootstrapOnly = false;
		iconOptions.profile = "Player";
		const auto player = Take(AssetImportPipeline::Prepare(*workspace, iconOptions));
		Expect(!Take(player.runtimeFiles.Exists(Path("ursa:Icons/Editor/EditorIcons.png"))),
		       "Player content unexpectedly includes Editor icon resources");
		Expect(first.importedAssets == 4, "Cold import did not prepare builtin and native project assets");
		auto resources = Take(ResourceSession::Open(first.runtimeFiles));
		auto oldMaterial = Take(resources->LoadMaterial(materialGuid));
		Expect(oldMaterial->Texture(MaterialTextureSlot::BaseColor)->GetPixels()[0] == 255,
		       "Native asset reference did not resolve");
		auto repeated = Take(AssetImportPipeline::Prepare(*workspace, options));
		Expect(repeated.importedAssets == 0 && repeated.cacheHits == 4, "Warm import missed cache");
		auto lease = Take(sources.AcquireExclusiveLease(Path("lib:Import.lock")));
		Expect(!AssetImportPipeline::Prepare(*workspace, options), "Concurrent writer was accepted");
		lease.reset();
		const auto manifest = Take(RuntimeContentManifestPath(options.configuration, options.profile));
		const auto previous = Take(sources.Read(manifest));
		bool modified = false;
		auto stale =
		    AssetImportPipeline::Prepare(*workspace, options,
		                                 [&](UInt64, UInt64, const VirtualPath& path)
		                                 {
			                                 if (!modified && path == Path("prj:Other.txt"))
			                                 {
				                                 modified = true;
				                                 Text(sources, Path("prj:Other.txt"), "new input during import");
			                                 }
		                                 });
		Expect(!stale && Take(sources.Read(manifest)) == previous,
		       "Mixed generation replaced last complete publication");
		options.scope = Path("prj:Other.txt");
		auto scoped = Take(AssetImportPipeline::Prepare(*workspace, options));
		Expect(scoped.importedAssets == 1, "Scoped import rebuilt unrelated resources");
		Text(sources, Path("prj:Added.txt"), "new source");
		Expect(!ReadRuntimeContent(sources, options.configuration, options.profile, options.target, &sources),
		       "New source was not detected by readiness validation");
		Done(workspace->Refresh());
		options.scope = {};
		Take(AssetImportPipeline::Prepare(*workspace, options));
		auto blue = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {0, 0, 255, 255}));
		Done(SaveTextureResource(sources, Path("prj:Color.ursatex"), textureGuid, *blue));
		auto updated = Take(AssetImportPipeline::Prepare(*workspace, options));
		Done(resources->ReloadImportedResources(updated.runtimeFiles));
		Expect(Take(resources->LoadMaterial(materialGuid)) == oldMaterial &&
		           oldMaterial->Texture(MaterialTextureSlot::BaseColor)->GetPixels()[2] == 255,
		       "Live scene material identity or updated texture was lost");
		VirtualFileSystem broken;
		for (const auto& provider : updated.runtimeFiles.Providers())
		{
			if (provider.scheme != projectScheme)
			{
				Done(broken.MountProvider(provider.scheme, provider.provider, provider.access));
			}
		}
		Done(broken.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadOnly));
		Expect(!resources->ReloadImportedResources(broken) &&
		           oldMaterial->Texture(MaterialTextureSlot::BaseColor)->GetPixels()[2] == 255,
		       "Failed reload destroyed last good display resources");
		VirtualFileSystem output;
		Done(output.MountProvider("out", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(PakBuilder::Build(updated.runtimeFiles, {engineScheme, projectScheme}, output, Path("out:Content.pak")));
		const auto package = Take(output.Read(Path("out:Content.pak")));
		Done(PakBuilder::Build(updated.runtimeFiles, {engineScheme, projectScheme}, output, Path("out:Content.pak")));
		Expect(package == Take(output.Read(Path("out:Content.pak"))), "Prepared content package was not deterministic");
		const auto scriptableRoot = std::filesystem::current_path() / "ScriptableImportTest" /
		                            ("UrsaScriptableImport-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(scriptableRoot);
		std::filesystem::create_directories(scriptableRoot / "Assets");

		struct ScriptableCleanup
		{
			std::filesystem::path path;

			~ScriptableCleanup()
			{
				std::error_code error;
				std::filesystem::remove_all(path, error);
			}
		} scriptableCleanup{scriptableRoot};

		auto scriptableWorkspace = Take(
		    ProjectWorkspace::Open(scriptableRoot, {{engineScheme, URSA_TEST_ENGINE_SOURCE, MountAccess::ReadOnly}}));
		Done(scriptableWorkspace->InitializeBuiltinAssets());
		auto& scriptableFiles = scriptableWorkspace->Index().Filesystem();
		const auto scriptMaterialGuid = ResourceId::Generate();
		const auto scriptTextureGuid = ResourceId::Generate();
		const auto scriptTexture = Take(MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {40, 80, 120, 255}));
		Done(SaveTextureResource(scriptableFiles, Path("prj:Dependency.ursatex"), scriptTextureGuid, *scriptTexture));
		MaterialInstance scriptMaterial(MaterialType::Unlit);
		Done(scriptMaterial.SetTexture(MaterialTextureSlot::BaseColor, scriptTexture));
		Done(SaveMaterialResource(scriptableFiles, Path("prj:Dependency.ursamat"), scriptMaterialGuid, scriptMaterial,
		                          [scriptTextureGuid](MaterialTexturePtr)
		                          { return Result<ResourceId>::Success(scriptTextureGuid); }));
		Text(scriptableFiles, Path("prj:Scripts/Settings.cs"),
		     "using Ursa; using Ursa.Rendering; namespace Demo; [UrsaScriptType(\"demo.render-settings\")] "
		     "public sealed class RenderSettings : RenderPipelineAsset { public Shader shader = null!; "
		     "public Material material = null!; public int quality = 3; public int[] weights = new[] { 1, 2 }; "
		     "protected override RenderPipeline CreatePipeline() => new "
		     "ForwardRenderPipeline(); } "
		     "[UrsaScriptType(\"demo.other-settings\")] public sealed class OtherSettings : ScriptableObject {}\n");
		const auto shaderPath = Path("prj:Shaders/Dependency.ursashader");
		const String shaderSource = "Shader \"Tests/Dependency\" { SubShader { Pass { HLSLPROGRAM\n"
		                            "#pragma vertex VSMain\n#pragma fragment PSMain\n"
		                            "float4 VSMain(float3 p: POSITION): SV_Position { return float4(p,1); }\n"
		                            "float4 PSMain(): SV_Target0 { return float4(1,1,1,1); }\nENDHLSL } } }";
		Text(scriptableFiles, shaderPath, shaderSource);
		const auto computePath = Path("prj:Shaders/Pattern.compute");
		Text(scriptableFiles, computePath,
		     "#pragma kernel Fill\n[[vk::image_format(\"rgba8\")]] RWTexture2D<float4> _Output;\n"
		     "[numthreads(8, 8, 1)] void Fill(uint3 id: SV_DispatchThreadID) { _Output[id.xy] = float4(1,0,0,1); }\n");
		Done(scriptableWorkspace->Refresh());
		options.buildScripts = true;
		auto scriptBuild = Take(AssetImportPipeline::Prepare(*scriptableWorkspace, options));
		const auto typeCatalog = nlohmann::json::parse(
		    ToStdString(Take(scriptableFiles.ReadText(Path("lib:ScriptBuild/Catalog/ScriptTypeCatalog.json")))));
		const auto type =
		    std::find_if(typeCatalog.at("Scripts").begin(), typeCatalog.at("Scripts").end(),
		                 [](const auto& record) { return record.at("ScriptType") == "demo.render-settings"; });
		Expect(type != typeCatalog.at("Scripts").end(), "Script build omitted the ScriptableObject type");
		Expect(type->at("IsRenderPipeline").get<bool>(), "Catalog did not identify the derived pipeline asset");
		const auto settingsPath = Path("prj:Settings.ursaasset");
		Text(scriptableFiles, settingsPath, "{}");
		Done(scriptableWorkspace->Refresh());
		const auto settingsMetadata = Take(scriptableWorkspace->Database()->Metadata(settingsPath));
		const auto typeMetadata = Take(scriptableWorkspace->Database()->Metadata(Path("prj:Scripts/Settings.cs")));
		const auto shaderMetadata = Take(scriptableWorkspace->Database()->Metadata(shaderPath));
		const auto computeMetadata = Take(scriptableWorkspace->Database()->Metadata(computePath));
		const auto materialReference =
		    nlohmann::json{{"$ursaReference",
		                    {{"Kind", "Material"},
		                     {"Resource",
		                      {{"kind", "asset"},
		                       {"asset", {{"guid", scriptMaterialGuid.ToString().c_str()}, {"localFileId", 0}}}}}}}};
		const auto shaderReference =
		    nlohmann::json{{"$ursaReference",
		                    {{"Kind", "Shader"},
		                     {"Resource", {{"guid", shaderMetadata.guid.ToString().c_str()}, {"localFileId", 0}}}}}};
		nlohmann::json payload{{"SourceGuid", typeMetadata.guid.ToString().c_str()},
		                       {"LocalFileId", type->at("LocalFileId").get<UInt64>()},
		                       {"Fields", {{"shader", shaderReference}, {"material", materialReference}}},
		                       {"FieldTypes",
		                        {{"shader",
		                          {{"Kind", "ShaderReference"},
		                           {"ElementKind", "Unsupported"},
		                           {"TypeName", "Ursa.Shader"},
		                           {"ElementTypeName", ""}}},
		                         {"material",
		                          {{"Kind", "MaterialReference"},
		                           {"ElementKind", "Unsupported"},
		                           {"TypeName", "Ursa.Material"},
		                           {"ElementTypeName", ""}}}}}};
		ScriptableAsset settings;
		payload["Fields"]["quality"] = 3;
		payload["Fields"]["weights"] = nlohmann::json::array({1, 2});
		payload["FieldTypes"]["quality"] = {
		    {"Kind", "SignedInteger"}, {"ElementKind", "Unsupported"}, {"TypeName", "int"}, {"ElementTypeName", ""}};
		payload["FieldTypes"]["weights"] = {{"Kind", "Array"},
		                                    {"ElementKind", "SignedInteger"},
		                                    {"TypeName", "System.Int32[]"},
		                                    {"ElementTypeName", "System.Int32"}};
		Done(settings.SetDescription(settingsMetadata.guid, 1, "demo.render-settings", 1, std::move(payload)));
		Done(SaveScriptableAssetSource(scriptableFiles, settingsPath, settings));
		Done(scriptableWorkspace->Refresh());
		ScriptTypeDescriptor pipelineType;
		pipelineType.sourceGuid = typeMetadata.guid;
		pipelineType.localFileId = type->at("LocalFileId").get<UInt64>();
		pipelineType.scriptType = "demo.render-settings";
		pipelineType.kind = ScriptTypeKind::ScriptableObject;
		pipelineType.isRenderPipeline = true;
		const auto validatePipeline = [&](AssetReference reference)
		{
			return Rendering::ValidatePipelineAsset(scriptableFiles, scriptableWorkspace->AssetResolver(),
			                                        {pipelineType}, reference);
		};
		Rendering::GraphicsSettings graphics{{settingsMetadata.guid, 1}};
		Done(Rendering::SaveGraphicsSettings(scriptableFiles, graphics, validatePipeline));
		const auto settingsSourcePath = Path(Rendering::graphicsSettingsSource);
		const auto selectedSettingsBytes = Take(scriptableFiles.Read(settingsSourcePath));
		Expect(!Rendering::SaveGraphicsSettings(scriptableFiles, {{scriptMaterialGuid, 1}}, validatePipeline) &&
		           Take(scriptableFiles.Read(settingsSourcePath)) == selectedSettingsBytes,
		       "Selecting a material replaced the valid Graphics configuration");
		Expect(!Rendering::EncodeGraphicsSettings({{settingsMetadata.guid, 0}}) &&
		           !Rendering::EncodeGraphicsSettings({{{}, 1}}),
		       "Graphics settings accepted an incomplete stable asset identity");
		const auto otherType =
		    std::find_if(typeCatalog.at("Scripts").begin(), typeCatalog.at("Scripts").end(),
		                 [](const auto& record) { return record.at("ScriptType") == "demo.other-settings"; });
		Expect(otherType != typeCatalog.at("Scripts").end() && !otherType->at("IsRenderPipeline").get<bool>(),
		       "Catalog accepted an ordinary ScriptableObject as a pipeline");
		const auto otherPath = Path("prj:OtherSettings.ursaasset");
		Text(scriptableFiles, otherPath, "{}");
		Done(scriptableWorkspace->Refresh());
		const auto otherMetadata = Take(scriptableWorkspace->Database()->Metadata(otherPath));
		ScriptableAsset otherSettings;
		Done(otherSettings.SetDescription(otherMetadata.guid, 1, "demo.other-settings", 1,
		                                  {{"SourceGuid", typeMetadata.guid.ToString().c_str()},
		                                   {"LocalFileId", otherType->at("LocalFileId").get<UInt64>()},
		                                   {"Fields", nlohmann::json::object()},
		                                   {"FieldTypes", nlohmann::json::object()}}));
		Done(SaveScriptableAssetSource(scriptableFiles, otherPath, otherSettings));
		Expect(!Rendering::SaveGraphicsSettings(scriptableFiles, {{otherMetadata.guid, 1}}, validatePipeline) &&
		           Take(scriptableFiles.Read(settingsSourcePath)) == selectedSettingsBytes,
		       "Selecting a non-pipeline ScriptableObject replaced the valid configuration");
		auto scriptableImport = Take(AssetImportPipeline::Prepare(*scriptableWorkspace, options));
		Expect(Take(Rendering::ReadGraphicsSettings(scriptableImport.runtimeFiles, true)).renderPipeline.guid ==
		           settingsMetadata.guid,
		       "Loose publication omitted the selected pipeline GUID");
		auto reopened = Take(ProjectWorkspace::Open(scriptableRoot));
		Expect(Take(Rendering::ReadGraphicsSettings(reopened->Index().Filesystem())).renderPipeline.guid ==
		           settingsMetadata.guid,
		       "Reopening the project lost its Graphics selection");
		Done(scriptableWorkspace->Database()->Move(settingsPath, Path("prj:MovedPipeline.ursaasset")));
		Done(scriptableWorkspace->Refresh());
		Done(validatePipeline(graphics.renderPipeline));
		scriptableImport = Take(AssetImportPipeline::Prepare(*scriptableWorkspace, options));
		Expect(Take(Rendering::ReadGraphicsSettings(scriptableImport.runtimeFiles, true)).renderPipeline.guid ==
		           settingsMetadata.guid,
		       "Moving the pipeline source changed its published selection");
		Expect(scriptableImport.importedAssets > 0 && scriptBuild.importedAssets > 0,
		       "ScriptableAsset import did not rebuild and publish project content");
		auto scriptableResources = Take(ResourceSession::Open(scriptableImport.runtimeFiles));
		const auto compute = Take(scriptableResources->LoadShader(computeMetadata.guid));
		const auto kernelIndex = Take(Rendering::FindComputeKernel(*compute, "Fill"));
		const auto* kernel = Take(Rendering::ComputeKernelAt(*compute, kernelIndex));
		Expect(kernel->threadGroupSize == Array<UInt32, 3>{8, 8, 1},
		       "Project .compute source or kernel reflection was omitted from runtime publication");
		Expect(Take(scriptableResources->LoadMaterial(scriptMaterialGuid))->Texture(MaterialTextureSlot::BaseColor) !=
		               nullptr &&
		           Take(scriptableResources->LoadShader(shaderMetadata.guid))->id == shaderMetadata.guid,
		       "ScriptableAsset material or Shader dependency was not published for loose runtime loading");
		auto settingsGraph = Take(LoadScriptableAssetGraph(*scriptableResources, {settingsMetadata.guid, 1}));
		Expect(settingsGraph.assets.size() == 1 && settingsGraph.assets[0].TypeId() == "demo.render-settings",
		       "Loose runtime did not load the imported ScriptableAsset artifact");
		VirtualFileSystem scriptablePackageOutput;
		Done(scriptablePackageOutput.MountProvider("out", std::make_shared<MemoryFileProvider>(),
		                                           MountAccess::ReadWrite));
		Done(PakBuilder::Build(scriptableImport.runtimeFiles, {engineScheme, projectScheme}, scriptablePackageOutput,
		                       Path("out:ScriptableContent.pak")));
		const auto scriptablePackage = Take(scriptablePackageOutput.Read(Path("out:ScriptableContent.pak")));
		const auto scriptablePackagePath = scriptableRoot / "ScriptableContent.pak";
		{
			std::ofstream packageFile(scriptablePackagePath, std::ios::binary | std::ios::trunc);
			Expect(static_cast<bool>(packageFile), "Cannot create ScriptableAsset Pak fixture");
			packageFile.write(reinterpret_cast<const char*>(scriptablePackage.data()),
			                  static_cast<std::streamsize>(scriptablePackage.size()));
			Expect(static_cast<bool>(packageFile), "Cannot write ScriptableAsset Pak fixture");
		}
		auto scriptableArchive = Take(PakArchive::Open(scriptablePackagePath));
		VirtualFileSystem packedScriptableFiles;
		Done(scriptableArchive->Mount(packedScriptableFiles));
		const auto packedGraphics = Take(Rendering::ReadGraphicsSettings(packedScriptableFiles, true));
		Expect(packedGraphics.renderPipeline.guid == graphics.renderPipeline.guid &&
		           packedGraphics.renderPipeline.localFileId == 1,
		       "Pak and loose content disagreed about the selected Graphics pipeline");
		auto packedScriptableResources = Take(ResourceSession::Open(packedScriptableFiles));
		Expect(Take(Rendering::FindComputeKernel(*Take(packedScriptableResources->LoadShader(computeMetadata.guid)),
		                                         "Fill")) == kernelIndex,
		       "Pak publication omitted the project .compute kernel");
		Expect(
		    Take(packedScriptableResources->LoadMaterial(scriptMaterialGuid))
		                ->Texture(MaterialTextureSlot::BaseColor) != nullptr &&
		        Take(packedScriptableResources->LoadShader(shaderMetadata.guid))->id == shaderMetadata.guid &&
		        Take(LoadScriptableAssetGraph(*packedScriptableResources, {settingsMetadata.guid, 1})).assets.size() ==
		            1,
		    "Pak runtime could not load ScriptableAsset, Shader and Material dependencies");
		const auto publishedManifest = Take(RuntimeContentManifestPath(options.configuration, options.profile));
		const auto goodPublication = Take(scriptableFiles.Read(publishedManifest));
		Done(scriptableFiles.WriteAtomic(settingsSourcePath,
		                                 Take(Rendering::EncodeGraphicsSettings({{otherMetadata.guid, 1}}))));
		Expect(!AssetImportPipeline::Prepare(*scriptableWorkspace, options) &&
		           Take(scriptableFiles.Read(publishedManifest)) == goodPublication,
		       "Invalid pipeline type was published over the last valid content");
		Done(Rendering::SaveGraphicsSettings(scriptableFiles, {}));
		Expect(!ReadRuntimeContent(scriptableFiles, options.configuration, options.profile, options.target,
		                           &scriptableFiles),
		       "Changing Graphics settings did not invalidate runtime readiness");
		const auto clearedImport = Take(AssetImportPipeline::Prepare(*scriptableWorkspace, options));
		Expect(!Take(Rendering::ReadGraphicsSettings(clearedImport.runtimeFiles, true)).renderPipeline.guid,
		       "Clearing the selection did not publish the default pipeline choice");
		std::cout << "Shared pipeline isolation, scope, concurrent writer, source generation, live references and "
		             "package tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
