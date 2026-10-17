#include <Fixture.generated.hpp>
#include <Dependent.generated.hpp>
#include <Scoped.generated.hpp>
#include <Fixture.Commands.generated.hpp>

static_assert(Ursa::Scripting::Bindings::Wire::Enum_BindingFixture_WideFlags::V_Maximum ==
              std::numeric_limits<Ursa::UInt64>::max());
static_assert(Ursa::Scripting::Bindings::Wire::Enum_BindingFixture_SignedFlags::V_Minimum ==
              std::numeric_limits<Ursa::Int64>::min());

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <hostfxr.h>
#include <nethost.h>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define URSA_BINDING_FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define URSA_BINDING_FIXTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace B = Ursa::Scripting::Bindings;
namespace F = Ursa::Tests::BindingFixture;

namespace
{
F::Context& Context()
{
	static F::Context context;
	return context;
}
} // namespace

URSA_BINDING_FIXTURE_EXPORT const B::BindingModuleDescriptor* GetModule()
{
	return &B::Fixture::NativeDescriptor();
}

URSA_BINDING_FIXTURE_EXPORT const B::BindingModuleDescriptor* GetModuleById(Ursa::UInt64 id)
{
	if (id == B::Fixture::moduleId)
	{
		return &B::Fixture::NativeDescriptor();
	}
	if (id == B::Dependent::moduleId)
	{
		return &B::Dependent::NativeDescriptor();
	}
	if (id == B::Scoped::moduleId)
	{
		return &B::Scoped::NativeDescriptor();
	}
	return nullptr;
}

URSA_BINDING_FIXTURE_EXPORT void* GetContext()
{
	return Context().Token();
}

URSA_BINDING_FIXTURE_EXPORT void* GetClosedContext()
{
	F::Context context;
	return context.Token();
}

URSA_BINDING_FIXTURE_EXPORT Ursa::UInt64 LiveResultAllocations()
{
	return Ursa::Memory::Snapshot().categories[Ursa::Size(Ursa::Memory::MemoryTag::Temporary)].liveAllocations;
}

URSA_BINDING_FIXTURE_EXPORT void SetResultBudget(Ursa::UInt64 budget)
{
	Context().budget = budget;
}

URSA_BINDING_FIXTURE_EXPORT Ursa::Int32 RunCommandBatch(const Ursa::UInt8* bytes, Ursa::UInt64 size,
                                                        Ursa::UInt32 validateOnly)
{
	try
	{
		namespace C = B::Fixture::Commands;
		B::BindingArena arena(validateOnly == 2 ? 1 : B::maximumBytes);
		const auto commands = C::Decode(bytes, Ursa::Size(size), arena);
		if (validateOnly)
		{
			return 0;
		}
		if (commands.size() != 1 || !std::holds_alternative<C::FixtureCommands_Capture>(commands[0]))
		{
			return 2;
		}
		const auto& value = std::get<C::FixtureCommands_Capture>(commands[0]);
		const auto* points = static_cast<const B::Wire::Value_BindingFixture_Point*>(value.F_packet.F_Values.data);
		const auto* integers = static_cast<const Ursa::Int32*>(value.F_values.data);
		if (value.F_packet.F_Identity != std::numeric_limits<Ursa::UInt64>::max() ||
		    B::ReadString(value.F_packet.F_Name) != Ursa::String("接口\0data", 11) ||
		    value.F_packet.F_Values.length != 2 || points[0].F_X != 1.25f || points[1].F_Y != -7.125 ||
		    points[0].F_Enabled != 1 || value.F_values.length != 3 || integers[0] != -7 || integers[2] != 9 ||
		    value.F_matrix.F_M03 != 11)
		{
			return 3;
		}
		B::BindingCommandWriter writer(C::hash);
		C::Record_FixtureCommands_Capture(writer, value);
		const auto& encoded = writer.Bytes();
		return encoded.size() == size && std::memcmp(encoded.data(), bytes, Ursa::Size(size)) == 0 ? 0 : 4;
	}
	catch (const std::exception&)
	{
		return 1;
	}
}

URSA_BINDING_FIXTURE_EXPORT Ursa::Int32 CheckNativeOnlyCommand()
{
	try
	{
		namespace C = B::Fixture::Commands;
		B::BindingCommandWriter writer(C::hash);
		C::Record_FixtureCommands_Reset(writer, {});
		const auto& bytes = writer.Bytes();
		B::BindingArena arena;
		if (C::Decode(bytes.data(), bytes.size(), arena, true).size() != 1)
		{
			return 2;
		}
		try
		{
			C::Decode(bytes.data(), bytes.size(), arena);
			return 3;
		}
		catch (const std::exception&)
		{
			return 0;
		}
	}
	catch (const std::exception&)
	{
		return 1;
	}
}

