#include <Assets/ShaderAssetImporter.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
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
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() / ToStdString(ResourceId::Generate().ToString());
		std::filesystem::create_directories(root / "Assets");
		auto workspace =
		    Take(ProjectWorkspace::Open(root, {{engineScheme, URSA_TEST_ENGINE_SOURCE, MountAccess::ReadOnly}}));
		Done(workspace->InitializeBuiltinAssets());
		const auto source = Take(VirtualPath::Parse("ursa:Shaders/Editor/ImGui.ursashader"));
		auto& files = workspace->Index().Filesystem();
		const auto sourceBytes = Take(files.Read(source));
		const auto metadataBytes = Take(files.Read(Take(VirtualPath::Parse(source.ToString() + ".meta"))));
		const auto first = Take(ShaderAssetImporter::Import(*workspace->BuiltinDatabase(), source));
		const auto repeated = Take(ShaderAssetImporter::Import(*workspace->BuiltinDatabase(), source));
		if (first.cacheHit || !repeated.cacheHit || repeated.artifactPath.scheme != editorLibraryScheme ||
		    first.asset.id != repeated.asset.id || sourceBytes != Take(files.Read(source)) ||
		    metadataBytes != Take(files.Read(Take(VirtualPath::Parse(source.ToString() + ".meta")))))
		{
			throw std::runtime_error("Shared builtin shader import did not preserve identity/cache/source boundaries");
		}
		Done(files.Remove(first.artifactPath));
		const auto repaired = Take(ShaderAssetImporter::Import(*workspace->BuiltinDatabase(), source));
		if (repaired.cacheHit || !Take(files.Exists(repaired.artifactPath)))
		{
			throw std::runtime_error("Missing builtin shader output was not regenerated");
		}
		if (!std::filesystem::is_directory(root / "Library" / "Temp" / "ShaderCompiler") ||
		    !std::filesystem::is_directory(root / "Library" / "ShaderCompileCache"))
		{
			throw std::runtime_error("Shader staging/cache is not project-owned");
		}
		Shader::ShaderCompilerTools missing;
		if (!ShaderAssetImporter::Import(*workspace->BuiltinDatabase(), source, missing))
		{
			throw std::runtime_error("Valid shader cache required unavailable compiler tools");
		}
		if (ShaderAssetImporter::Import(*workspace->BuiltinDatabase(), source, missing, true))
		{
			throw std::runtime_error("Missing compiler tools were not diagnosed");
		}
		std::cout << "Project-owned shader import tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
