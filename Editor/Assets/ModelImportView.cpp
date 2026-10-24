#include <Ui/EditorIcons.hpp>
#include <Serialization/AssetReference.hpp>
#include <Assets/ModelImportView.hpp>
#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Assets/SceneDraft.hpp>

#include <Core/StringInterop.hpp>
#include <imgui.h>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

String AssetDisplayPath(const VirtualPath& path)
{
	return path.scheme == projectScheme ? String("Assets/") + path.relative : path.ToString();
}

Result<Json> ReadCbor(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto bytes = filesystem.Read(path);
	if (!bytes)
	{
		return Result<Json>::Failure(bytes.error);
	}
	try
	{
		return Result<Json>::Success(Json::from_cbor(bytes.value.begin(), bytes.value.end()));
	}
	catch (const std::exception& error)
	{
		return Result<Json>::Failure("Cannot decode imported model artifact " + path.ToString() + ": " +
		                             ToUrsaString(error.what()));
	}
}

String LocalFileIdText(Ursa::UInt64 localFileId)
{
	return ToUrsaString(std::to_string(localFileId));
}

void DrawModelNode(const Json& nodes, UInt64 localFileId, UInt64& selected)
{
	for (const auto& node : nodes)
	{
		if (node.value("localFileId", UInt64{0}) != localFileId)
		{
			continue;
		}
		const String name = ToUrsaString(node.value("name", std::string("Node")));
		const auto key = name + "##" + LocalFileIdText(localFileId);
		const auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
		                   ImGuiTreeNodeFlags_SpanAvailWidth |
		                   (selected == localFileId ? ImGuiTreeNodeFlags_Selected : 0) |
		                   (!node.value("parentLocalFileId", UInt64{0}) ? ImGuiTreeNodeFlags_DefaultOpen : 0) |
		                   (node.value("children", Json::array()).empty() ? ImGuiTreeNodeFlags_Leaf : 0);
		const bool open = ImGui::TreeNodeEx(key.c_str(), flags);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			selected = localFileId;
		}
		if (open)
		{
			for (const auto& child : node.value("children", Json::array()))
			{
				DrawModelNode(nodes, child.get<UInt64>(), selected);
			}
			ImGui::TreePop();
		}
		return;
	}
}

} // namespace

Result<std::unique_ptr<ModelImportView>> ModelImportView::Open(VirtualPath source, ModelImportSettings settings)
{
	if (source.scheme != projectScheme)
	{
		return Result<std::unique_ptr<ModelImportView>>::Failure(
		    "Model import settings can only be edited for a project asset: " + source.ToString());
	}
	return Result<std::unique_ptr<ModelImportView>>::Success(
	    std::unique_ptr<ModelImportView>(new ModelImportView(std::move(source), settings)));
}

Result<void> ModelImportView::RefreshImportedDocument(const VirtualFileSystem& filesystem)
{
	auto loaded = ReadCbor(filesystem, imported.artifactPath);
	if (!loaded)
	{
		return Result<void>::Failure(loaded.error);
	}
	if (loaded.value.value("format", std::string{}) != "UrsaModel" ||
	    loaded.value.value("version", Ursa::UInt32{0}) != 2 || !loaded.value.contains("nodes") ||
	    !loaded.value["nodes"].is_array())
	{
		return Result<void>::Failure("Imported model artifact has an unsupported document at " +
		                             imported.artifactPath.ToString());
	}
	document = std::move(loaded.value);
	if (std::none_of(document["nodes"].begin(), document["nodes"].end(),
	                 [&](const auto& node) { return node.value("localFileId", UInt64{0}) == selectedNode; }))
	{
		selectedNode = document.value("rootNodeLocalFileId", UInt64{0});
	}
	hasImported = true;
	return {};
}

Result<void> ModelImportView::Reimport(AssetDatabase& database, ProjectAssetImporter& importer, bool cachedOnly)
{
	if (!cachedOnly && !settingsInspector)
	{
		auto savedSettings = ModelImporter::SetSettings(database, sourcePath, settings);
		if (!savedSettings)
		{
			diagnostic = savedSettings.error;
			return savedSettings;
		}
	}
	auto result = cachedOnly ? ProjectAssetImporter::LoadCurrent(database, sourcePath)
	                         : importer.Import(database, sourcePath, true);
	if (!result)
	{
		diagnostic = result.error;
		return Result<void>::Failure(diagnostic);
	}
	const auto root =
	    std::find_if(result.value.artifacts.begin(), result.value.artifacts.end(),
	                 [&](const auto& artifact) { return artifact.localFileId == result.value.mainObjectLocalFileId; });
	if (root == result.value.artifacts.end())
	{
		return Result<void>::Failure("Model importer produced no root artifact");
	}
	imported = {result.value.guid,
	            root->path,
	            result.value.artifacts,
	            result.value.dependencies,
	            result.value.cacheHit,
	            result.value.diagnostics,
	            result.value.mainObjectLocalFileId};
	auto refreshed = RefreshImportedDocument(database.Filesystem());
	if (!refreshed)
	{
		diagnostic = refreshed.error;
		return refreshed;
	}
	diagnostic.clear();
	return {};
}

Result<void> ModelImportView::SelectNode(UInt64 localFileId)
{
	if (!hasImported || std::none_of(document["nodes"].begin(), document["nodes"].end(), [&](const auto& node)
	                                 { return node.value("localFileId", UInt64{0}) == localFileId; }))
	{
		return Result<void>::Failure("The selected model hierarchy node is missing");
	}
	selectedNode = localFileId;
	return {};
}