URSA_BINDING_FIXTURE_EXPORT void MakeMalformedResult(B::BindingOutcome* outcome)
{
	*outcome = {};
	B::BindingArena arena;
	auto* byte = static_cast<Ursa::UInt8*>(arena.Allocate(1, 1));
	*byte = 0xff;
	outcome->error = {byte, 1, 1, 0};
	arena.Transfer(*outcome);
}

URSA_BINDING_FIXTURE_EXPORT Ursa::Int32 RunCallback(const B::BindingModuleDescriptor* descriptor, void* context,
                                                    Ursa::UInt32 fail)
{
	if (!descriptor || !descriptor->functions ||
	    std::memcmp(descriptor->hash, B::Fixture::NativeDescriptor().hash, 32) != 0)
	{
		return 9;
	}
	const auto* functions = static_cast<const B::Fixture::Table*>(descriptor->functions);
	B::BindingOutcome outcome;
	Ursa::Int32 status;
	if (fail)
	{
		status = functions->FixtureBindings_ManagedFail(context, &outcome);
		if (status != 0 &&
		    (B::ReadString(outcome.error, true).find("managed fixture error") == Ursa::String::npos ||
		     B::ReadString(outcome.error, true).find("session 17") == Ursa::String::npos ||
		     B::ReadString(outcome.error, true).find("BindingFixture.Callbacks.Fail") == Ursa::String::npos))
		{
			status = 7;
		}
	}
	else
	{
		F::Point input;
		input.F_X = 1.25f;
		input.F_Y = -3.5;
		input.F_Enabled = 1;
		F::Point output;
		status = functions->FixtureBindings_ManagedScale(context, &input, &output, &outcome);
		if (!status && (output.F_X != 2.5f || output.F_Y != -7 || output.F_Enabled != 1))
		{
			status = 8;
		}
	}
	B::Release(outcome);
	return status;
}

URSA_BINDING_FIXTURE_EXPORT Ursa::Int32 RunDelayedCallback(const B::BindingModuleDescriptor* descriptor, void* context)
{
	const auto* functions = static_cast<const B::Fixture::Table*>(descriptor->functions);
	Ursa::Int32 source[]{4, 7, 9};
	B::DataView input{source, 3, 1, 0};
	B::BindingOutcome outcome;
	auto status = functions->FixtureBindings_ManagedCapture(context, &input, &outcome);
	B::Release(outcome);
	if (status)
	{
		return status;
	}
	source[0] = 100;
	B::DataView output;
	status = functions->FixtureBindings_ManagedReadCaptured(context, &output, &outcome);
	if (!status)
	{
		const auto* values = static_cast<const Ursa::Int32*>(output.data);
		if (output.length != 3 || !values || values[0] != 4 || values[1] != 7 || values[2] != 9)
		{
			status = 8;
		}
	}
	B::Release(outcome);
	return status;
}

int wmain(int count, wchar_t** arguments)
{
	if (count != 5)
	{
		std::cerr << "Expected managed test DLL, generator DLL, contracts and scratch directory\n";
		return 1;
	}
	size_t length = 0;
	get_hostfxr_path(nullptr, &length, nullptr);
	std::vector<wchar_t> hostPath(length);
	if (!length || get_hostfxr_path(hostPath.data(), &length, nullptr) != 0)
	{
		std::cerr << "Cannot find the CoreCLR host\n";
		return 1;
	}
	HMODULE library = LoadLibraryW(hostPath.data());
	if (!library)
	{
		return 1;
	}
	auto initialize = reinterpret_cast<hostfxr_initialize_for_dotnet_command_line_fn>(
	    GetProcAddress(library, "hostfxr_initialize_for_dotnet_command_line"));
	auto run = reinterpret_cast<hostfxr_run_app_fn>(GetProcAddress(library, "hostfxr_run_app"));
	auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(library, "hostfxr_close"));
	int result = 1;
	if (initialize && run && close)
	{
		const wchar_t* managedArguments[]{arguments[1], L"--host-executable", arguments[2], arguments[3], arguments[4]};
		hostfxr_handle handle = nullptr;
		result = initialize(5, managedArguments, nullptr, &handle);
		if (result == 0)
		{
			result = run(handle);
		}
		if (handle)
		{
			close(handle);
		}
	}
	FreeLibrary(library);
	return result;
}
