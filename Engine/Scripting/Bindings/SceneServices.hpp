#pragma once

#include <BindingTypes.generated.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Application/RuntimeSceneManager.hpp>

namespace Ursa::Scripting::Bindings
{
struct SceneServices
{
	using Data = Wire::Value_Ursa_SceneManagement_SceneData;
	static RuntimeSceneManager& Api(BindingCall& call);
	static Data Write(const RuntimeScene& scene, UInt64 session, BindingArena& arena);
	static Result<Data> GetActiveScene(BindingCall& call);
	static Result<bool> IsSceneLoaded(BindingCall& call, UInt64 session, UInt64 token);
	static Result<bool> GetIsLoading(BindingCall& call);
	static Result<Utf8View> GetLastLoadError(BindingCall& call);
	static Result<void> LoadByName(BindingCall& call, const Utf8View& nameOrPath, UInt32 mode);
	static Result<void> LoadByIndex(BindingCall& call, Int32 index, UInt32 mode);
	static Result<void> LoadByReference(BindingCall& call, const Utf8View& guid, UInt64 localFileId, UInt32 mode);
};
} // namespace Ursa::Scripting::Bindings
