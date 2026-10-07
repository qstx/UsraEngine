#include <Assets/AssetProjectMigrator.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/AssetReference.hpp>
#include <Serialization/ResourceFile.hpp>
#include <algorithm>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

struct FileRewrite
{
	VirtualPath source;
	VirtualPath backup;
	Vector<Ursa::UInt8> original;
	Vector<Ursa::UInt8> replacement;
};

Vector<Ursa::UInt8> BytesOf(const std::string& text)
{
	Vector<Ursa::UInt8> bytes;
	bytes.reserve(text.size());
	for (unsigned char character : text)
	{
		bytes.push_back(character);
	}
	return bytes;
}

bool EndsWith(const String& value, const char* suffix)
{
	const String ending = suffix;
	return value.size() >= ending.size() && value.compare(value.size() - ending.size(), ending.size(), ending) == 0;
}

Result<ResourceId> ShaderSourceId(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto source = filesystem.ReadText(path);
	if (!source)
	{
		return Result<ResourceId>::Failure(source.error);
	}
	Ursa::Size cursor = 0;
	while (cursor < source.value.size())
	{
		const auto end = source.value.find('\n', cursor);
		const String line = source.value.substr(cursor, end == String::npos ? String::npos : end - cursor);
		const auto marker = line.find("ID");
		if (marker != String::npos && (marker == 0 || line[marker - 1] == ' ' || line[marker - 1] == '\t') &&
		    marker + 2 < line.size() && (line[marker + 2] == ' ' || line[marker + 2] == '\t'))
		{
			const auto firstQuote = line.find('"', marker + 2);
			const auto secondQuote = firstQuote == String::npos ? String::npos : line.find('"', firstQuote + 1);
			if (firstQuote == String::npos || secondQuote == String::npos)
			{
				return Result<ResourceId>::Failure("Malformed Shader ID declaration at " + path.ToString());
			}
			return ResourceId::Parse(line.substr(firstQuote + 1, secondQuote - firstQuote - 1));
		}
		if (end == String::npos)
		{
			break;
		}
		cursor = end + 1;
	}
	return Result<ResourceId>::Failure("Shader source has no ID declaration at " + path.ToString());
}

bool IsReferenceField(const String& field)
{
	return field == "material" || field == "prefab" || field == "shaderId" || field == "textureId";
}

Result<bool> ConvertReferenceValue(Json& value, const String& location)
{
	if (value.is_null())
	{
		return Result<bool>::Success(false);
	}
	if (value.is_object())
	{
		auto reference = AssetReferenceFromJson(value, location);
		return reference ? Result<bool>::Success(false) : Result<bool>::Failure(reference.error);
	}
	auto reference = AssetReferenceFromJson(value, location);
	if (!reference)
	{
		return Result<bool>::Failure(reference.error);
	}
	if (reference.value.localFileId != 0)
	{
		return Result<bool>::Success(false);
	}
	const Json converted = AssetReferenceToJson(reference.value);
	if (value == converted)
	{
		return Result<bool>::Success(false);
	}
	value = converted;
	return Result<bool>::Success(true);
}

Result<Ursa::UInt32> ConvertReferences(Json& value, const String& location)
{
	Ursa::UInt32 converted = 0;
	if (value.is_object())
	{
		for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
		{
			const String field = ToUrsaString(iterator.key());
			if (IsReferenceField(field))
			{
				auto changed = ConvertReferenceValue(iterator.value(), location + "." + field);
				if (!changed)
				{
					return Result<Ursa::UInt32>::Failure(changed.error);
				}
				converted += changed.value ? 1u : 0u;
				continue;
			}
			if (field == "textures" && iterator.value().is_object())
			{
				for (auto texture = iterator.value().begin(); texture != iterator.value().end(); ++texture)
				{
					auto changed =
					    ConvertReferenceValue(texture.value(), location + ".textures." + ToUrsaString(texture.key()));
					if (!changed)
					{
						return Result<Ursa::UInt32>::Failure(changed.error);
					}
					converted += changed.value ? 1u : 0u;
				}
				continue;
			}
			auto nested = ConvertReferences(iterator.value(), location + "." + field);
			if (!nested)
			{
				return nested;
			}
			converted += nested.value;
		}
	}
	else if (value.is_array())
	{
		for (Ursa::Size index = 0; index < value.size(); ++index)
		{
			auto nested = ConvertReferences(value[index], location + "[" + ToUrsaString(std::to_string(index)) + "]");
			if (!nested)
			{
				return nested;
			}
			converted += nested.value;
		}
	}
	return Result<Ursa::UInt32>::Success(converted);
}

