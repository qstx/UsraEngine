#include <ResourceFixtures.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/AssetRoots.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

std::string bytes(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::current_path() / ("editor_documents_" + ToStdString(ResourceId::Generate().ToString()));
	const auto logPath = root.parent_path() / (root.filename().string() + ".log");
	try
	{
		std::filesystem::create_directories(root / "Assets");
		LogConfig config;
		config.filePath = logPath;
		config.level = LogLevel::Debug;
		done(Log::Initialize(root / "EditorDocumentsTests.exe", config));
		ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(Take(OpenEngineFileSystem())));

		struct EngineFilesCleanup
		{
			~EngineFilesCleanup()
			{
				ConfigureEngineFileSystem({});
			}
		} engineFilesCleanup;

		const auto firstPath = root / "Assets" / "first.ursamat";
		const auto firstId = ResourceId::Generate();
		MaterialInstance original(MaterialType::Unlit);
		done(SaveMaterialResource(firstPath, firstId, original, {}));
		const std::string oldBytes = bytes(firstPath);
		auto workspace = Take(Editor::ProjectWorkspace::Open(root));
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		auto edited = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		done(edited->SetBaseColor({0.2f, 0.7f, 0.3f, 1.f}));
		bool failWrite = true;
		const Editor::DocumentWriter writer =
		    [edited, &failWrite](VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id)
		{
			if (failWrite)
			{
				return Result<void>::Failure("simulated material save failure");
			}
			return SaveMaterialResource(filesystem, path, id, *edited, {});
		};
		auto first = Take(Editor::EditorDocument::Create(ResourceKind::Material, firstId,
		                                                 Take(VirtualPath::Parse("prj:first.ursamat")), writer));
		done(documents->RequestOpen(std::move(first)));
		documents->Current()->MarkDirty();
		const auto secondId = ResourceId::Generate();
		auto makeSecond = [&]
		{
			return Take(Editor::EditorDocument::Create(
			    ResourceKind::Material, secondId, Take(VirtualPath::Parse("prj:second.ursamat")),
			    [edited](VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id)
			    { return SaveMaterialResource(filesystem, path, id, *edited, {}); }));
		};
		done(documents->RequestOpen(makeSecond()));
		Expect(documents->PromptPending(), "Switching a dirty document did not request a choice");
		done(documents->ResolvePrompt(Editor::UnsavedChoice::Cancel));
		Expect(documents->Current() && documents->Current()->Id() == firstId && documents->Current()->Dirty(),
		       "Cancel did not preserve the dirty document");
		done(documents->RequestClose());
		Expect(!documents->ResolvePrompt(Editor::UnsavedChoice::Save), "Failed save was accepted");
		Expect(documents->PromptPending() && documents->Current() && documents->Current()->Dirty(),
		       "Failed save dropped pending changes");
		Expect(bytes(firstPath) == oldBytes, "Failed save modified the existing file");
		Expect(bytes(logPath).find("simulated material save failure") != std::string::npos,
		       "Failed asset save did not record a diagnostic");
		failWrite = false;
		done(documents->ResolvePrompt(Editor::UnsavedChoice::Cancel));
		done(documents->SaveCurrent());
		Expect(!documents->Current()->Dirty() && documents->Current()->Id() == firstId,
		       "Successful save changed the ID or left the document dirty");
		Expect(static_cast<bool>(documents->Session().Index().Resolve(firstId)),
		       "Session was not refreshed after save");
		auto loaded = Take(LoadMaterialResource(firstPath, {}));
		Expect(loaded.id == firstId && loaded.material->Parameters().baseColor.green > 0.6f,
		       "Saved material did not reopen with edited values");
		documents->Current()->MarkDirty();
		done(documents->RequestOpen(makeSecond()));
		done(documents->ResolvePrompt(Editor::UnsavedChoice::Discard));
		Expect(documents->Current() && documents->Current()->Id() == secondId &&
		           !std::filesystem::exists(root / "Assets" / "second.ursamat"),
		       "Discard did not switch documents without writing the old one");
		done(documents->SaveCurrent());
		Expect(static_cast<bool>(documents->Session().Index().Resolve(secondId)), "New document ID was not indexed");
		const auto renamedPath = Take(VirtualPath::Parse("prj:renamed.ursamat"));
		done(documents->RenameCurrent(renamedPath));
		Expect(documents->Current()->Path() == renamedPath &&
		           documents->Session().Index().Resolve(secondId).value.path == renamedPath &&
		           !std::filesystem::exists(root / "Assets" / "second.ursamat"),
		       "Rename lost the stable resource ID or old path");
		auto provider = std::make_shared<MemoryFileProvider>();
		done(provider->WriteAtomic("renamed.ursamat", Take(workspace->Index().Filesystem().Read(renamedPath)), true));
		VirtualFileSystem readOnly;
		done(readOnly.MountProvider(projectScheme, provider, MountAccess::ReadOnly));
		auto readOnlyWorkspace = Take(Editor::ProjectWorkspace::FromFilesystem(root, readOnly));
		auto readOnlyDocuments = Take(Editor::EditorDocuments::Create(*readOnlyWorkspace));
		auto openedReadOnly =
		    Take(Editor::OpenMaterialDocument(renamedPath, readOnlyDocuments->Session(), *readOnlyWorkspace));
		done(readOnlyDocuments->RequestOpen(std::move(openedReadOnly)));
		readOnlyDocuments->Current()->MarkDirty();
		const auto before = Take(readOnly.Read(renamedPath));
		Expect(!readOnlyDocuments->SaveCurrent() && readOnlyDocuments->Current()->Dirty() &&
		           Take(readOnly.Read(renamedPath)) == before,
		       "Read-only save damaged content or discarded dirty state");
		Log::Shutdown();
		Expect(bytes(logPath).find("Saved asset") != std::string::npos,
		       "Successful asset save did not record its path");
		std::filesystem::remove_all(root);
		std::filesystem::remove(logPath);
		std::cout << "Editor document safety test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		Log::Shutdown();
		std::filesystem::remove_all(root);
		std::filesystem::remove(logPath);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
