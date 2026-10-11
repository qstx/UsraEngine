#pragma once

#include <Core/Types.hpp>
#include <mutex>

namespace Ursa::Profiling
{
enum class PlotUnit
{
	Number,
	Bytes,
	Percentage
};

bool IsConnected() noexcept;

void MemoryAllocation(const void* pointer, Size size, const char* pool) noexcept;

void MemoryRelease(const void* pointer, const char* pool) noexcept;

void Plot(const char* name, Float64 value, PlotUnit unit = PlotUnit::Number) noexcept;

void Event(const char* text, Size length) noexcept;

void Operation(const char* name, const char* path, const char* result) noexcept;

void ApplicationInfo(const char* name) noexcept;

void SampleCpuMemory() noexcept;

Int32 MemoryCallstackDepth() noexcept;

namespace Detail
{
void RegisterCpuCategory(Size index, const char* name) noexcept;

const char* CpuPool(Size index) noexcept;
} // namespace Detail
} // namespace Ursa::Profiling

#if defined(URSA_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#define URSA_PROFILE_JOIN_IMPL(left, right) left##right
#define URSA_PROFILE_JOIN(left, right) URSA_PROFILE_JOIN_IMPL(left, right)
#define URSA_PROFILE_DYNAMIC_SCOPE_IMPL(name, length, line)                                                            \
	ZoneNamedN(URSA_PROFILE_JOIN(ursaProfileZone_, line), "Ursa.DynamicScope", true);                                  \
	URSA_PROFILE_JOIN(ursaProfileZone_, line).Name(name, length)
// The default frame set drives Tracy's initial FPS display and global frame count.
#define URSA_PROFILE_FRAME(name)                                                                                       \
	do                                                                                                                 \
	{                                                                                                                  \
		FrameMark;                                                                                                     \
		FrameMarkNamed(name);                                                                                          \
		URSA_PROFILE_SAMPLE_MEMORY();                                                                                  \
	} while (false)
#define URSA_PROFILE_SCOPE(name) ZoneScopedN(name)
#define URSA_PROFILE_SCOPE_DYNAMIC(name, length) URSA_PROFILE_DYNAMIC_SCOPE_IMPL(name, length, __LINE__)
#define URSA_PROFILE_MESSAGE(text) TracyMessage(text.data(), text.size())
#else
#define URSA_PROFILE_FRAME(name) ((void)sizeof(name))
#define URSA_PROFILE_SCOPE(name) ((void)sizeof(name))
#define URSA_PROFILE_SCOPE_DYNAMIC(name, length) ((void)sizeof(name), (void)sizeof(length))
#define URSA_PROFILE_MESSAGE(text) ((void)sizeof(text))
#endif

#if defined(URSA_ENABLE_TRACY_MEMORY)
#define URSA_PROFILE_MEMORY_ALLOC(pointer, size, pool) ::Ursa::Profiling::MemoryAllocation(pointer, size, pool)
#define URSA_PROFILE_MEMORY_FREE(pointer, pool) ::Ursa::Profiling::MemoryRelease(pointer, pool)
#else
#define URSA_PROFILE_MEMORY_ALLOC(pointer, size, pool) ((void)0)
#define URSA_PROFILE_MEMORY_FREE(pointer, pool) ((void)0)
#endif

#if defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
#define URSA_PROFILE_PLOT(name, value, unit) ::Ursa::Profiling::Plot(name, value, unit)
#define URSA_PROFILE_EVENT(text, length) ::Ursa::Profiling::Event(text, length)
#define URSA_PROFILE_OPERATION(name, path, result) ::Ursa::Profiling::Operation(name, path, result)
#define URSA_PROFILE_APP(name) ::Ursa::Profiling::ApplicationInfo(name)
#define URSA_PROFILE_SAMPLE_MEMORY() ::Ursa::Profiling::SampleCpuMemory()
#define URSA_PROFILE_LOCK(type, variable, name) TracyLockableN(type, variable, name)
#define URSA_PROFILE_DIAGNOSTIC_SCOPE(name) ZoneScopedN(name)
#else
#define URSA_PROFILE_PLOT(name, value, unit) ((void)0)
#define URSA_PROFILE_EVENT(text, length) ((void)0)
#define URSA_PROFILE_OPERATION(name, path, result) ((void)0)
#define URSA_PROFILE_APP(name) ((void)0)
#define URSA_PROFILE_SAMPLE_MEMORY() ((void)0)
#define URSA_PROFILE_LOCK(type, variable, name) type variable
#define URSA_PROFILE_DIAGNOSTIC_SCOPE(name) ((void)0)
#endif

#if defined(URSA_ENABLE_TRACY_MEMORY) || defined(URSA_ENABLE_TRACY_DIAGNOSTICS)
#define URSA_PROFILE_CATEGORY(index, name) ::Ursa::Profiling::Detail::RegisterCpuCategory(index, name)
#else
#define URSA_PROFILE_CATEGORY(index, name) ((void)0)
#endif
