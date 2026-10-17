#pragma once

#include <BindingTypes.generated.hpp>
#include <Serialization/Archive.hpp>

namespace Ursa::Scripting::Bindings
{
struct SerializationServices
{
	using Handle = Wire::Value_Ursa_NativeHandle;
	using Archive = Wire::Value_Ursa_ResourceReferenceArchive;
	static Result<Archive> SnapshotMesh(BindingCall& call, const Handle& mesh);
	static Result<Archive> SnapshotMaterial(BindingCall& call, const Handle& material);
	static Result<Handle> RestoreMesh(BindingCall& call, const Archive& archive);
	static Result<Handle> RestoreMaterial(BindingCall& call, const Archive& archive);
	static Result<Wire::Value_Ursa_ScriptableAssetGraphArchive>
	LoadScriptableAssetGraph(BindingCall& call, const Wire::Value_Ursa_NativeAssetReference& root);
};
} // namespace Ursa::Scripting::Bindings
