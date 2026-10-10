#include <Assets/ModelImportView.hpp>
#include <Assets/ProjectAssetImporter.hpp>
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

void DrawModelNode(const Json& nodes, Ursa::UInt64 localFileId)
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
				}
			}
			if (node.contains("children"))
			{
				for (const Json& child : node["children"])
				{
					DrawModelNode(nodes, child.get<Ursa::UInt64>());
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
	    loaded.value.value("version", Ursa::UInt32{0}) != 1 || !loaded.value.contains("nodes") ||
	    !loaded.value["nodes"].is_array())
	{
		return Result<void>::Failure("Imported model artifact has an unsupported document at " +
		                             imported.artifactPath.ToString());
	}
	document = std::move(loaded.value);
	hasImported = true;
	return {};
}

Result<void> ModelImportView::Reimport(AssetDatabase& database, bool cachedOnly)
{
	if (!cachedOnly)
	{
		auto savedSettings = ModelImporter::SetSettings(database, sourcePath, settings);
		if (!savedSettings)
		{
			diagnostic = savedSettings.error;
			return savedSettings;
		}
	}
	auto result = cachedOnly ? ProjectAssetImporter::LoadCurrent(database, sourcePath)
	                         : ProjectAssetImporter{}.Import(database, sourcePath, true);
	if (!result)
	{
		diagnostic = result.error;
		return Result<void>::Failure(diagnostic);
	}
	const auto root = std::find_if(result.value.artifacts.begin(), result.value.artifacts.end(),
	                               [](const auto& artifact) { return artifact.localFileId == 0; });
	if (root == result.value.artifacts.end())
	{
		return Result<void>::Failure("Model importer produced no root artifact");
	}
	imported = {result.value.guid,      root->path,
	            result.value.artifacts, result.value.dependencies,
	            result.value.cacheHit,  result.value.diagnostics};
	auto refreshed = RefreshImportedDocument(database.Filesystem());
	if (!refreshed)
	{
		diagnostic = refreshed.error;
		return refreshed;
	}
	dirty = false;
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

Result<void> ModelImportView::Draw(AssetDatabase& database, bool canReimport,
                                   const std::function<Result<void>()>& instantiate,
                                   const std::function<void(const VirtualPath&)>& requestImport)
{
	ImGui::Begin("Model Import");
	ImGui::TextUnformatted(AssetDisplayPath(sourcePath).c_str());
	ImGui::TextDisabled("Source stays in Assets; imported model data is cached in Library.");
	float scaleFactor = settings.scaleFactor;
	if (ImGui::DragFloat("Scale Factor", &scaleFactor, 0.01f, 0.001f, 10000.0f, "%.3f"))
	{
		settings.scaleFactor = scaleFactor;
		dirty = true;
	}
	if (ImGui::Checkbox("Generate Normals", &settings.generateNormals))
	{
		dirty = true;
	}
	if (ImGui::Checkbox("Generate Tangents", &settings.generateTangents))
	{
		dirty = true;
	}
	if (ImGui::Checkbox("Flip UVs", &settings.flipUvs))
	{
		dirty = true;
	}
	if (ImGui::Checkbox("Import Materials", &settings.importMaterials))
	{
		dirty = true;
	}
	ImGui::BeginDisabled(!settings.importMaterials);
	if (ImGui::Checkbox("Import Textures", &settings.importTextures))
	{
		dirty = true;
	}
	ImGui::EndDisabled();
	static constexpr const char* influenceOptions[] = {"1", "2", "3", "4"};
	int influenceCount = static_cast<int>(settings.maxBoneInfluences) - 1;
	if (ImGui::Combo("Max Bone Influences", &influenceCount, influenceOptions, 4))
	{
		settings.maxBoneInfluences = static_cast<Ursa::UInt32>(influenceCount + 1);
		dirty = true;
	}
	ImGui::BeginDisabled(!canReimport || (!dirty && hasImported));
	const bool reimport = ImGui::Button("Reimport");
	ImGui::EndDisabled();
	if (reimport)
	{
		auto result = requestImport ? ModelImporter::SetSettings(database, sourcePath, settings) : Reimport(database);
		if (result && requestImport)
		{
			requestImport(sourcePath);
			dirty = false;
		}

		if (!result)
		{
			diagnostic = result.error;
		}
	}
	if (hasImported)
	{
		ImGui::BeginDisabled(!instantiate);
		if (ImGui::Button("Add to Scene") && instantiate)
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
			DrawModelNode(document["nodes"], document.value("rootNodeLocalFileId", Ursa::UInt64{0}));
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Sub-assets"))
		{
			for (const ImportedArtifactLocation& subAsset : imported.subAssets)
			{
				if (subAsset.localFileId == 0)
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
	ImGui::End();
	return {};
}
} // namespace Ursa::Editor