Result<VirtualPath> BackupPath(const String& backupRoot, const VirtualPath& original)
{
	return VirtualPath::Parse(backupRoot + "/" + original.scheme + "/" + original.relative + ".backup");
}

Result<void> RestoreFiles(VirtualFileSystem& filesystem, const Vector<FileRewrite>& files)
{
	Vector<VirtualPath> restored;
	for (const FileRewrite& file : files)
	{
		auto write = filesystem.WriteAtomic(file.source, file.original);
		if (!write)
		{
			return Result<void>::Failure("Cannot restore " + file.source.ToString() + ": " + write.error);
		}
		restored.push_back(file.source);
	}
	return {};
}
} // namespace

Result<AssetMigrationSummary> AssetProjectMigrator::Migrate(AssetDatabase& database)
{
	VirtualFileSystem& filesystem = database.Filesystem();
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return Result<AssetMigrationSummary>::Failure("Asset migration needs a writable lib: backup mount");
	}

	Vector<AssetGuidAssignment> assignments;
	Vector<FileRewrite> rewrites;
	Ursa::UInt32 convertedReferenceCount = 0;
	Ursa::UInt32 embeddedGuidCount = 0;
	for (const AssetRecord& asset : database.Assets())
	{
		if (asset.metadata.folder)
		{
			continue;
		}
		Result<ResourceId> embeddedId = Result<ResourceId>::Failure("Asset has no embedded identity");
		if (EndsWith(asset.path.relative, ".ursashader"))
		{
			embeddedId = ShaderSourceId(filesystem, asset.path);
		}
		else
		{
			auto kind = ResourceKindForPath(asset.path);
			if (kind)
			{
				auto header = ReadResourceHeader(filesystem, asset.path);
				if (!header)
				{
					return Result<AssetMigrationSummary>::Failure(header.error);
				}
				embeddedId = Result<ResourceId>::Success(header.value.id);
			}
		}
		if (embeddedId)
		{
			++embeddedGuidCount;
			if (embeddedId.value != asset.metadata.guid)
			{
				assignments.push_back({asset.path, embeddedId.value});
			}
		}

		if (!EndsWith(asset.path.relative, ".ursamat") && !EndsWith(asset.path.relative, ".ursascene") &&
		    !EndsWith(asset.path.relative, ".ursaprefab"))
		{
			continue;
		}
		auto original = filesystem.Read(asset.path);
		if (!original)
		{
			return Result<AssetMigrationSummary>::Failure(original.error);
		}
		try
		{
			Json document =
			    Json::parse(std::string(reinterpret_cast<const char*>(original.value.data()), original.value.size()));
			auto converted = ConvertReferences(document, asset.path.ToString());
			if (!converted)
			{
				return Result<AssetMigrationSummary>::Failure(converted.error);
			}
			if (converted.value > 0)
			{
				convertedReferenceCount += converted.value;
				rewrites.push_back({asset.path, {}, std::move(original.value), BytesOf(document.dump(2))});
			}
		}
		catch (const std::exception& error)
		{
			return Result<AssetMigrationSummary>::Failure("Cannot parse legacy resource " + asset.path.ToString() +
			                                              ": " + ToUrsaString(error.what()));
		}
	}

	const String backupRoot = String(editorLibraryScheme) + ":Migrations/" + ResourceId::Generate().ToString();
	Vector<FileRewrite> backups = rewrites;
	for (const AssetGuidAssignment& assignment : assignments)
	{
		auto sidecar = VirtualPath::Parse(assignment.path.ToString() + ".meta");
		if (!sidecar)
		{
			return Result<AssetMigrationSummary>::Failure(sidecar.error);
		}
		auto bytes = filesystem.Read(sidecar.value);
		if (!bytes)
		{
			return Result<AssetMigrationSummary>::Failure(bytes.error);
		}
		backups.push_back({sidecar.value, {}, std::move(bytes.value), {}});
	}
	for (FileRewrite& backup : backups)
	{
		auto backupPath = BackupPath(backupRoot, backup.source);
		if (!backupPath)
		{
			return Result<AssetMigrationSummary>::Failure(backupPath.error);
		}
		backup.backup = std::move(backupPath.value);
	}

	Json manifest = Json::array();
	Vector<VirtualPath> backupFiles;
	for (const FileRewrite& backup : backups)
	{
		auto saved = filesystem.WriteAtomic(backup.backup, backup.original);
		if (!saved)
		{
			for (const VirtualPath& path : backupFiles)
			{
				filesystem.Remove(path);
			}
			return Result<AssetMigrationSummary>::Failure("Cannot create migration backup: " + saved.error);
		}
		backupFiles.push_back(backup.backup);
		manifest.push_back(
		    {{"source", ToStdString(backup.source.ToString())}, {"backup", ToStdString(backup.backup.ToString())}});
	}
	auto manifestPath = VirtualPath::Parse(backupRoot + "/Manifest.json");
	if (!manifestPath)
	{
		return Result<AssetMigrationSummary>::Failure(manifestPath.error);
	}
	auto manifestSaved = filesystem.WriteAtomic(manifestPath.value, BytesOf(manifest.dump(2)));
	if (!manifestSaved)
	{
		for (const VirtualPath& path : backupFiles)
		{
			filesystem.Remove(path);
		}
		return Result<AssetMigrationSummary>::Failure("Cannot write migration manifest: " + manifestSaved.error);
	}

	auto seeded = database.ApplyGuidMigration(assignments);
	if (!seeded)
	{
		return Result<AssetMigrationSummary>::Failure(seeded.error);
	}
	for (const FileRewrite& rewrite : rewrites)
	{
		auto saved = filesystem.WriteAtomic(rewrite.source, rewrite.replacement);
		if (!saved)
		{
			auto restored = RestoreFiles(filesystem, backups);
			database.Refresh();
			return Result<AssetMigrationSummary>::Failure("Migration failed and was rolled back: " +
			                                              (restored ? saved.error : restored.error));
		}
	}
	auto refreshed = database.Refresh();
	if (!refreshed)
	{
		auto restored = RestoreFiles(filesystem, backups);
		database.Refresh();
		return Result<AssetMigrationSummary>::Failure("Migration validation failed and was rolled back: " +
		                                              (restored ? refreshed.error : restored.error));
	}
	return Result<AssetMigrationSummary>::Success({embeddedGuidCount, convertedReferenceCount, backupRoot});
}

