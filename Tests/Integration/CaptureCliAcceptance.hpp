#pragma once

#include <Diagnostics/CaptureChannel.hpp>

namespace Ursa::Test
{
Result<Diagnostics::CaptureResult> RunCaptureCli(const std::filesystem::path& output);
}
