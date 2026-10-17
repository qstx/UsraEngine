#include <Scripting/ScriptProjectModel.hpp>

#include <Core/StringInterop.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Ursa::Editor
{
String EscapeProjectXml(const String& value)
{
	String escaped;
	for (char character : value)
	{
		switch (character)
		{
			case '&':
				escaped += "&amp;";
				break;
			case '<':
				escaped += "&lt;";
				break;
			case '>':
				escaped += "&gt;";
				break;
			case '"':
				escaped += "&quot;";
				break;
			case '\'':
				escaped += "&apos;";
				break;
			default:
				escaped.push_back(character);
				break;
		}
	}
	return escaped;
}

Vector<ScriptSourceAsset> ScriptProjectInput::SourceAssets() const
{
	Vector<ScriptSourceAsset> result;
	for (const auto& source : sources)
	{
		result.push_back(source.asset);
	}
	return result;
}

String ScriptProjectInput::StructureFingerprint() const
{
	UInt64 hash = 14695981039346656037ull;
	const auto add = [&](const String& value)
	{
		for (unsigned char character : value)
		{
			hash = (hash ^ character) * 1099511628211ull;
		}
		hash = (hash ^ 0xffu) * 1099511628211ull;
	};
	add(Properties());
	for (const auto& source : sources)
	{
		add(source.asset.path.ToString());
		add(source.asset.guid.ToString());
		add(PathToUrsaString(source.nativePath));
		add(source.asset.group == ScriptAssemblyGroup::Editor ? "Editor" : "Game");
	}
	for (const auto& reference : references)
	{
		add(reference.name);
		add(PathToUrsaString(reference.nativePath));
		add(reference.editorOnly ? "Editor" : "Game");
		add(reference.contentIdentity);
	}
	std::ostringstream encoded;
	encoded << std::hex << std::setfill('0') << std::setw(16) << hash;
	return ToUrsaString(encoded.str());
}

String ScriptProjectInput::Properties() const
{
	return "    <TargetFramework>" + EscapeProjectXml(targetFramework) + "</TargetFramework>\n" + "    <LangVersion>" +
	       EscapeProjectXml(languageVersion) + "</LangVersion>\n" +
	       "    <OutputType>Library</OutputType>\n    <Nullable>" + EscapeProjectXml(nullable) + "</Nullable>\n" +
	       "    <ImplicitUsings>" + EscapeProjectXml(implicitUsings) + "</ImplicitUsings>\n" +
	       "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n" +
	       "    <DebugType>portable</DebugType>\n" +
	       "    <Optimize Condition=\"'$(Configuration)' == 'Debug'\">false</Optimize>\n";
}

String ScriptProjectInput::CompileItems(ScriptAssemblyGroup group, const std::filesystem::path& projectDirectory,
                                        bool linkAssets) const
{
	String result;
	for (const auto& source : sources)
	{
		if (source.asset.group != group)
		{
			continue;
		}
		const auto path =
		    projectDirectory.empty() ? source.nativePath : source.nativePath.lexically_relative(projectDirectory);
		const auto& includePath = path.empty() ? source.nativePath : path;
		result += "    <Compile Include=\"" + EscapeProjectXml(PathToUrsaString(includePath)) + "\"";
		if (linkAssets)
		{
			result += " Link=\"Assets/" + EscapeProjectXml(source.asset.path.relative) + "\"";
		}
		result += " />\n";
	}
	return result;
}

String ScriptProjectInput::FrameworkItems(ScriptAssemblyGroup group) const
{
	String result;
	for (const auto& reference : references)
	{
		if (reference.editorOnly && group != ScriptAssemblyGroup::Editor)
		{
			continue;
		}
		result += "    <Reference Include=\"" + EscapeProjectXml(reference.name) + "\"><HintPath>" +
		          EscapeProjectXml(PathToUrsaString(reference.nativePath)) + "</HintPath></Reference>\n";
	}
	return result;
}

Result<ScriptProjectInput> CollectScriptProjectInput(VirtualFileSystem& filesystem, AssetDatabase& database,
                                                     const std::filesystem::path& runtimeAssembly,
                                                     const std::filesystem::path& editorAssembly)
{
	auto refreshed = database.Refresh();
	if (!refreshed)
	{
		return Result<ScriptProjectInput>::Failure(refreshed.error);
	}
	ScriptProjectInput input;
	for (const auto& asset : database.ScriptSources())
	{
		auto nativePath = filesystem.ResolveForExternalToolInterop(asset.path);
		if (!nativePath)
		{
			return Result<ScriptProjectInput>::Failure(nativePath.error);
		}
		input.sources.push_back({asset, std::move(nativePath.value)});
	}
	std::sort(input.sources.begin(), input.sources.end(), [](const auto& left, const auto& right)
	          { return left.asset.path.ToString() < right.asset.path.ToString(); });
	input.references = {{"Ursa.Runtime", runtimeAssembly},
	                    {"Ursa.Rendering", runtimeAssembly.parent_path() / "Ursa.Rendering.dll"},
	                    {"Ursa.Bindings", runtimeAssembly.parent_path() / "Ursa.Bindings.dll"},
	                    {"Ursa.Editor", editorAssembly, true}};
	for (auto& reference : input.references)
	{
		std::ifstream assembly(reference.nativePath, std::ios::binary);
		if (!assembly)
		{
			reference.contentIdentity = "unavailable";
			continue;
		}
		UInt64 hash = 14695981039346656037ull;
		char buffer[8192];
		while (assembly.read(buffer, sizeof(buffer)) || assembly.gcount() != 0)
		{
			for (std::streamsize index = 0; index < assembly.gcount(); ++index)
			{
				hash = (hash ^ static_cast<unsigned char>(buffer[index])) * 1099511628211ull;
			}
		}
		std::ostringstream identity;
		identity << std::hex << hash;
		reference.contentIdentity = assembly.bad() ? String("unreadable") : ToUrsaString(identity.str());
	}
	return Result<ScriptProjectInput>::Success(std::move(input));
}
} // namespace Ursa::Editor
