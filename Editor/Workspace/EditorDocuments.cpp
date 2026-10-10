#include <Workspace/EditorDocuments.hpp>
#include <Assets/MaterialDraft.hpp>
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
		case ResourceKind::Shader:
			return ".ursashaderasset";
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
	auto session = ResourceSession::Open(projectWorkspace.ContentFiles(), projectWorkspace.AssetResolver());
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
	auto sidecar = VirtualPath::Parse(current->Path().ToString() + ".meta");
	if (!sidecar)
	{
		return fail(sidecar.error);
	}
	auto existingMetadata = filesystem.Read(sidecar.value);
	Vector<UInt8> metadataBytes;
	if (existingMetadata)
	{
		metadataBytes = std::move(existingMetadata.value);
	}
	else
	{
		const nlohmann::json metadata = {{"version", assetMetadataVersion},
		                                 {"guid", ToStdString(current->Id().ToString())},
		                                 {"folder", false},
		                                 {"importer", ""},
		                                 {"importerVersion", 0},
		                                 {"settings", nlohmann::json::object()}};
		const std::string text = metadata.dump(2);
		metadataBytes.reserve(text.size());
		for (unsigned char value : text)
		{
			metadataBytes.push_back(value);
		}
	}
	written = staging.WriteAtomic(sidecar.value, metadataBytes);
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
	for (const auto& mount : workspace.ContentFiles().Providers())
	{
		mounts.push_back({mount.scheme, {}, mount.access, mount.provider});
	}
	auto assetResolver = workspace.AssetResolver();
	auto validated = ResourceSession::Open(mounts,
	                                       [assetResolver, currentId = current->Id(),
	                                        currentPath = current->Path()](ResourceId guid, Ursa::UInt64 localFileId)
	                                       {
		                                       if (guid == currentId && localFileId == 0)
		                                       {
			                                       return Result<VirtualPath>::Success(currentPath);
		                                       }
		                                       return assetResolver(guid, localFileId);
	                                       });
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
	auto metadataExisted = filesystem.Exists(sidecar.value);
	if (!metadataExisted)
	{
		return fail(metadataExisted.error);
	}
	auto originalMetadata = metadataExisted.value ? filesystem.Read(sidecar.value) : Result<Vector<UInt8>>::Success({});
	if (!originalMetadata)
	{
		return fail(originalMetadata.error);
	}
	written = filesystem.WriteAtomic(current->Path(), bytes.value);
	if (!written)
	{
		return fail(written.error);
	}
	auto refreshed = workspace.Refresh();
	auto session = refreshed ? ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver())
	                         : Result<std::unique_ptr<ResourceSession>>::Failure(refreshed.error);
	if (!session)
	{
		auto restored = existed.value ? filesystem.WriteAtomic(current->Path(), original.value)
		                              : filesystem.Remove(current->Path());
		auto restoredMetadata = metadataExisted.value ? filesystem.WriteAtomic(sidecar.value, originalMetadata.value)
		                                              : filesystem.Remove(sidecar.value);
		workspace.Refresh();
		String rollbackError;
		if (!restored)
		{
			rollbackError = "; asset rollback failed: " + restored.error;
		}
		if (!restoredMetadata)
		{
			rollbackError += "; metadata rollback failed: " + restoredMetadata.error;
		}
		return fail(session.error + rollbackError);
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
	auto* database = workspace.Database();
	if (!database)
	{
		return Result<void>::Failure("AssetDatabase is unavailable for project asset rename");
	}
	auto renamed = database->Move(previous, path);
	if (!renamed)
	{
		return renamed;
	}
	auto refreshed = workspace.Refresh();
	auto session = refreshed ? ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver())
	                         : Result<std::unique_ptr<ResourceSession>>::Failure(refreshed.error);
	if (!session)
	{
		auto restored = database->Move(path, previous);
		workspace.Refresh();
		return Result<void>::Failure(session.error +
		                             (restored ? String{} : "; rename rollback failed: " + restored.error));
	}
	sessions.push_back(std::move(session.value));
	current->path = path;
	current->dirty = false;
	return {};
}

Result<void> EditorDocuments::RefreshResources()
{
	const MaterialInstance* protectedMaterial = nullptr;
	if (current && current->Dirty())
	{
		auto material = current->Payload<MaterialDraft>();
		if (material)
		{
			protectedMaterial = material->MaterialPtr().get();
		}
	}
	Vector<std::shared_ptr<ResourceReload>> reloads;
	for (const auto& existing : sessions)
	{
		auto reloaded = existing->PrepareResourceReload(workspace.ContentFiles(), protectedMaterial);
		if (!reloaded)
		{
			return Result<void>::Failure(reloaded.error);
		}
		reloads.push_back(std::move(reloaded.value));
	}
	std::unique_ptr<EditorDocument> refreshedMaterial;
	if (current && !current->Dirty() && current->Kind() == ResourceKind::Material)
	{
		auto candidate = ResourceSession::Open(workspace.ContentFiles(), workspace.AssetResolver());
		if (!candidate)
		{
			return Result<void>::Failure(candidate.error);
		}
		auto opened = OpenMaterialDocument(current->Path(), *candidate.value, workspace);
		if (!opened)
		{
			return Result<void>::Failure(opened.error);
		}
		refreshedMaterial = std::move(opened.value);
	}
	for (Size index = 0; index < sessions.size(); ++index)
	{
		sessions[index]->CommitResourceReload(std::move(reloads[index]));
	}
	if (refreshedMaterial)
	{
		current = std::move(refreshedMaterial);
	}

	return {};
}
} // namespace Ursa::Editor
