#pragma once

#include <Core/Result.hpp>
#include <Serialization/ResourceId.hpp>
#include <optional>

namespace Ursa::Editor
{
struct ImportScriptType
{
	ResourceId sourceGuid;
	UInt64 localFileId = 0;
	String clrTypeName;

	Result<String> StableIdentity() const;
	static Result<ImportScriptType> FromIdentity(const String& identity);
};

struct ScriptedImporterDescriptor
{
	ImportScriptType type;
	UInt32 version = 1;
	Vector<String> extensions;
	String defaultSettingsJson;
};

enum class AssetPostprocessorTarget : UInt32
{
	Texture = 1,
	Model = 2,
	TextureAndModel = 3
};

struct AssetPostprocessorDescriptor
{
	ImportScriptType type;
	UInt32 version = 0;
	Int32 order = 0;
	AssetPostprocessorTarget targets = AssetPostprocessorTarget::TextureAndModel;
};

struct AssetImporterDescriptor
{
	String identity;
	UInt32 version = 0;
	Vector<String> extensions;
	std::optional<ImportScriptType> scriptType;
	String defaultSettingsJson;
};

// An owned, immutable registration snapshot. Execution callbacks belong to its host session.
class AssetImportRules final
{
public:
	static Result<AssetImportRules> Create(Vector<ScriptedImporterDescriptor> scriptImporters = {},
	                                       Vector<AssetPostprocessorDescriptor> scriptPostprocessors = {},
	                                       String compiledFingerprint = {}, UInt64 selectedGeneration = 1);

	Result<const AssetImporterDescriptor*> FindImporter(const String& extension,
	                                                    const String& requiredIdentity = {}) const;
	Vector<AssetPostprocessorDescriptor> PostprocessorsFor(const String& importer, const String& sourceScheme) const;
	Result<String> FingerprintFor(const String& extension, const String& sourceScheme,
	                              const String& requiredIdentity = {}) const;

	UInt64 Generation() const
	{
		return generation;
	}

	const Vector<AssetImporterDescriptor>& Importers() const
	{
		return importers;
	}

private:
	UInt64 generation = 0;
	String codeFingerprint;
	Vector<AssetImporterDescriptor> importers;
	Vector<AssetPostprocessorDescriptor> postprocessors;
	Map<String, Size> importersByExtension;
};

Result<UInt64> StableImportObjectId(const String& importerIdentity, const String& outputKey);
} // namespace Ursa::Editor
