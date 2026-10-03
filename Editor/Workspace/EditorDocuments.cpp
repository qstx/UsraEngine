#include <Workspace/EditorDocuments.hpp>
#include <Core/Log.hpp>
#include <Serialization/AtomicFile.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <fstream>
#include <iterator>
#include <string>

namespace Ursa::Editor
{
namespace
{
const char* ExtensionFor(ResourceKind kind)
{
	switch (kind)
	{
		case ResourceKind::texture:
			return ".ursatex";
		case ResourceKind::material:
			return ".ursamat";
		case ResourceKind::scene:
			return ".ursascene";
		case ResourceKind::prefab:
			return ".ursaprefab";
	}
	return "";
}

String VirtualFileName(const VirtualPath& path)
{
	const auto slash = path.relative.rfind('/');
	return slash == String::npos ? path.relative : path.relative.substr(slash + 1);
}

Result<void> RestoreFile(const std::filesystem::path& path, bool existed, const std::string& original)
{
	if (!existed)
	{
		std::error_code error;
		std::filesystem::remove(path, error);
		return error ? Result<void>::Failure("Cannot remove failed new asset: " + PathToUrsaString(path))
		             : Result<void>{};
	}
	return WriteResourceAtomically(path,
	                               [&](std::ostream& output)
	                               {
		                               output.write(original.data(), static_cast<std::streamsize>(original.size()));
		                               return output ? Result<void>{}
		                                             : Result<void>::Failure("Cannot restore old asset bytes");
	                               });
}
} // namespace

Result<std::unique_ptr<EditorDocument>> EditorDocument::Create(ResourceKind kind, ResourceId id, VirtualPath path,
                                                               DocumentWriter writer)
{
	const auto matched = ResourceKindForPath(path);
	if (!id || !writer || !matched || matched.value != kind)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure("Invalid Editor document ID, path, or writer");
	}
	return Result<std::unique_ptr<EditorDocument>>::Success(
	    std::unique_ptr<EditorDocument>(new EditorDocument(kind, id, std::move(path), std::move(writer))));
}

Result<std::unique_ptr<EditorDocuments>> EditorDocuments::Create(ProjectWorkspace& workspace)
{
	auto session = ResourceSession::Open(workspace.Index().Mounts());
	if (!session)
	{
		return Result<std::unique_ptr<EditorDocuments>>::Failure(session.error);
	}
	auto documents = std::unique_ptr<EditorDocuments>(new EditorDocuments(workspace));
	documents->sessions.push_back(std::move(session.value));
	return Result<std::unique_ptr<EditorDocuments>>::Success(std::move(documents));
}

Result<void> EditorDocuments::RequestOpen(std::unique_ptr<EditorDocument> document)
{
	if (!document)
	{
		return Result<void>::Failure("Cannot open an empty Editor document");
	}
	if (promptPending)
	{
		return Result<void>::Failure("Resolve the unsaved changes prompt first");
	}
	auto target = workspace.WritablePath(document->Path());
	if (!target)
	{
		return Result<void>::Failure(target.error);
	}
	pending = std::move(document);
	pendingClose = false;
	if (current && current->Dirty())
	{
		promptPending = true;
	}
	else
	{
		FinishPending();
	}
	return {};
}

Result<void> EditorDocuments::RequestClose()
{
	if (promptPending)
	{
		return Result<void>::Failure("Resolve the unsaved changes prompt first");
	}
	pending.reset();
	pendingClose = true;
	if (current && current->Dirty())
	{
		promptPending = true;
	}
	else
	{
		FinishPending();
	}
	return {};
}

void EditorDocuments::FinishPending()
{
	if (pendingClose)
	{
		current.reset();
	}
	else
	{
		current = std::move(pending);
	}
	pendingClose = false;
	promptPending = false;
	lastError.clear();
}

Result<void> EditorDocuments::ResolvePrompt(UnsavedChoice choice)
{
	if (!promptPending)
	{
		return Result<void>::Failure("No unsaved changes prompt is active");
	}
	if (choice == UnsavedChoice::cancel)
	{
		pending.reset();
		pendingClose = false;
		promptPending = false;
		return {};
	}
	if (choice == UnsavedChoice::save)
	{
		auto saved = SaveCurrent();
		if (!saved)
		{
			return saved;
		}
	}
	FinishPending();
	return {};
}

