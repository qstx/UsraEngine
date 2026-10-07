#include <Serialization/JsonInterop.hpp>
#include <Serialization/JsonResource.hpp>

namespace Ursa
{
Result<nlohmann::json> ReadJsonResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                        ResourceKind expectedKind)
{
	auto header = ReadResourceHeader(filesystem, path);
	if (!header)
	{
		return Result<nlohmann::json>::Failure(header.error);
	}
	if (header.value.kind != expectedKind)
	{
		return Result<nlohmann::json>::Failure("Unexpected resource kind at " + path.ToString());
	}
	auto bytes = filesystem.Read(path);
	if (!bytes)
	{
		return Result<nlohmann::json>::Failure(bytes.error);
	}
	auto document = nlohmann::json::parse(bytes.value.begin(), bytes.value.end(), nullptr, false);
	return document.is_object() ? Result<nlohmann::json>::Success(std::move(document))
	                            : Result<nlohmann::json>::Failure("Invalid JSON resource at " + path.ToString());
}

Result<void> WriteJsonResource(VirtualFileSystem& filesystem, const VirtualPath& path, const nlohmann::json& document)
{
	try
	{
		auto content = document.dump(2);
		if (content.size() > maxJsonResourceBytes)
		{
			return Result<void>::Failure("JSON resource exceeds size limit at " + path.ToString());
		}
		const auto* begin = reinterpret_cast<const UInt8*>(content.data());
		return filesystem.WriteAtomic(path, Vector<UInt8>(begin, begin + content.size()));
	}
	catch (const nlohmann::json::exception& error)
	{
		return Result<void>::Failure("Cannot encode JSON resource " + path.ToString() + ": " + error.what());
	}
}

} // namespace Ursa
