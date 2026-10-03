#include <serialization/JsonInterop.hpp>
#include <serialization/JsonResource.hpp>
#include <serialization/AtomicFile.hpp>
#include <fstream>
#include <iterator>

namespace ursa {
Result<nlohmann::json> readJsonResource(const std::filesystem::path &path, ResourceKind expectedKind) {
    const auto header = readResourceHeader(path);
    if (!header)
        return Result<nlohmann::json>::failure(header.error);
    if (header.value.kind != expectedKind)
        return Result<nlohmann::json>::failure("Unexpected resource kind at " + pathToUrsaString(path));
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Result<nlohmann::json>::failure("Cannot open JSON resource at " + pathToUrsaString(path));
    const std::string content(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    const auto document = nlohmann::json::parse(content, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return Result<nlohmann::json>::failure("Invalid JSON resource at " + pathToUrsaString(path));
    return Result<nlohmann::json>::success(document);
}

Result<void> writeJsonResource(const std::filesystem::path &path, const nlohmann::json &document) {
    std::string content;
    try {
        content = document.dump(2);
    } catch (const nlohmann::json::exception &error) {
        return Result<void>::failure("Cannot encode JSON resource " + pathToUrsaString(path) + ": " + error.what());
    }
    if (content.size() > maxJsonResourceBytes)
        return Result<void>::failure("JSON resource exceeds size limit at " + pathToUrsaString(path));
    return writeResourceAtomically(path, [&](std::ostream &output) {
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        return output ? Result<void>{} : Result<void>::failure("Cannot write JSON resource " + pathToUrsaString(path));
    });
}
} // namespace ursa
