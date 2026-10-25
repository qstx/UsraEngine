#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <ScriptableAssets/ScriptableAsset.hpp>
#include <nlohmann/json.hpp>
#include <Assets/NativeAssetInspector.hpp>
#include <functional>

namespace Ursa::Editor
{
class ScriptableAssetInspector;
class ImportedAssetInspector;
struct ManagedEditorContext;

class AssetInspectorPanel final
{
public:
	using OpenAsset = std::function<Result<void>(const ProjectBrowserEntry&)>;
	using RequestImport = std::function<void(const VirtualPath&)>;
	bool HasModifiedSource() const;
	VirtualPath ModifiedSourcePath() const;
	ImporterSelectionResult DrawSourceSelectionPrompt(const RequestImport& requestImport, bool canSave);
	Result<bool> SaveSelectedSource(const EditorSelectionContext& selection);
	EditorDocument* SelectedDocument(const EditorSelectionContext& selection) const;
	bool RetainsModifiedTargets(const EditorSelectionContext& next) const;

	void Reset()
	{
		inspectedImportedKey.clear();
		importedInspector.reset();
		inspectedScriptableKey.clear();
		scriptableInspector.reset();
		inspectedMeshKey.clear();
		inspectedMesh.reset();
		inspectedNativeKey.clear();
		nativeInspector.reset();
		multiMaterialTargets.clear();
		multiMaterialSelectionKey.clear();
		multiMaterialDiagnostic.clear();
		multiScriptableTargets.clear();
		multiMaterialPrimaryIndex = 0;
		nativeDiagnostic.clear();
		scriptableDiagnostic.clear();
	}

	void Draw(ProjectWorkspace& workspace, const ProjectBrowserModel& model, const EditorSelectionContext& selection,
	          bool& open, const Vector<ScriptTypeDescriptor>* scriptTypes, const OpenAsset& openAsset,
	          const RequestImport& requestImport, Float32 footerHeight = 0,
	          const std::function<void(Float32)>& footer = {}, const std::function<void()>& properties = {},
	          const std::function<Result<bool>(ManagedEditorContext&)>& custom = {},
	          const ComponentRegistry* components = nullptr,
	          const NativeAssetInspector::DefaultDraw& nativeDefault = {},
	          EditorDocuments* existingDocuments = nullptr);

private:
	struct MultiMaterialTarget
	{
		VirtualPath source;
		ResourceId guid;
		std::shared_ptr<NativeAssetInspector> inspector;
	};

	struct MultiScriptableTarget
	{
		VirtualPath source;
		ResourceId guid;
		std::shared_ptr<ScriptableAssetInspector> inspector;
	};

	Result<bool> DrawMultiMaterialInspector(ProjectWorkspace& workspace, const ProjectBrowserModel& model,
	                                        const EditorSelectionContext& selection,
	                                        const ComponentRegistry* components, EditorDocuments* existingDocuments,
	                                        const RequestImport& requestImport);
	Result<void> SaveMultiMaterialTargets(const RequestImport& requestImport);
	Result<bool> DrawMultiScriptableInspector(ProjectWorkspace& workspace, const ProjectBrowserModel& model,
	                                          const EditorSelectionContext& selection,
	                                          const Vector<ScriptTypeDescriptor>* scriptTypes,
	                                          const RequestImport& requestImport);
	Result<void> SaveMultiScriptableTargets(const RequestImport& requestImport);
	String MultiSelectionDiagnostic(const ProjectBrowserModel& model, const EditorSelectionContext& selection) const;

	String inspectedImportedKey;
	String importedDiagnostic;
	std::shared_ptr<ImportedAssetInspector> importedInspector;
	String inspectedMeshKey;
	MeshPtr inspectedMesh;
	String meshDiagnostic;
	String inspectedScriptableKey;
	String scriptableDiagnostic;
	std::shared_ptr<ScriptableAssetInspector> scriptableInspector;
	String inspectedNativeKey;
	String nativeDiagnostic;
	std::shared_ptr<NativeAssetInspector> nativeInspector;
	Vector<MultiMaterialTarget> multiMaterialTargets;
	Vector<MultiScriptableTarget> multiScriptableTargets;
	String multiMaterialSelectionKey;
	String multiMaterialDiagnostic;
	Size multiMaterialPrimaryIndex = 0;
};
} // namespace Ursa::Editor
