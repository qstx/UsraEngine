#pragma once

#include <Scripting/ManagedHandleRegistry.hpp>

namespace Ursa::Scripting::Bindings
{
template <class T> ManagedHandle ReadManagedHandle(const T& value)
{
	return {value.F_sessionId, value.F_index, value.F_generation, static_cast<ManagedHandleKind>(value.F_kind),
	        value.F_reserved};
}

template <class T> T WriteManagedHandle(const ManagedHandle& value)
{
	return {value.sessionId, value.index, value.generation, static_cast<UInt32>(value.kind), value.reserved};
}
} // namespace Ursa::Scripting::Bindings
