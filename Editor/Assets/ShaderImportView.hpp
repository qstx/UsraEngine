#pragma once

#include <Shader/ShaderAsset.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <memory>
#include <optional>

namespace Ursa::Editor
{
class ShaderImportView final
{
public:
	static Result<std::unique_ptr<ShaderImportView>> Open(const VirtualFileSystem& filesystem, VirtualPath sourcePath);

	Result<void> Reimport(ProjectWorkspace& workspace, bool force = true);

	const VirtualPath& SourcePath() const
	{
		return sourcePath;
	}

	const String& SourceText() const
	{
		return sourceText;
	}

	const String& Diagnostic() const
	{
		return diagnostic;
	}

	const Vector<String>& Dependencies() const
	{
		return dependencies;
	}

	bool SourceReadOnly() const
	{
		return sourceReadOnly;
	}

	bool Imported() const
	{
		return importedAsset.has_value();
	}

private:
	ShaderImportView(VirtualPath path, String source, bool isReadOnly)
	    : sourcePath(std::move(path)), sourceText(std::move(source)), sourceReadOnly(isReadOnly)
	{
	}

	VirtualPath sourcePath;
	String sourceText;
	bool sourceReadOnly = true;
	String diagnostic;
	Vector<String> dependencies;
	std::optional<Shader::ShaderAsset> importedAsset;
};
} // namespace Ursa::Editor
