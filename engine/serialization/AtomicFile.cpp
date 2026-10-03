#include <serialization/AtomicFile.hpp>
#include <serialization/ResourceId.hpp>
#include <core/StringInterop.hpp>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace ursa {
namespace {
Result<void> replaceFile(const std::filesystem::path &temporary, const std::filesystem::path &target) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
                          nullptr))
            return Result<void>::failure("Cannot replace resource " + pathToUrsaString(target) + ": Windows error " +
                                         toUrsaString(std::to_string(GetLastError())));
    } else {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return Result<void>::failure("Cannot inspect target resource " + pathToUrsaString(target) +
                                         ": Windows error " + toUrsaString(std::to_string(error)));
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
            return Result<void>::failure("Cannot install resource " + pathToUrsaString(target) + ": Windows error " +
                                         toUrsaString(std::to_string(GetLastError())));
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error)
        return Result<void>::failure("Cannot replace resource " + pathToUrsaString(target) + ": " +
                                     toUrsaString(error.message()));
#endif
    return {};
}
} // namespace

Result<void> writeResourceAtomically(const std::filesystem::path &target, const ResourceWriteFunction &write,
                                     const ResourceReplaceFunction &replace) {
    if (!write)
        return Result<void>::failure("Resource writer is empty");
    std::error_code error;
    const auto absolute = std::filesystem::absolute(target, error);
    if (error)
        return Result<void>::failure("Cannot resolve resource target " + pathToUrsaString(target) + ": " +
                                     toUrsaString(error.message()));
    const auto temporary = absolute.parent_path() / (absolute.filename().u8string() + "." +
                                                     toStdString(ResourceId::generate().toString()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return Result<void>::failure("Cannot create temporary resource file " + pathToUrsaString(temporary));
        auto result = write(output);
        if (result) {
            output.flush();
            if (!output)
                result = Result<void>::failure("Cannot flush temporary resource file " + pathToUrsaString(temporary));
        }
        output.close();
        if (result && !output)
            result = Result<void>::failure("Cannot close temporary resource file " + pathToUrsaString(temporary));
        if (!result) {
            std::filesystem::remove(temporary, error);
            return result;
        }
    }
    const auto result = replace ? replace(temporary, absolute) : replaceFile(temporary, absolute);
    if (!result)
        std::filesystem::remove(temporary, error);
    return result;
}
} // namespace ursa
