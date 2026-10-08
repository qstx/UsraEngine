#pragma once

#include <Core/Types.hpp>

namespace Ursa::Scripting
{
struct ManagedEditorApi
{
	UInt32 version = 1;
	UInt32 size = 0;
	void* context = nullptr;
	// One scoped UTF-8 request/response exchange; no window or renderer pointers cross the ABI.
	Int32 (*exchange)(void*, const UInt8*, UInt64, UInt8*, UInt64, UInt64*) = nullptr;
};

static_assert(sizeof(ManagedEditorApi) == 24, "ManagedEditorApi layout changed");
} // namespace Ursa::Scripting
