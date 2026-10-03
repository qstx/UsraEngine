#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <filesystem>
#include <functional>
#include <ostream>

namespace Ursa
{
using ResourceWriteFunction = std::function<Result<void>(std::ostream&)>;
using ResourceReplaceFunction =
    std::function<Result<void>(const std::filesystem::path& temporary, const std::filesystem::path& target)>;

Result<void> WriteResourceAtomically(const std::filesystem::path& target, const ResourceWriteFunction& write,
                                     const ResourceReplaceFunction& replace = {});
} // namespace Ursa
