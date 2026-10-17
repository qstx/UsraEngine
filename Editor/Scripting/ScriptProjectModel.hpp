#pragma once

#include <Assets/AssetDatabase.hpp>
#include <filesystem>

namespace Ursa::Editor
{
struct ScriptProjectSource
{
	ScriptSourceAsset asset;
	std::filesystem::path nativePath;
};

struct ScriptFrameworkReference
{
	String name;
	std::filesystem::path nativePath;
	bool editorOnly = false;
	String contentIdentity;
};

// A source/configuration snapshot, independent of Roslyn and published project assemblies.
struct ScriptProjectInput
{
	Vector<ScriptProjectSource> sources;
	Vector<ScriptFrameworkReference> references;
	String targetFramework = "net10.0";
	String languageVersion = "14.0";
	String nullable = "enable";
	String implicitUsings = "disable";

	Vector<ScriptSourceAsset> SourceAssets() const;

	String StructureFingerprint() const;

	String Properties() const;

	String CompileItems(ScriptAssemblyGroup group, const std::filesystem::path& projectDirectory = {},
	                    bool linkAssets = false) const;

	String FrameworkItems(ScriptAssemblyGroup group) const;
};

String EscapeProjectXml(const String& value);

Result<ScriptProjectInput> CollectScriptProjectInput(VirtualFileSystem& filesystem, AssetDatabase& database,
                                                     const std::filesystem::path& runtimeAssembly,
                                                     const std::filesystem::path& editorAssembly);
} // namespace Ursa::Editor
