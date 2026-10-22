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

void DrawModelNode(const Json& nodes, Ursa::UInt64 localFileId, ResourceId modelGuid)
{
	for (const Json& node : nodes)
	{
		if (node.value("localFileId", Ursa::UInt64{0}) != localFileId)
		{
			continue;
		}
		const String name = ToUrsaString(node.value("name", std::string("Node")));
		const String id = LocalFileIdText(localFileId);
		if (ImGui::TreeNode((name + "##" + id).c_str()))
		{
			const Json transform = node.value("transform", Json::array());
			if (transform.size() == 16)
			{
				ImGui::Text("Position: %.3f, %.3f, %.3f", transform[3].get<float>(), transform[7].get<float>(),
				            transform[11].get<float>());
			}
			if (node.contains("meshLocalFileIds"))
			{
				for (const Json& meshId : node["meshLocalFileIds"])
				{
					ImGui::BulletText("Mesh local ID: %s", LocalFileIdText(meshId.get<Ursa::UInt64>()).c_str());
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
					{
						const AssetReference reference{modelGuid, meshId.get<UInt64>()};
						ImGui::SetDragDropPayload("URSA_ASSET_REFERENCE", &reference, sizeof(reference));
						ImGui::TextUnformatted("Mesh sub-asset");
						ImGui::EndDragDropSource();
					}
				}
			}
			if (node.contains("children"))
			{
				for (const Json& child : node["children"])
				{
					DrawModelNode(nodes, child.get<Ursa::UInt64>(), modelGuid);
				}
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
			DrawModelNode(document["nodes"], document.value("rootNodeLocalFileId", Ursa::UInt64{0}), imported.guid);
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Sub-assets"))
		{
			for (const ImportedArtifactLocation& subAsset : imported.subAssets)
			{
				if (subAsset.localFileId == imported.mainObjectLocalFileId)
				{
					continue;
				}
				auto record = ReadCbor(database.Filesystem(), subAsset.path);
				if (!record)
				{
					ImGui::BulletText("%s: %s", subAsset.kind.c_str(), record.error.c_str());
					continue;
				}
				const String name = ToUrsaString(record.value.value("name", std::string(subAsset.kind.c_str())));
				ImGui::BulletText("%s: %s (local ID %s)", subAsset.kind.c_str(), name.c_str(),
				                  LocalFileIdText(subAsset.localFileId).c_str());
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
