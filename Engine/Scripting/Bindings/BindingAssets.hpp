#pragma once

#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Serialization/AssetReference.hpp>

namespace Ursa::Scripting::Bindings
{
template <class W> AssetReference ReadAssetValue(const W& value)
{
	return {TakeBindingResult(ResourceId::Parse(ReadString(value.F_Guid))), value.F_LocalFileId};
}

template <class W> W WriteAssetValue(BindingArena& arena, const AssetReference& value)
{
	W result;
	result.F_Guid = arena.StringValue(value.guid.ToString());
	result.F_LocalFileId = value.localFileId;
	return result;
}

template <class W> W WriteOptionalAssetValue(BindingArena& arena, const AssetReference& value)
{
	W result;
	if (value.guid)
	{
		result.present = 1;
		result.F_value = WriteAssetValue<decltype(result.F_value)>(arena, value);
	}
	return result;
}
} // namespace Ursa::Scripting::Bindings
