#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <ScriptableAssets/ScriptableAsset.hpp>
#include <nlohmann/json.hpp>
#include <functional>

namespace Ursa::Editor
{
class AssetInspectorPanel final
{
public:
	using OpenAsset = std::function<Result<void>(const ProjectBrowserEntry&)>;
	using RequestImport = std::function<void(const VirtualPath&)>;

	void Draw(ProjectWorkspace& workspace, const ProjectBrowserModel& model, const EditorSelectionContext& selection,
	          bool& open, const Vector<ScriptTypeDescriptor>* scriptTypes, const OpenAsset& openAsset,
	          const RequestImport& requestImport);

private:
	String inspectedMeshKey;
	MeshPtr inspectedMesh;
	String meshDiagnostic;
	String inspectedScriptableKey;
	String scriptableDiagnostic;
	ScriptableAsset scriptableAsset;
	nlohmann::json scriptablePayload = nlohmann::json::object();
	bool scriptableLoaded = false;
	bool scriptableDirty = false;
};
} // namespace Ursa::Editor
