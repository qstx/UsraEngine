#pragma once

#include <Workspace/ProjectWorkspace.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/VirtualPath.hpp>
#include <functional>
#include <memory>
#include <typeindex>

namespace Ursa::Editor
{
using DocumentWriter = std::function<Result<void>(VirtualFileSystem&, const VirtualPath&, ResourceId)>;

class EditorDocument final
{
public:
	static Result<std::unique_ptr<EditorDocument>> Create(ResourceKind documentKind, ResourceId documentId,
	                                                      VirtualPath documentPath, DocumentWriter documentWriter);

	ResourceKind Kind() const
	{
		return kind;
	}

	ResourceId Id() const
	{
		return id;
	}

	const VirtualPath& Path() const
	{
		return path;
	}

	void SetPath(VirtualPath newPath)
	{
		this->path = std::move(newPath);
		dirty = true;
	}

	bool Dirty() const
	{
		return dirty;
	}

	void MarkDirty()
	{
		dirty = true;
		++revision;
	}

	UInt64 Revision() const
	{
		return revision;
	}

	UInt64 SaveVersion() const
	{
		return saveVersion;
	}

	template <class T> void SetPayload(std::shared_ptr<T> value)
	{
		payload = std::move(value);
		payloadType = std::type_index(typeid(T));
	}

	template <class T> std::shared_ptr<T> Payload() const
	{
		return payloadType == std::type_index(typeid(T)) ? std::static_pointer_cast<T>(payload) : nullptr;
	}

private:
	EditorDocument(ResourceKind documentKind, ResourceId documentId, VirtualPath documentPath,
	               DocumentWriter documentWriter)
	    : kind(documentKind), id(documentId), path(std::move(documentPath)), writer(std::move(documentWriter))
	{
	}

	friend class EditorDocuments;

	ResourceKind kind;
	ResourceId id;
	VirtualPath path;
	DocumentWriter writer;
	bool dirty = false;
	UInt64 revision = 0;
	UInt64 saveVersion = 0;
	std::shared_ptr<void> payload;
	std::type_index payloadType{typeid(void)};
};

enum class UnsavedChoice
{
	Save,
	Discard,
	Cancel
};

class EditorDocuments final
{
public:
	static Result<std::unique_ptr<EditorDocuments>> Create(ProjectWorkspace& projectWorkspace);

	Result<void> RequestOpen(std::unique_ptr<EditorDocument> document);

	Result<void> RequestClose();

	Result<void> ResolvePrompt(UnsavedChoice choice);

	Result<void> SaveCurrent();

	Result<void> RefreshResources();

	Result<void> RenameCurrent(const VirtualPath& path);

	void SetSaveBlocked(bool blocked)
	{
		saveBlocked = blocked;
	}

	void DrawUnsavedPrompt();

	bool PromptPending() const
	{
		return promptPending;
	}

	EditorDocument* Current() const
	{
		return current.get();
	}

	ResourceSession& Session() const
	{
		return *sessions.back();
	}

	const String& LastError() const
	{
		return lastError;
	}

private:
	explicit EditorDocuments(ProjectWorkspace& projectWorkspace) : workspace(projectWorkspace)
	{
	}

	void FinishPending();

	ProjectWorkspace& workspace;
	std::unique_ptr<EditorDocument> current;
	std::unique_ptr<EditorDocument> pending;
	bool pendingClose = false;
	bool promptPending = false;
	bool saveBlocked = false;
	String lastError;
	Vector<std::unique_ptr<ResourceSession>> sessions;
};
} // namespace Ursa::Editor
