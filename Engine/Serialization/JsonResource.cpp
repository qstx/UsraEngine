#include <Serialization/JsonInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/AtomicFile.hpp>
#include <fstream>
#include <iterator>

namespace Ursa
{
Result<nlohmann::json> ReadJsonResource(const std::filesystem::path& path, ResourceKind expectedKind)
{
	const auto header = ReadResourceHeader(path);
	if (!header)
	{
		return Result<nlohmann::json>::Failure(header.error);
	}
	if (header.value.kind != expectedKind)
	{
		return Result<nlohmann::json>::Failure("Unexpected resource kind at " + PathToUrsaString(path));
	}
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		return Result<nlohmann::json>::Failure("Cannot open JSON resource at " + PathToUrsaString(path));
	}
	const std::string content(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
	const auto document = nlohmann::json::parse(content, nullptr, false);
	if (document.is_discarded() || !document.is_object())
	{
		return Result<nlohmann::json>::Failure("Invalid JSON resource at " + PathToUrsaString(path));
	}
	return Result<nlohmann::json>::Success(document);
}

Result<void> WriteJsonResource(const std::filesystem::path& path, const nlohmann::json& document)
{
	std::string content;
	try
	{
		content = document.dump(2);
	}
	catch (const nlohmann::json::exception& error)
	{
		return Result<void>::Failure("Cannot encode JSON resource " + PathToUrsaString(path) + ": " + error.what());
	}
	if (content.size() > maxJsonResourceBytes)
	{
		return Result<void>::Failure("JSON resource exceeds size limit at " + PathToUrsaString(path));
	}
	return WriteResourceAtomically(path,
	                               [&](std::ostream& output)
	                               {
		                               output.write(content.data(), static_cast<std::streamsize>(content.size()));
		                               return output ? Result<void>{}
		                                             : Result<void>::Failure("Cannot write JSON resource " +
		                                                                     PathToUrsaString(path));
	                               });
}
} // namespace Ursa