Result<void> EditorDocuments::SaveCurrent()
{
	if (saveBlocked)
	{
		return Result<void>::Failure("Saving is disabled while Play mode is active");
	}
	if (!current)
	{
		return Result<void>::Failure("No Editor document is open");
	}
	auto target = workspace.WritablePath(current->Path());
	if (!target)
	{
		return Result<void>::Failure(target.error);
	}
	if (target.value.extension() != ExtensionFor(current->Kind()))
	{
		return Result<void>::Failure("Document path has the wrong resource extension");
	}
	auto indexed = workspace.Index().Resolve(current->Id());
	if (indexed && indexed.value.path != current->Path())
	{
		return Result<void>::Failure("Document ID is already used by " + indexed.value.path.ToString());
	}
	std::error_code fileError;
	const bool existed = std::filesystem::exists(target.value, fileError);
	if (fileError)
	{
		return Result<void>::Failure("Cannot inspect document destination");
	}
	std::string original;
	if (existed)
	{
		std::ifstream input(target.value, std::ios::binary);
		if (!input)
		{
			return Result<void>::Failure("Cannot read existing document before save");
		}
		original.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
		if (!input.eof() && input.fail())
		{
			return Result<void>::Failure("Cannot back up existing document before save");
		}
	}
	const auto stage =
	    target.value.parent_path() / (target.value.filename().string() + ".editor-stage-" +
	                                  ToStdString(current->Id().ToString()) + ExtensionFor(current->Kind()));
	std::filesystem::remove(stage, fileError);
	if (fileError)
	{
		return Result<void>::Failure("Cannot clear previous Editor staging file");
	}
	auto written = current->writer(stage, current->Id());
	if (!written)
	{
		std::filesystem::remove(stage, fileError);
		lastError = written.error;
		URSA_LOG_ERROR("Editor", "Cannot save asset " + current->Path().ToString() + ": " + lastError);
		return written;
	}
	auto header = ReadResourceHeader(stage);
	if (!header || header.value.id != current->Id() || header.value.kind != current->Kind())
	{
		std::filesystem::remove(stage, fileError);
		lastError = header ? String("Editor writer produced the wrong resource ID or kind") : header.error;
		return Result<void>::Failure(lastError);
	}
	written = WriteResourceAtomically(target.value,
	                                  [&](std::ostream& output)
	                                  {
		                                  std::ifstream input(stage, std::ios::binary);
		                                  if (!input)
		                                  {
			                                  return Result<void>::Failure("Cannot open staged Editor asset");
		                                  }
		                                  output << input.rdbuf();
		                                  return output && !input.bad()
		                                             ? Result<void>{}
		                                             : Result<void>::Failure("Cannot copy staged Editor asset");
	                                  });
	std::filesystem::remove(stage, fileError);
	if (!written)
	{
		lastError = written.error;
		return written;
	}
	auto refreshed = workspace.Refresh();
	auto session = refreshed ? ResourceSession::Open(workspace.Index().Mounts())
	                         : Result<std::unique_ptr<ResourceSession>>::Failure(refreshed.error);
	if (!session)
	{
		auto restored = RestoreFile(target.value, existed, original);
		workspace.Refresh();
		lastError = session.error;
		if (!restored)
		{
			lastError += "; rollback failed: " + restored.error;
		}
		return Result<void>::Failure(lastError);
	}
	sessions.push_back(std::move(session.value));
	current->dirty = false;
	lastError.clear();
	URSA_LOG_INFO("Editor", "Saved asset " + current->Path().ToString());
	return {};
}

void EditorDocuments::DrawUnsavedPrompt()
{
	if (!promptPending)
	{
		return;
	}
	if (!ImGui::IsPopupOpen("Unsaved changes"))
	{
		ImGui::OpenPopup("Unsaved changes");
	}
	if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}
	const String name = current ? VirtualFileName(current->Path()) : String("document");
	ImGui::Text("Save changes to %s?", name.c_str());
	if (!lastError.empty())
	{
		ImGui::TextWrapped("%s", lastError.c_str());
	}
	if (ImGui::Button("Save"))
	{
		ResolvePrompt(UnsavedChoice::save);
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard"))
	{
		ResolvePrompt(UnsavedChoice::discard);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		ResolvePrompt(UnsavedChoice::cancel);
	}
	ImGui::EndPopup();
}
} // namespace Ursa::Editor
