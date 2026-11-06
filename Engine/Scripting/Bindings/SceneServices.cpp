#include <Scripting/Bindings/SceneServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Core/Log.hpp>

namespace Ursa::Scripting::Bindings
{
RuntimeSceneManager& SceneServices::Api(BindingCall& call)
{
	auto& api = RuntimeServices::SceneApi(call);
	api.RequireGameExecution();
	if (!api.sceneManager)
	{
		throw std::logic_error("SceneManager requires an active Player or Run session");
	}
	return *api.sceneManager;
}

SceneServices::Data SceneServices::Write(const RuntimeScene& scene, UInt64 session, BindingArena& arena)
{
	Data result{};
	result.F_Session = session;
	result.F_Token = scene.token;
	result.F_Guid = arena.StringValue(scene.asset.reference.guid.ToString());
	result.F_LocalFileId = scene.asset.reference.localFileId;
	result.F_Name = arena.StringValue(scene.asset.name);
	result.F_Path = arena.StringValue(scene.asset.path.relative);
	result.F_BuildIndex = scene.asset.buildIndex;
	return result;
}

Result<SceneServices::Data> SceneServices::GetActiveScene(BindingCall& call)
{
	return Result<Data>::Success(
	    Write(Api(call).ActiveScene(), RuntimeServices::SceneApi(call).SessionIdentity(), call.arena));
}

Result<bool> SceneServices::IsSceneLoaded(BindingCall& call, UInt64 session, UInt64 token)
{
	return Result<bool>::Success(session == RuntimeServices::SceneApi(call).SessionIdentity() &&
	                             Api(call).IsLoaded(token));
}

Result<bool> SceneServices::GetIsLoading(BindingCall& call)
{
	return Result<bool>::Success(Api(call).IsLoading());
}

Result<Utf8View> SceneServices::GetLastLoadError(BindingCall& call)
{
	return Result<Utf8View>::Success(call.arena.StringValue(Api(call).LastLoadError()));
}

Result<void> SceneServices::LoadByName(BindingCall& call, const Utf8View& nameOrPath, UInt32 mode)
{
	auto& api = Api(call);
	auto result = api.Load(ReadString(nameOrPath), mode);
	if (!api.LastDiagnostic().empty())
	{
		URSA_LOG_WARN("Scene", api.LastDiagnostic());
	}
	return result;
}

Result<void> SceneServices::LoadByIndex(BindingCall& call, Int32 index, UInt32 mode)
{
	return Api(call).Load(index, mode);
}

Result<void> SceneServices::LoadByReference(BindingCall& call, const Utf8View& guid, UInt64 localFileId, UInt32 mode)
{
	auto id = ResourceId::Parse(ReadString(guid));
	return id ? Api(call).Load(AssetReference{id.value, localFileId}, mode) : Result<void>::Failure(id.error);
}
} // namespace Ursa::Scripting::Bindings
