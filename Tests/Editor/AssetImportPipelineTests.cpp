#include <Assets/AssetImportPipeline.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Core/StringInterop.hpp>
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
