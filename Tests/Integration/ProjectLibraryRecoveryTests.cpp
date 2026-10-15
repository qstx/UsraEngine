#include <Assets/ModelImporter.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/ShaderImportView.hpp>
#include <Assets/ShaderAssetImporter.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
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

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

std::unique_ptr<ProjectWorkspace> Prepare(const std::filesystem::path& project)
{
	Vector<ResourceMount> mounts;
	for (const auto& root : Take(ResolveEngineAssetRoots()))
	{
		mounts.push_back({engineScheme, root, MountAccess::ReadOnly});
	}
	auto workspace = Take(ProjectWorkspace::Open(project, mounts));
	auto records = workspace->Database()->Assets();
	for (const auto& record : records)
	{
		if (record.metadata.folder)
		{
			continue;
		}
		const auto extension = PathFromUrsaString(record.path.relative).extension();
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
		{
			Take(TextureImporter::Import(*workspace->Database(), record.path,
			                             Take(TextureImporter::Settings(*workspace->Database(), record.path))));
		}
		else if (extension == ".fbx" || extension == ".obj")
		{
			Take(ModelImporter::Import(*workspace->Database(), record.path,
			                           Take(ModelImporter::Settings(*workspace->Database(), record.path))));
		}
		else if (extension == ".ursashader")
		{
			auto view = Take(ShaderImportView::Open(workspace->Index().Filesystem(), record.path));
			Done(view->Reimport(*workspace));
		}
		else if (extension == ".compute")
		{
			Take(ShaderAssetImporter::Import(*workspace->Database(), record.path));
		}
	}
	Done(workspace->Refresh());
	Take(workspace->Database()->CurrentImportedArtifacts());
	return workspace;
}
} // namespace

int main(int count, char** arguments)
{
	try
	{
		if (count == 3 && String(arguments[1]) == "--prepare")
		{
			auto workspace = Prepare(std::filesystem::u8path(arguments[2]));
			std::cout << "Prepared current project Library with "
			          << Take(workspace->Database()->CurrentImportedArtifacts()).size() << " imported artifacts\n";
			return 0;
		}
		const auto root =
		    std::filesystem::u8path(URSA_TEST_SCRATCH_ROOT) / ToStdString(ResourceId::Generate().ToString());
		std::filesystem::create_directories(root);
		std::filesystem::copy(std::filesystem::u8path(URSA_PROJECT_DIR) / "Assets", root / "Assets",
		                      std::filesystem::copy_options::recursive);
		auto workspace = Prepare(root);
		const auto originalRecords = workspace->Database()->Assets();
		const auto originalArtifacts = Take(workspace->Database()->CurrentImportedArtifacts());
		Expect(!originalArtifacts.empty(), "Project recovery fixture did not import any assets");
		const auto roots = Take(workspace->Index().Filesystem().Roots(editorLibraryScheme));
		Expect(roots.size() == 1 && roots.front() == std::filesystem::canonical(root / "Library"),
		       "Project cache escaped its Library directory");
		workspace.reset();
		const auto library = std::filesystem::canonical(root / "Library");
		Expect(library.parent_path() == std::filesystem::canonical(root), "Unsafe project Library test deletion");
		std::filesystem::remove_all(library);
		workspace = Prepare(root);
		for (const auto& record : originalRecords)
		{
			const auto metadata = Take(workspace->Database()->Metadata(record.path));
			Expect(metadata.guid == record.metadata.guid, "Library recovery changed an asset GUID");
		}
		for (const auto& artifact : originalArtifacts)
		{
			const auto resolved = Take(workspace->Database()->Resolve(artifact.guid, artifact.localFileId));
			Expect(Take(workspace->Index().Filesystem().Exists(resolved)), "Imported sub-asset did not recover");
		}
		std::cout << "Project Library deletion/rebuild and stable GUID/sub-asset tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
