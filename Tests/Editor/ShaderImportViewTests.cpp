#include <Assets/ShaderImportView.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/PakArchive.hpp>
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

struct TemporaryDirectory
{
	std::filesystem::path path = std::filesystem::temp_directory_path() /
	                             std::filesystem::u8path("UrsaEditorShaderImport-") /
	                             std::filesystem::u8path(ToStdString(ResourceId::Generate().ToString()));

	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}
};

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void WriteText(const std::filesystem::path& path, const String& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!file)
	{
		throw std::runtime_error("Could not write Shader importer fixture");
	}
}

std::string ReadBinary(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

String ShaderSource(const char* fragmentBody)
{
	return String("Shader \"Tests/EditorImport\" { ID \"5403c8ea-669d-4b64-8c30-6d63d128de71\" Version 1 ") +
	       "SubShader { Tags { \"RenderPipeline\" = \"UrsaForward\" } Pass { Name \"Forward\" "
	       "Tags { \"LightMode\" = \"Forward\" } HLSLINCLUDE\n#include \"Includes/Shared.hlsli\"\n"
	       "ENDHLSL HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\n"
	       "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }\n" +
	       fragmentBody + "\nENDHLSL } } }";
}

ResourceMount EngineMount(const std::filesystem::path& root)
{
	return {engineScheme, root, MountAccess::ReadOnly};
}

void TestReadOnlyProjectPackage(const std::filesystem::path& root)
{
	const auto projectRoot = root / "PakProject";
	const auto projectAssets = projectRoot / "Assets";
	const auto packageDirectory = root / "PakOutput";
	std::filesystem::create_directories(projectAssets / "Shaders");
	WriteText(projectAssets / "Shaders" / "ReadOnly.ursashader",
	          "Shader \"Tests/PakReadOnly\" { ID \"5403c8ea-669d-4b64-8c30-6d63d128de74\" Version 1 "
	          "SubShader { Pass { HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\n"
	          "float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p, 1); }\n"
	          "float4 PSMain() : SV_Target0 { return float4(1, 1, 1, 1); }\nENDHLSL } } }");
	VirtualFileSystem source = Take(OpenEngineFileSystem());
	Done(source.Mount(projectScheme, projectAssets, MountAccess::ReadWrite));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	const ResourceId materialId = ResourceId::Generate();
	Done(material->SetShader(Shader::BuiltinUnlitShaderId()));
	Done(SaveMaterialResource(source, Take(VirtualPath::Parse("prj:Materials/ReadOnly.ursamat")), materialId, *material,
	                          {}));
	std::filesystem::create_directories(packageDirectory);
	auto outputProvider = Take(DirectoryFileProvider::Open(packageDirectory, true));
	VirtualFileSystem output;
	Done(output.MountProvider("output", outputProvider, MountAccess::ReadWrite));
	const auto pakPath = Take(VirtualPath::Parse("output:Content.pak"));
	Done(PakBuilder::Build(source, {engineScheme, projectScheme}, output, pakPath));
	const auto pakFile = packageDirectory / "Content.pak";
	const std::string originalPakBytes = ReadBinary(pakFile);
	auto archive = Take(PakArchive::Open(pakFile));
	VirtualFileSystem packed;
	Done(archive->Mount(packed));
	auto workspace = Take(ProjectWorkspace::FromFilesystem(projectRoot, packed));
	auto documents = Take(EditorDocuments::Create(*workspace));
	auto materialDocument = Take(OpenMaterialDocument(Take(VirtualPath::Parse("prj:Materials/ReadOnly.ursamat")),
	                                                  documents->Session(), *workspace));
	Done(documents->RequestOpen(std::move(materialDocument)));
	auto edited = SetShaderMaterialProperty(*documents->Current(), "_BaseColor",
	                                        MaterialPropertyValue::ColorValue({0.2f, 0.3f, 0.4f, 1.0f}));
	Expect(static_cast<bool>(edited) && documents->Current()->Dirty(),
	       "Package-backed Material could not be edited in memory");
	auto saved = documents->SaveCurrent();
	if (saved)
	{
		throw std::runtime_error("Package-backed Material save unexpectedly succeeded");
	}
	if (saved.error.find("read-only") == String::npos)
	{
		throw std::runtime_error("Package-backed Material save returned an unexpected diagnostic: " +
		                         ToStdString(saved.error));
	}
	auto sourcePath = Take(VirtualPath::Parse("prj:Shaders/ReadOnly.ursashader"));
	auto shaderView = Take(ShaderImportView::Open(workspace->Index().Filesystem(), sourcePath));
	Expect(shaderView->SourceReadOnly(), "Pak-mounted project Shader source was reported writable");
	Done(shaderView->Reimport(*workspace));
	Expect(shaderView->Imported() && ReadBinary(pakFile) == originalPakBytes,
	       "Package-backed Shader reimport or Material save modified the source Pak");
}
} // namespace

