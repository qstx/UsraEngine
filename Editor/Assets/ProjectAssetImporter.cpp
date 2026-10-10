#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/ShaderAssetImporter.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/ModelImporter.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
String Extension(const VirtualPath& path)
{
	String extension = PathToUrsaString(PathFromUrsaString(path.relative).extension());
	std::transform(extension.begin(), extension.end(), extension.begin(), [](char value)
	               { return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value; });
	return extension;
}

Result<AssetImportResult> DefaultImport(AssetDatabase& database, const VirtualPath& source, bool force)
{
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<AssetImportResult>::Failure(metadata.error);
	}
	String kind = "Default";
	auto resourceKind = ResourceKindForPath(source);
	if (resourceKind)
	{
		auto header = ReadResourceHeader(database.Filesystem(), source);
		if (!header)
		{
			return Result<AssetImportResult>::Failure(header.error);
		}
		const char* names[]{"Texture", "Material", "Scene", "Prefab", "Shader"};
		kind = names[static_cast<int>(resourceKind.value)];
	}
	const nlohmann::json settings = metadata.value.importSettings;
	auto cached = database.CheckImport(source, "ursa.Default", 1, "native-v1", settings, {});
	if (!cached)
	{
		return Result<AssetImportResult>::Failure(cached.error);
	}
	if (!force && cached.value.upToDate)
	{
		return Result<AssetImportResult>::Success({metadata.value.guid, cached.value.artifacts, {}, true});
	}
	auto bytes = database.Filesystem().Read(source);
	if (!bytes)
	{
		return Result<AssetImportResult>::Failure(bytes.error);
	}
	auto published =
	    database.PublishImport(source, "ursa.Default", 1, "native-v1", settings, {}, {{0, kind, bytes.value}});
	if (!published)
	{
		return Result<AssetImportResult>::Failure(published.error);
	}
	auto status = database.CheckImport(source, "ursa.Default", 1, "native-v1", settings, {});
	return status ? Result<AssetImportResult>::Success({metadata.value.guid, status.value.artifacts, {}, false})
	              : Result<AssetImportResult>::Failure(status.error);
}
} // namespace

ProjectAssetImporter::ProjectAssetImporter()
{
	importers[".ursashader"] = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{
		auto imported = ShaderAssetImporter::Import(database, source, DefaultShaderCompilerTools(), force);
		if (!imported)
		{
			return Result<AssetImportResult>::Failure(imported.error);
		}
		auto metadata = database.Metadata(source);
		if (!metadata)
		{
			return Result<AssetImportResult>::Failure(metadata.error);
		}
		ImportedArtifactLocation artifact{0, "Shader", imported.value.artifactPath};
		return Result<AssetImportResult>::Success(
		    {metadata.value.guid, {artifact}, imported.value.dependencies, imported.value.cacheHit});
	};
	const ImportFunction texture = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{
		auto settings = TextureImporter::Settings(database, source);
		if (!settings)
		{
			return Result<AssetImportResult>::Failure(settings.error);
		}
		auto imported = TextureImporter::Import(database, source, settings.value, "texture-rgba8", force);
		if (!imported)
		{
			return Result<AssetImportResult>::Failure(imported.error);
		}
		ImportedArtifactLocation artifact{0, "Texture", imported.value.artifactPath};
		return Result<AssetImportResult>::Success({imported.value.guid, {artifact}, {}, imported.value.cacheHit});
	};
	for (const String extension : {String(".png"), String(".jpg"), String(".jpeg")})
	{
		importers[extension] = texture;
	}
	const ImportFunction model = [](AssetDatabase& database, const VirtualPath& source, bool force)
	{
		auto settings = ModelImporter::Settings(database, source);
		if (!settings)
		{
			return Result<AssetImportResult>::Failure(settings.error);
		}
		auto imported = ModelImporter::Import(database, source, settings.value, force);
		return imported ? Result<AssetImportResult>::Success({imported.value.guid, imported.value.subAssets,
		                                                      imported.value.dependencies, imported.value.cacheHit,
		                                                      imported.value.diagnostics})
		                : Result<AssetImportResult>::Failure(imported.error);
	};
	importers[".fbx"] = model;
	importers[".obj"] = model;
}

Result<void> ProjectAssetImporter::Register(const String& extension, ImportFunction importer)
{
	if (extension.empty() || extension.front() != '.' || !importer ||
	    !importers.emplace(extension, std::move(importer)).second)
	{
		return Result<void>::Failure("Invalid or duplicate asset importer extension: " + extension);
	}
	return {};
}

