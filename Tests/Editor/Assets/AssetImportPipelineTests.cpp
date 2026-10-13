#include <Assets/AssetImportPipeline.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
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
		AssetImportOptions options;
		options.buildScripts = false;
		auto first = Take(AssetImportPipeline::Prepare(*workspace, options));
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
		     "using Ursa; namespace Demo; [UrsaScriptType(\"demo.render-settings\")] "
		     "public sealed class RenderSettings : ScriptableObject { public Shader shader = null!; "
		     "public Material material = null!; }\n");
		const auto shaderPath = Path("prj:Shaders/Dependency.ursashader");
		const String shaderSource = "Shader \"Tests/Dependency\" { SubShader { Pass { HLSLPROGRAM\n"
		                            "#pragma vertex VSMain\n#pragma fragment PSMain\n"
		                            "float4 VSMain(float3 p: POSITION): SV_Position { return float4(p,1); }\n"
		                            "float4 PSMain(): SV_Target0 { return float4(1,1,1,1); }\nENDHLSL } } }";
		Text(scriptableFiles, shaderPath, shaderSource);
		Done(scriptableWorkspace->Refresh());
		options.buildScripts = true;
		auto scriptBuild = Take(AssetImportPipeline::Prepare(*scriptableWorkspace, options));
		const auto typeCatalog = nlohmann::json::parse(
		    ToStdString(Take(scriptableFiles.ReadText(Path("lib:ScriptBuild/Catalog/ScriptTypeCatalog.json")))));
		const auto type =
		    std::find_if(typeCatalog.at("Scripts").begin(), typeCatalog.at("Scripts").end(),
		                 [](const auto& record) { return record.at("ScriptType") == "demo.render-settings"; });
		Expect(type != typeCatalog.at("Scripts").end(), "Script build omitted the ScriptableObject type");
		const auto settingsPath = Path("prj:Settings.ursaasset");
		Text(scriptableFiles, settingsPath, "{}");
		Done(scriptableWorkspace->Refresh());
		const auto settingsMetadata = Take(scriptableWorkspace->Database()->Metadata(settingsPath));
		const auto typeMetadata = Take(scriptableWorkspace->Database()->Metadata(Path("prj:Scripts/Settings.cs")));
		const auto shaderMetadata = Take(scriptableWorkspace->Database()->Metadata(shaderPath));
		const auto materialReference =
		    nlohmann::json{{"$ursaReference",
		                    {{"Kind", "Material"},
		                     {"Resource", {{"guid", scriptMaterialGuid.ToString().c_str()}, {"localFileId", 0}}}}}};
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
		Done(settings.SetDescription(settingsMetadata.guid, 1, "demo.render-settings", 1, std::move(payload)));
		Done(SaveScriptableAssetSource(scriptableFiles, settingsPath, settings));
		Done(scriptableWorkspace->Refresh());
		auto scriptableImport = Take(AssetImportPipeline::Prepare(*scriptableWorkspace, options));
		Expect(scriptableImport.importedAssets > 0 && scriptBuild.importedAssets > 0,
		       "ScriptableAsset import did not rebuild and publish project content");
		auto scriptableResources = Take(ResourceSession::Open(scriptableImport.runtimeFiles));
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
		auto packedScriptableResources = Take(ResourceSession::Open(packedScriptableFiles));
		Expect(
		    Take(packedScriptableResources->LoadMaterial(scriptMaterialGuid))
		                ->Texture(MaterialTextureSlot::BaseColor) != nullptr &&
		        Take(packedScriptableResources->LoadShader(shaderMetadata.guid))->id == shaderMetadata.guid &&
		        Take(LoadScriptableAssetGraph(*packedScriptableResources, {settingsMetadata.guid, 1})).assets.size() ==
		            1,
		    "Pak runtime could not load ScriptableAsset, Shader and Material dependencies");
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