Result<void> ModelImportView::Instantiate(SceneDraft& draft) const
{
	if (!hasImported)
	{
		return Result<void>::Failure("Import the model before adding it to a Scene or Prefab");
	}
	auto instantiated = draft.InstantiateModel(imported.guid, document);
	return instantiated ? Result<void>{} : Result<void>::Failure(instantiated.error);
}

Result<std::shared_ptr<AssetImporterInspector>> ModelImportView::SettingsInspector(AssetDatabase& database)
{
	if (!settingsInspector)
	{
		auto opened = AssetImporterInspector::Open(database, sourcePath);
		if (!opened)
		{
			return opened;
		}
		settingsInspector = std::move(opened.value);
	}
	return Result<std::shared_ptr<AssetImporterInspector>>::Success(settingsInspector);
}

bool ModelImportView::HasModifiedSettings() const
{
	return settingsInspector && settingsInspector->HasModified();
}

Result<void> ModelImportView::Draw(AssetDatabase& database, ProjectAssetImporter& importer, bool canReimport,
                                   const std::function<Result<void>()>& instantiate,
                                   const std::function<void(const VirtualPath&)>& requestImport,
                                   ProjectWorkspace* workspace, const AssetImporterInspector::CustomDraw& custom,
                                   bool embedded)
{
	if (!embedded)
	{
		ImGui::Begin("Model Import");
	}
	ImGui::TextUnformatted(AssetDisplayPath(sourcePath).c_str());
	ImGui::TextDisabled("Source stays in Assets; imported model data is cached in Library.");
	auto target = SettingsInspector(database);
	if (!target)
	{
		if (!embedded)
		{
			ImGui::End();
		}
		return Result<void>::Failure(target.error);
	}
	auto drawn = target.value->DrawDefault(
	    [&](const VirtualPath& path)
	    {
		    const auto saved = ModelImporter::Settings(database, sourcePath);
		    if (!saved)
		    {
			    diagnostic = saved.error;
			    return;
		    }
		    settings = saved.value;
		    if (requestImport)
		    {
			    requestImport(path);
		    }
		    else
		    {
			    Reimport(database, importer);
		    }
	    },
	    canReimport, workspace, custom);
	if (!drawn)
	{
		diagnostic = drawn.error;
	}
	if (hasImported)
	{
		ImGui::BeginDisabled(!instantiate);
		if (Icons::Button("Add to Scene") && instantiate)
		{
			auto added = instantiate();
			if (!added)
			{
				diagnostic = added.error;
			}
		}
		ImGui::EndDisabled();
		ImGui::SeparatorText("Model");
		ImGui::Text("Nodes: %u  Meshes: %u  Materials: %u", static_cast<unsigned int>(document["nodes"].size()),
		            static_cast<unsigned int>(document.value("meshLocalFileIds", Json::array()).size()),
		            static_cast<unsigned int>(document.value("materialLocalFileIds", Json::array()).size()));
		ImGui::TextDisabled("GUID: %s", imported.guid.ToString().c_str());
		if (ImGui::TreeNode("Hierarchy"))
		{
			DrawModelNode(document["nodes"], document.value("rootNodeLocalFileId", UInt64{0}), selectedNode);
			for (const auto& node : document["nodes"])
			{
				if (node.value("localFileId", UInt64{0}) != selectedNode)
				{
					continue;
				}
				ImGui::SeparatorText("Node (read-only)");
				ImGui::TextUnformatted(node.value("name", std::string("Node")).c_str());
				if (node.contains("localTransform"))
				{
					for (const char* field : {"position", "rotation", "scale"})
					{
						const auto text = node["localTransform"].value(field, Json::array()).dump();
						ImGui::Text("%s: %s", field, text.c_str());
					}
				}
				for (const auto& id : node.value("meshLocalFileIds", Json::array()))
				{
					const auto object = database.ImportedObject(imported.guid, id.get<UInt64>());
					if (object && Icons::Selectable(Icons::Icon::Mesh, object.value.Label().c_str()) && selectObject)
					{
						selectObject({imported.guid, object.value.localFileId});
					}
				}
			}
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Sub-assets"))
		{
			for (const auto& object : database.VisibleImportedObjects())
			{
				if (object.guid != imported.guid || object.isMainObject)
				{
					continue;
				}
				if (Icons::Selectable(Icons::Icon::File, object.Label().c_str()) && selectObject)
				{
					selectObject({object.guid, object.localFileId});
				}
			}
			ImGui::TreePop();
		}
		if (!imported.diagnostics.empty())
		{
			if (ImGui::TreeNode("Import Diagnostics"))
			{
				for (const String& item : imported.diagnostics)
				{
					ImGui::BulletText("%s", item.c_str());
				}
				ImGui::TreePop();
			}
		}
		if (!imported.dependencies.empty() && ImGui::TreeNode("Dependencies"))
		{
			for (const VirtualPath& dependency : imported.dependencies)
			{
				ImGui::BulletText("%s", AssetDisplayPath(dependency).c_str());
			}
			ImGui::TreePop();
		}
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	if (!embedded)
	{
		ImGui::End();
	}
	return {};
}
} // namespace Ursa::Editor