Result<AssetImportResult> ProjectAssetImporter::LoadCurrent(AssetDatabase& database, const VirtualPath& source)
{
	auto status = database.CurrentImport(source);
	if (!status || !status.value.upToDate)
	{
		return Result<AssetImportResult>::Failure(status ? "Asset import is pending: " + source.ToString()
		                                                 : status.error);
	}
	auto metadata = database.Metadata(source);
	return metadata ? Result<AssetImportResult>::Success(
	                      {metadata.value.guid, status.value.artifacts, status.value.dependencies, true})
	                : Result<AssetImportResult>::Failure(metadata.error);
}

Result<AssetImportResult> ProjectAssetImporter::Import(AssetDatabase& database, const VirtualPath& source,
                                                       bool force) const
{
	auto metadata = database.Metadata(source);
	if (!metadata || metadata.value.folder)
	{
		return Result<AssetImportResult>::Failure(metadata ? "Cannot import a folder" : metadata.error);
	}
	const auto extension = Extension(source);
	const auto selected = importers.find(extension);
	if (extension == ".ursashader")
	{
		return selected->second(database, source, force);
	}
	if (database.Filesystem().CheckWritable(source))
	{
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
		{
			auto settings = TextureImporter::Settings(database, source);
			if (!settings)
			{
				return Result<AssetImportResult>::Failure(settings.error);
			}
			if (metadata.value.importer != "ursa.Texture" || metadata.value.importerVersion != 1 ||
			    metadata.value.importSettings != settings.value.ToJson())
			{
				auto saved = TextureImporter::SetSettings(database, source, settings.value);
				if (!saved)
				{
					return Result<AssetImportResult>::Failure(saved.error);
				}
			}
		}
		else if (extension == ".fbx" || extension == ".obj")
		{
			auto settings = ModelImporter::Settings(database, source);
			if (!settings)
			{
				return Result<AssetImportResult>::Failure(settings.error);
			}
			if (metadata.value.importer != "ursa.Model" || metadata.value.importerVersion != 4 ||
			    metadata.value.importSettings != settings.value.ToJson())
			{
				auto saved = ModelImporter::SetSettings(database, source, settings.value);
				if (!saved)
				{
					return Result<AssetImportResult>::Failure(saved.error);
				}
			}
		}
	}
	VirtualFileSystem frozen;
	Vector<RuntimeContentInput> inputs;
	for (const auto& scheme : database.Filesystem().Schemes())
	{
		if (scheme != source.scheme)
		{
			for (const auto& provider : database.Filesystem().Providers())
			{
				if (provider.scheme == scheme)
				{
					auto mounted = frozen.MountProvider(scheme, provider.provider, provider.access);
					if (!mounted)
					{
						return Result<AssetImportResult>::Failure(mounted.error);
					}
				}
			}
			continue;
		}
		auto memory = std::make_shared<MemoryFileProvider>();
		auto entries = database.Filesystem().List(scheme);
		if (!entries)
		{
			return Result<AssetImportResult>::Failure(entries.error);
		}
		for (const auto& entry : entries.value)
		{
			if (selected == importers.end() || extension == ".png" || extension == ".jpg" || extension == ".jpeg")
			{
				bool needed = entry.path == source || entry.path.ToString() == source.ToString() + ".meta";
				for (auto slash = source.relative.find('/'); slash != String::npos;
				     slash = source.relative.find('/', slash + 1))
				{
					needed = needed || entry.path.relative == source.relative.substr(0, slash) + ".meta";
				}
				if (!needed)
				{
					continue;
				}
			}
			auto bytes = database.Filesystem().Read(entry.path);
			if (!bytes)
			{
				return Result<AssetImportResult>::Failure(bytes.error);
			}
			inputs.push_back(
			    {entry.path, BuildResourceFingerprint(bytes.value), static_cast<UInt64>(bytes.value.size())});
			memory->WriteAtomic(entry.path.relative, bytes.value, true);
		}
		auto mounted = frozen.MountProvider(scheme, memory, MountAccess::ReadOnly);
		if (!mounted)
		{
			return Result<AssetImportResult>::Failure(mounted.error);
		}
	}
	auto isolated = AssetDatabase::Open(frozen, database.SourceScheme());
	if (!isolated)
	{
		return Result<AssetImportResult>::Failure(isolated.error);
	}
	auto refreshed = isolated.value->Refresh();
	if (!refreshed)
	{
		return Result<AssetImportResult>::Failure(refreshed.error);
	}
	auto result = selected == importers.end() ? DefaultImport(*isolated.value, source, force)
	                                          : selected->second(*isolated.value, source, force);
	if (!result)
	{
		return result;
	}
	for (const auto& input : inputs)
	{
		auto current = CaptureRuntimeContentInput(database.Filesystem(), input.path);
		if (!current || current.value.fingerprint != input.fingerprint)
		{
			return Result<AssetImportResult>::Failure("Asset inputs changed during import: " + input.path.ToString());
		}
	}
	refreshed = database.ReloadImports();
	return refreshed ? result : Result<AssetImportResult>::Failure(refreshed.error);
}
} // namespace Ursa::Editor
