#pragma once

#include <Core/Result.hpp>

namespace Ursa::Profiling
{
bool CpuMarkersEnabled() noexcept;

Result<UInt64> RegisterCpuMarker(const String& name, const String& file, const String& function, Int32 line);

Result<UInt64> BeginCpuMarker(UInt64 marker);

Result<void> EndCpuMarker(UInt64 token);
} // namespace Ursa::Profiling