Result<void> AssetProjectMigrator::Restore(VirtualFileSystem& filesystem, const String& backupRoot)
{
	auto manifestPath = VirtualPath::Parse(backupRoot + "/Manifest.json");
	if (!manifestPath)
	{
		return Result<void>::Failure(manifestPath.error);
	}
	auto text = filesystem.ReadText(manifestPath.value);
	if (!text)
	{
		return Result<void>::Failure(text.error);
	}
	try
	{
		const Json manifest = Json::parse(ToStdString(text.value));
		if (!manifest.is_array())
		{
			return Result<void>::Failure("Invalid migration backup manifest at " + manifestPath.value.ToString());
		}
		Vector<FileRewrite> backups;
		for (const Json& entry : manifest)
		{
			if (!entry.is_object() || !entry.contains("source") || !entry["source"].is_string() ||
			    !entry.contains("backup") || !entry["backup"].is_string())
			{
				return Result<void>::Failure("Invalid entry in migration backup manifest");
			}
			auto source = VirtualPath::Parse(ToUrsaString(entry["source"].get<std::string>()));
			auto backup = VirtualPath::Parse(ToUrsaString(entry["backup"].get<std::string>()));
			if (!source || !backup)
			{
				return Result<void>::Failure("Invalid path in migration backup manifest");
			}
			auto bytes = filesystem.Read(backup.value);
			if (!bytes)
			{
				return Result<void>::Failure(bytes.error);
			}
			backups.push_back({std::move(source.value), std::move(backup.value), std::move(bytes.value), {}});
		}
		for (const FileRewrite& backup : backups)
		{
			auto writable = filesystem.CheckWritable(backup.source);
			if (!writable)
			{
				return writable;
			}
		}
		return RestoreFiles(filesystem, backups);
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Cannot parse migration backup manifest: " + ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