int main()
{
	TemporaryDirectory temporary;
	try
	{
		const auto projectRoot = temporary.path / "Project";
		const auto engineRoot = temporary.path / "EngineAssets";
		std::filesystem::create_directories(projectRoot / "Assets");
		std::filesystem::create_directories(engineRoot);
		WriteText(projectRoot / "Assets" / "Shaders" / "Includes" / "Shared.hlsli",
		          "float4 ReturnColor() { return float4(1, 0, 0, 1); }\n");
		WriteText(projectRoot / "Assets" / "Shaders" / "EditorImport.ursashader",
		          ShaderSource("float4 PSMain() : SV_Target0 { return ReturnColor(); }"));
		WriteText(projectRoot / "Assets" / "Shaders" / "Independent.ursashader",
		          "Shader \"Tests/Independent\" { ID \"5403c8ea-669d-4b64-8c30-6d63d128de75\" Version 1 "
		          "SubShader { Pass { HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\n"
		          "float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p, 1); }\n"
		          "float4 PSMain() : SV_Target0 { return float4(0, 0, 1, 1); }\nENDHLSL } } }");
		auto workspace = Take(ProjectWorkspace::Open(projectRoot));
		auto sourcePath = Take(VirtualPath::Parse("prj:Shaders/EditorImport.ursashader"));
		auto view = Take(ShaderImportView::Open(workspace->Index().Filesystem(), sourcePath));
		Expect(!view->SourceReadOnly() && view->SourceText().find("ReturnColor") != String::npos,
		       "Project Shader source was not discovered as writable source metadata");
		Done(view->Reimport(*workspace));
		Expect(view->Imported() && view->Dependencies().size() == 1 &&
		           view->Dependencies()[0] == "prj:Shaders/Includes/Shared.hlsli",
		       "Shader import status did not report the resolved include dependency");
		const VirtualPath independentPath = Take(VirtualPath::Parse("prj:Shaders/Independent.ursashader"));
		const AssetMetadata initialMetadata = Take(workspace->Database()->Metadata(sourcePath));
		const auto initialDependencies = Take(workspace->Database()->ImportDependencies(sourcePath, "ursa.Shader"));
		Expect(initialMetadata.importer == "ursa.Shader" && initialDependencies.size() == 1 &&
		           initialDependencies[0].ToString() == "prj:Shaders/Includes/Shared.hlsli",
		       "Shader source identity and include dependencies were not recorded in AssetDatabase");
		const auto initialCache = Take(
		    workspace->Database()->CheckImport(sourcePath, initialMetadata.importer, initialMetadata.importerVersion,
		                                       "vulkan-spv", initialMetadata.importSettings, initialDependencies));
		const AssetMetadata independentMetadata = Take(workspace->Database()->Metadata(independentPath));
		const auto independentDependencies =
		    Take(workspace->Database()->ImportDependencies(independentPath, "ursa.Shader"));
		const auto independentCache = Take(workspace->Database()->CheckImport(
		    independentPath, independentMetadata.importer, independentMetadata.importerVersion, "vulkan-spv",
		    independentMetadata.importSettings, independentDependencies));
		Expect(initialCache.upToDate && independentCache.upToDate,
		       "Shader artifacts were not published into the AssetDatabase import cache");
		const ResourceId shaderId = Take(ResourceId::Parse("5403c8ea-669d-4b64-8c30-6d63d128de71"));
		auto location = Take(workspace->Database()->Resolve(shaderId));
		auto originalAsset = Take(Shader::LoadShaderAssetResource(workspace->Index().Filesystem(), location));
		WriteText(projectRoot / "Assets" / "Shaders" / "Includes" / "Shared.hlsli",
		          "float4 ReturnColor() { return float4(0, 1, 0, 1); }\n");
		Done(view->Reimport(*workspace));
		location = Take(workspace->Database()->Resolve(shaderId));
		auto updatedAsset = Take(Shader::LoadShaderAssetResource(workspace->Index().Filesystem(), location));
		Expect(updatedAsset.sourceHash != originalAsset.sourceHash,
		       "Reimport did not invalidate the compiled Shader after its include changed");
		const AssetMetadata updatedMetadata = Take(workspace->Database()->Metadata(sourcePath));
		const auto updatedDependencies = Take(workspace->Database()->ImportDependencies(sourcePath, "ursa.Shader"));
		const auto updatedCache = Take(
		    workspace->Database()->CheckImport(sourcePath, updatedMetadata.importer, updatedMetadata.importerVersion,
		                                       "vulkan-spv", updatedMetadata.importSettings, updatedDependencies));
		const AssetMetadata updatedIndependentMetadata = Take(workspace->Database()->Metadata(independentPath));
		const auto updatedIndependentDependencies =
		    Take(workspace->Database()->ImportDependencies(independentPath, "ursa.Shader"));
		const auto updatedIndependentCache = Take(workspace->Database()->CheckImport(
		    independentPath, updatedIndependentMetadata.importer, updatedIndependentMetadata.importerVersion,
		    "vulkan-spv", updatedIndependentMetadata.importSettings, updatedIndependentDependencies));
		Expect(updatedCache.upToDate && updatedCache.fingerprint != initialCache.fingerprint &&
		           updatedIndependentCache.upToDate &&
		           updatedIndependentCache.fingerprint == independentCache.fingerprint,
		       "Changing a Shader include invalidated unrelated Shader artifact cache entries");
		WriteText(projectRoot / "Assets" / "Shaders" / "EditorImport.ursashader",
		          ShaderSource("float4 PSMain( : SV_Target0 { return ReturnColor(); }"));
		auto failedImport = view->Reimport(*workspace);
		Expect(!failedImport && view->Diagnostic().find("prj:Shaders/EditorImport.ursashader") != String::npos,
		       "Invalid HLSL did not expose its source-located diagnostic in the Editor Shader view");
		location = Take(workspace->Database()->Resolve(shaderId));
		auto retainedAsset = Take(Shader::LoadShaderAssetResource(workspace->Index().Filesystem(), location));
		Expect(retainedAsset.sourceHash == updatedAsset.sourceHash,
		       "Failed Shader reimport replaced the last valid compiled asset");

		WriteText(engineRoot / "Shaders" / "Readonly.ursashader",
		          "Shader \"Tests/Readonly\" { ID \"5403c8ea-669d-4b64-8c30-6d63d128de72\" Version 1 "
		          "SubShader { Pass { HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\n"
		          "float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p, 1); }\n"
		          "float4 PSMain() : SV_Target0 { return float4(1, 1, 1, 1); }\nENDHLSL } } }");
		Vector<ResourceMount> mounts{EngineMount(engineRoot)};
		auto readonlyWorkspace = Take(ProjectWorkspace::Open(projectRoot, mounts));
		auto readonlyPath = Take(VirtualPath::Parse("ursa:Shaders/Readonly.ursashader"));
		auto readonlyView = Take(ShaderImportView::Open(readonlyWorkspace->Index().Filesystem(), readonlyPath));
		Expect(readonlyView->SourceReadOnly(), "Engine Shader source was incorrectly marked writable");
		const auto sourceBytes = std::filesystem::file_size(engineRoot / "Shaders" / "Readonly.ursashader");
		Done(readonlyView->Reimport(*readonlyWorkspace));
		Expect(readonlyView->Imported() &&
		           std::filesystem::file_size(engineRoot / "Shaders" / "Readonly.ursashader") == sourceBytes &&
		           !std::filesystem::exists(engineRoot / "Shaders" / "Readonly.ursashaderasset"),
		       "Reimport of a read-only Engine Shader modified or wrote beside the source asset");

		WriteText(projectRoot / "Assets" / "Shaders" / "EditorImport.ursashader",
		          ShaderSource("float4 PSMain() : SV_Target0 { return ReturnColor(); }"));
		WriteText(projectRoot / "Assets" / "Shaders" / "Packaged.ursashader",
		          "Shader \"Tests/Packaged\" { ID \"5403c8ea-669d-4b64-8c30-6d63d128de73\" Version 1 "
		          "SubShader { Pass { HLSLPROGRAM\n#pragma vertex VSMain\n#pragma fragment PSMain\n"
		          "float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p, 1); }\n"
		          "float4 PSMain() : SV_Target0 { return float4(1, 1, 1, 1); }\nENDHLSL } } }");
		VirtualFileSystem packagedFilesystem;
		Expect(
		    static_cast<bool>(packagedFilesystem.Mount(projectScheme, projectRoot / "Assets", MountAccess::ReadOnly)) &&
		        static_cast<bool>(packagedFilesystem.Mount(engineScheme, engineRoot, MountAccess::ReadOnly)),
		    "Cannot create package-backed read-only Shader sources");
		auto packagedWorkspace = Take(ProjectWorkspace::FromFilesystem(projectRoot, packagedFilesystem));
		auto packagedPath = Take(VirtualPath::Parse("prj:Shaders/Packaged.ursashader"));
		auto packagedView = Take(ShaderImportView::Open(packagedWorkspace->Index().Filesystem(), packagedPath));
		Expect(packagedView->SourceReadOnly(), "Package-backed project Shader source was reported writable");
		Done(packagedView->Reimport(*packagedWorkspace));
		Expect(packagedView->Imported() &&
		           !std::filesystem::exists(projectRoot / "Assets" / "Shaders" / "Packaged.ursashaderasset"),
		       "Package-backed Shader reimport wrote into a read-only project mount");
		TestReadOnlyProjectPackage(temporary.path);
		std::cout << "Editor Shader import tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
