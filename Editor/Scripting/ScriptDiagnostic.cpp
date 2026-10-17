#include <Scripting/ScriptDiagnostic.hpp>

#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

UInt32 Position(const Json& document, const char* key)
{
	const auto found = document.find(key);
	if (found == document.end() || !found->is_number_integer())
	{
		return 0;
	}
	const auto value = found->get<Int64>();
	return value > 0 && value <= 2147483647 ? static_cast<UInt32>(value) : 0;
}

String DecodeFileUri(String uri)
{
	if (uri.find("file:///") == 0)
	{
		uri.erase(0, 8);
	}
	String decoded;
	const auto hex = [](char character) -> Int32
	{
		if (character >= '0' && character <= '9')
		{
			return character - '0';
		}
		if (character >= 'a' && character <= 'f')
		{
			return character - 'a' + 10;
		}
		if (character >= 'A' && character <= 'F')
		{
			return character - 'A' + 10;
		}
		return -1;
	};
	for (Size index = 0; index < uri.size(); ++index)
	{
		if (uri[index] == '%' && index + 2 < uri.size() && hex(uri[index + 1]) >= 0 && hex(uri[index + 2]) >= 0)
		{
			decoded.push_back(static_cast<char>(hex(uri[index + 1]) * 16 + hex(uri[index + 2])));
			index += 2;
		}
		else
		{
			decoded.push_back(uri[index]);
		}
	}
	return decoded;
}

VirtualPath MatchSource(const String& location, const Vector<ScriptProjectSource>& sources)
{
	const auto native = PathFromUrsaString(DecodeFileUri(location)).lexically_normal();
	for (const auto& source : sources)
	{
		if (location == source.asset.path.ToString())
		{
			return source.asset.path;
		}
#ifdef _WIN32
		if (_wcsicmp(native.c_str(), source.nativePath.lexically_normal().c_str()) == 0)
#else
		if (native == source.nativePath.lexically_normal())
#endif
		{
			return source.asset.path;
		}
	}
	return {};
}
} // namespace

Result<Vector<ScriptDiagnostic>> ReadScriptDiagnostics(VirtualFileSystem& filesystem, const VirtualPath& path,
                                                       const Vector<ScriptProjectSource>& sources, const String& stage)
{
	auto text = filesystem.ReadText(path);
	if (!text)
	{
		return Result<Vector<ScriptDiagnostic>>::Failure(text.error);
	}
	try
	{
		const auto document = Json::parse(ToStdString(text.value));
		Vector<ScriptDiagnostic> records;
		if (document.contains("Version") && document.at("Version") == 1 && document.contains("Diagnostics"))
		{
			for (const auto& entry : document.at("Diagnostics"))
			{
				records.push_back({MatchSource(ToUrsaString(entry.value("Path", std::string{})), sources),
				                   Position(entry, "Line"), Position(entry, "Column"),
				                   ToUrsaString(entry.value("Severity", std::string("error"))),
				                   ToUrsaString(entry.value("Code", std::string{})),
				                   ToUrsaString(entry.at("Message").get<std::string>()), stage});
			}
		}
		else
		{
			for (const auto& run : document.at("runs"))
			{
				for (const auto& entry : run.at("results"))
				{
					ScriptDiagnostic record;
					record.severity = ToUrsaString(entry.value("level", std::string("error")));
					record.code = ToUrsaString(entry.value("ruleId", std::string{}));
					const auto& message = entry.at("message");
					record.message = ToUrsaString(message.is_string() ? message.get<std::string>()
					                                                  : message.value("text", std::string{}));
					record.stage = stage;
					if (entry.contains("locations") && !entry.at("locations").empty())
					{
						const auto& location = entry.at("locations").at(0);
						if (location.contains("physicalLocation"))
						{
							const auto& physical = location.at("physicalLocation");
							record.source = MatchSource(
							    ToUrsaString(physical.at("artifactLocation").value("uri", std::string{})), sources);
							const auto region = physical.value("region", Json::object());
							record.line = Position(region, "startLine");
							record.column = Position(region, "startColumn");
						}
						else if (location.contains("resultFile"))
						{
							const auto& physical = location.at("resultFile");
							record.source = MatchSource(ToUrsaString(physical.value("uri", std::string{})), sources);
							const auto region = physical.value("region", Json::object());
							record.line = Position(region, "startLine");
							record.column = Position(region, "startColumn");
						}
					}
					records.push_back(std::move(record));
				}
			}
		}
		return Result<Vector<ScriptDiagnostic>>::Success(std::move(records));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<ScriptDiagnostic>>::Failure("Cannot read structured script diagnostics: " +
		                                                 ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
