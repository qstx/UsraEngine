#pragma once

#include <Scene/ComponentEditors.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Scene/ViewportLayout.hpp>
#include <optional>

namespace Ursa::Editor
{
class AssetImporterInspector;
class AssetPreviewExtension;
struct AssetPreviewTarget;
using ManagedExtensionDescriptor = Scripting::ManagedEditorExtension;
using ManagedExtensionKind = Scripting::ManagedEditorExtensionKind;

struct ManagedEditorContext
{
	EditorDocument* document = nullptr;
	EditorDocuments* documents = nullptr;
	ProjectWorkspace* workspace = nullptr;
	const Vector<ScriptTypeDescriptor>* scriptTypes = nullptr;
	ComponentRegistry* registry = nullptr;
	Component* target = nullptr;
	Vector<Component*> componentTargets;
	Size componentPrimaryIndex = 0;
	Vector<GameObject::Ptr> gameObjectTargets;
	Size gameObjectPrimaryIndex = 0;
	SerializedObject* serializedProperties = nullptr;
	SerializedObjectGroup* serializedPropertyGroup = nullptr;
	bool supportsMultipleObjects = false;
	std::shared_ptr<MaterialInstance> targetMaterial;
	MeshPtr targetMesh;
	MaterialTexturePtr targetTexture;
	AssetPreviewExtension* preview = nullptr;
	std::function<Result<std::shared_ptr<AssetPreviewExtension>>(const AssetPreviewTarget&)> createReferencePreview;
	bool previewProperties = false;
	bool canEditTarget = true;
	ComponentInspectorContext selection;
	ComponentSaveContext saveContext;
	std::function<Result<void>(const VirtualPath&)> openAsset;
	std::optional<AssetReference> assetSelection;
	String assetSelectionName;
	String targetIdentity;
	std::optional<SerializedTargetAdapter> serializedTarget;
	std::function<Result<void>()> validateTarget;
	std::function<void()> markTargetDirty;
	std::function<Result<void>()> saveTarget;
	bool targetUsesDocument = true;
	std::shared_ptr<AssetImporterInspector> importerInspector;
	std::function<void(const VirtualPath&)> requestImport;
	bool canApplyImporter = true;
	bool importerControlsDrawn = false;
};

#if defined(URSA_ENABLE_DOTNET)
Result<bool> InvokeManagedEditorExtension(Scripting::DotNetHost& host, UInt64 sessionId, const String& extensionId,
                                          ManagedEditorContext& context);
Result<Scripting::ManagedPreviewResult> InvokeManagedEditorPreview(Scripting::DotNetHost& host, UInt64 sessionId,
                                                                   const String& extensionId, UInt64 instance,
                                                                   Scripting::ManagedPreviewPhase phase,
                                                                   const ViewportRect& rect,
                                                                   ManagedEditorContext& context);
#endif
} // namespace Ursa::Editor
