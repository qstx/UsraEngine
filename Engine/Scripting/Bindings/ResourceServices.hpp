#pragma once

#include <BindingTypes.generated.hpp>

namespace Ursa::Scripting::Bindings
{
struct ResourceServices
{
	using Handle = Wire::Value_Ursa_NativeHandle;
	using OptionalHandle = Wire::Value_Ursa_NativeHandle_;
	using Asset = Wire::Value_Ursa_NativeAssetReference;
	static Result<Handle> LoadTexture(BindingCall& call, const Asset& reference);
	static Result<bool> IsValid(BindingCall& call, const Handle& resource);
	static Result<OptionalHandle> GetMeshBinding(BindingCall& call, const Handle& component, bool instance);
	static Result<void> SetMeshBinding(BindingCall& call, const Handle& component, const OptionalHandle& mesh,
	                                   bool instance);
	static Result<DataView> GetMaterialBindings(BindingCall& call, const Handle& component, bool instance, bool first);
	static Result<void> SetMaterialBindings(BindingCall& call, const Handle& component, const DataView& materials,
	                                        bool shared, bool first);
	static Result<Int32> FindComputeKernel(BindingCall& call, const Asset& reference, const Utf8View& name);
	static Result<Wire::Value_Ursa_ComputeThreadGroupSize>
	GetComputeThreadGroupSize(BindingCall& call, const Asset& reference, Int32 kernel);
};
} // namespace Ursa::Scripting::Bindings
