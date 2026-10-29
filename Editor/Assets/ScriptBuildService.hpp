#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Core/Result.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Scripting/ScriptProjectModel.hpp>
#include <Scripting/ScriptDiagnostic.hpp>
#include <Scripting/ScriptIdeProject.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>

namespace Ursa::Editor
{
struct ScriptProjectPaths
{
	VirtualPath gameProject;
	VirtualPath editorProject;
	VirtualPath gameAssembly;
	VirtualPath editorAssembly;
	VirtualPath typeCatalog;
};

using ScriptTypeRecord = Ursa::ScriptTypeDescriptor;

struct ScriptBuildReport
{
	bool succeeded = false;
	ScriptAssemblyGroup group = ScriptAssemblyGroup::Game;
	VirtualPath assembly;
	String diagnostics;
	Vector<ScriptDiagnostic> records;
};

class ScriptBuildService final
{
public:
	static Result<std::unique_ptr<ScriptBuildService>>
	Open(VirtualFileSystem& filesystem, AssetDatabase& database, std::filesystem::path dotnetExecutable,
	     std::filesystem::path runtimeAssembly, std::filesystem::path editorAssembly,
	     std::filesystem::path catalogGeneratorAssembly, std::filesystem::path referenceAssemblyDirectory);

	Result<ScriptProjectPaths> GenerateProjects();

	Result<ScriptProjectInput> ProjectInput();

	Result<ScriptIdeWorkspace> GenerateIdeProjects(const std::filesystem::path& description = {},
	                                               bool preparing = false);

	const Vector<ScriptDiagnostic>& LastDiagnostics() const
	{
		return diagnosticRecords;
	}

	Result<ScriptBuildReport> Build(ScriptAssemblyGroup group, const String& configuration = "Debug");

	Result<String> SourceFingerprint();

	Result<bool> IsBuildCurrent(ScriptAssemblyGroup group, const String& configuration = "Debug");

	const Vector<ScriptTypeRecord>& ScriptTypes() const
	{
		return scriptTypes;
	}

private:
	ScriptBuildService(VirtualFileSystem& filesystem, AssetDatabase& database, std::filesystem::path dotnetExecutable,
	                   std::filesystem::path runtimeAssembly, std::filesystem::path editorAssembly,
	                   std::filesystem::path catalogGeneratorAssembly,
	                   std::filesystem::path referenceAssemblyDirectory);

	Result<void> WriteProject(const VirtualPath& path, const ScriptProjectInput& input, ScriptAssemblyGroup group,
	                          const ScriptProjectPaths& projects);

	Result<void> GenerateCatalog(const ScriptProjectInput& input, VirtualPath& catalogPath);

	Result<std::filesystem::path> EnsureToolDirectory(const VirtualPath& directory);

	Result<ScriptBuildReport> BuildProject(const VirtualPath& project, const VirtualPath& assembly,
	                                       ScriptAssemblyGroup group, const String& configuration);

	Result<void> PublishContent(ScriptAssemblyGroup group, const String& fingerprint, const VirtualPath& assembly,
	                            const VirtualPath& pdb);

	Result<String> BuildFingerprint(ScriptAssemblyGroup group, const String& configuration) const;

	VirtualFileSystem& filesystem;
	AssetDatabase& database;
	std::filesystem::path dotnetExecutable;
	std::filesystem::path runtimeAssembly;
	std::filesystem::path editorAssembly;
	std::filesystem::path catalogGeneratorAssembly;
	std::filesystem::path referenceAssemblyDirectory;
	ScriptProjectPaths generatedProjects;
	Vector<ScriptTypeRecord> scriptTypes;
	Vector<ScriptDiagnostic> diagnosticRecords;
	bool projectsGenerated = false;
};
} // namespace Ursa::Editor
