#include <Workspace/EditorDocuments.hpp>
#include <Core/Log.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/TextureResource.hpp>
#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <Vfs/MemoryFileProvider.hpp>
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
		case ResourceKind::Texture:
			return ".ursatex";
		case ResourceKind::Material:
			return ".ursamat";
		case ResourceKind::Scene:
			return ".ursascene";
		case ResourceKind::Prefab:
			return ".ursaprefab";
	}
	return "";
}

String VirtualFileName(const VirtualPath& path)
{
	const auto slash = path.relative.rfind('/');
	return slash == String::npos ? path.relative : path.relative.substr(slash + 1);
}

} // namespace

Result<std::unique_ptr<EditorDocument>> EditorDocument::Create(ResourceKind documentKind, ResourceId documentId,
                                                               VirtualPath documentPath, DocumentWriter documentWriter)
{
	const auto matched = ResourceKindForPath(documentPath);
	if (!documentId || !documentWriter || !matched || matched.value != documentKind)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure("Invalid Editor document ID, path, or writer");
	}
	return Result<std::unique_ptr<EditorDocument>>::Success(std::unique_ptr<EditorDocument>(
	    new EditorDocument(documentKind, documentId, std::move(documentPath), std::move(documentWriter))));
}

Result<std::unique_ptr<EditorDocuments>> EditorDocuments::Create(ProjectWorkspace& projectWorkspace)
{
	auto session = ResourceSession::Open(projectWorkspace.Index().Mounts());
	if (!session)
	{
		return Result<std::unique_ptr<EditorDocuments>>::Failure(session.error);
	}
	auto documents = std::unique_ptr<EditorDocuments>(new EditorDocuments(projectWorkspace));
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
	if (!workspace.Index().Filesystem().IsMounted(document->Path().scheme))
	{
		return Result<void>::Failure("Document scheme is not mounted");
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
	if (choice == UnsavedChoice::Cancel)
	{
		pending.reset();
		pendingClose = false;
		promptPending = false;
		return {};
	}
	if (choice == UnsavedChoice::Save)
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
	auto& filesystem = workspace.Index().Filesystem();
	auto writable = filesystem.CheckWritable(current->Path());
	if (!writable || current->Path().scheme != projectResourceScheme)
	{
		return Result<void>::Failure(writable ? "Only project resources are writable" : writable.error);
	}
	auto indexed = workspace.Index().Resolve(current->Id());
	if (indexed && indexed.value.path != current->Path())
	{
		return Result<void>::Failure("Document ID is already used by " + indexed.value.path.ToString());
	}
	auto provider = std::make_shared<MemoryFileProvider>();
	VirtualFileSystem staging;
	auto mounted = staging.MountProvider(current->Path().scheme, provider, MountAccess::ReadWrite);
	auto written = mounted ? current->writer(staging, current->Path(), current->Id()) : mounted;
	auto fail = [&](const String& error)
	{
		lastError = error;
		URSA_LOG_ERROR("Editor", "Cannot save asset " + current->Path().ToString() + ": " + error);
		return Result<void>::Failure(error);
	};
	if (!written)
	{
		return fail(written.error);
	}
	auto header = ReadResourceHeader(staging, current->Path());
	if (!header || header.value.id != current->Id() || header.value.kind != current->Kind())
	{
		return fail(header ? "Editor writer produced the wrong resource ID or kind" : header.error);
	}
	Vector<ResourceMount> mounts;
	mounts.push_back({current->Path().scheme, {}, MountAccess::ReadOnly, provider});
	mounts.insert(mounts.end(), workspace.Index().Mounts().begin(), workspace.Index().Mounts().end());
	auto validated = ResourceSession::Open(mounts);
	if (!validated)
	{
		return fail(validated.error);
	}
	if (current->Kind() == ResourceKind::Texture)
	{
		auto texture = LoadTextureResource(staging, current->Path());
		if (!texture)
		{
			return fail(texture.error);
		}
	}
	if (current->Kind() == ResourceKind::Material)
	{
		auto material = validated.value->LoadMaterial(current->Id());
		if (!material)
		{
			return fail(material.error);
		}
	}
	auto existed = filesystem.Exists(current->Path());
	if (!existed)
	{
		return fail(existed.error);
	}
	auto original = existed.value ? filesystem.Read(current->Path()) : Result<Vector<UInt8>>::Success({});
	auto bytes = staging.Read(current->Path());
	if (!original || !bytes)
	{
		return fail(!original ? original.error : bytes.error);
	}
	written = filesystem.WriteAtomic(current->Path(), bytes.value);
	if (!written)
	{
		return fail(written.error);
	}
	auto refreshed = workspace.Refresh();
	auto session = refreshed ? ResourceSession::Open(workspace.Index().Mounts())
	                         : Result<std::unique_ptr<ResourceSession>>::Failure(refreshed.error);
	if (!session)
	{
		auto restored = existed.value ? filesystem.WriteAtomic(current->Path(), original.value)
		                              : filesystem.Remove(current->Path());
		workspace.Refresh();
		return fail(session.error + (restored ? String{} : "; rollback failed: " + restored.error));
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
		ResolvePrompt(UnsavedChoice::Save);
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard"))
	{
		ResolvePrompt(UnsavedChoice::Discard);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		ResolvePrompt(UnsavedChoice::Cancel);
	}
	ImGui::EndPopup();
}

Result<void> EditorDocuments::RenameCurrent(const VirtualPath& path)
{
	if (saveBlocked || promptPending || !current)
	{
		return Result<void>::Failure("Cannot rename the current document now");
	}
	auto kind = ResourceKindForPath(path);
	if (!kind || kind.value != current->Kind() || path.scheme != projectResourceScheme)
	{
		return Result<void>::Failure("Rename must retain the resource kind and project scheme");
	}
	if (path == current->Path())
	{
		return {};
	}
	auto& files = workspace.Index().Filesystem();
	auto writable = workspace.CheckWritable(path);
	if (!writable)
	{
		return writable;
	}
	auto exists = files.Exists(path);
	if (!exists || exists.value)
	{
		return Result<void>::Failure(exists ? "Rename destination already exists" : exists.error);
	}
	if (current->Dirty())
	{
		auto saved = SaveCurrent();
		if (!saved)
		{
			return saved;
		}
	}
	const auto previous = current->Path();
	auto renamed = files.Rename(previous, path);
	if (!renamed)
	{
		return renamed;
	}
	auto refreshed = workspace.Refresh();
	auto session = refreshed ? ResourceSession::Open(workspace.Index().Mounts())
	                         : Result<std::unique_ptr<ResourceSession>>::Failure(refreshed.error);
	if (!session)
	{
		auto restored = files.Rename(path, previous);
		workspace.Refresh();
		return Result<void>::Failure(session.error +
		                             (restored ? String{} : "; rename rollback failed: " + restored.error));
	}
	sessions.push_back(std::move(session.value));
	current->path = path;
	current->dirty = false;
	return {};
}
} // namespace Ursa::Editor
