#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/ManagedSceneApi.hpp>

namespace Ursa::Scripting::Bindings
{
BindingHostAccess RuntimeServices::HostApi(BindingCall& call)
{
	void* token =
	    dynamic_cast<BindingHostServices*>(&call.context) ? call.context.Token() : SceneApi(call).bindingHostToken;
	if (!token)
	{
		throw std::runtime_error("Runtime host services have not been connected");
	}
	return BindingHostAccess(token);
}

Result<bool> RuntimeServices::GetKey(BindingCall& call, UInt32 key, UInt32 phase)
{
	const auto& api = SceneApi(call);
	if (!api.frameContext || key >= FrameInput::keyCount || phase > 2)
	{
		return Result<bool>::Failure("Input requires an active frame, supported key and phase");
	}
	const auto keyCode = static_cast<Key>(key);
	if (keyCode == Key::Unknown)
	{
		return Result<bool>::Success(false);
	}
	const auto& input = api.frameContext->input;
	return Result<bool>::Success(phase == 0   ? input.KeyDown(keyCode)
	                             : phase == 1 ? input.KeyPressed(keyCode)
	                                          : input.KeyReleased(keyCode));
}

Result<Wire::Value_Ursa_TimeState> RuntimeServices::GetTime(BindingCall& call)
{
	const auto* frame = SceneApi(call).frameContext;
	if (!frame)
	{
		return Result<Wire::Value_Ursa_TimeState>::Failure("Time requires an active frame");
	}
	return Result<Wire::Value_Ursa_TimeState>::Success({frame->deltaSeconds, frame->fixedDeltaSeconds,
	                                                    frame->elapsedSeconds, frame->fixedElapsedSeconds,
	                                                    frame->frameIndex});
}

Result<void> RuntimeServices::WriteLog(BindingCall& call, UInt32 level, const Utf8View& message)
{
	if (level > 5)
	{
		return Result<void>::Failure("Unknown Script log level");
	}
	auto host = HostApi(call);
	return host.Services().WriteScriptLog(level, ReadString(message));
}

Result<void> RuntimeServices::ReportHostMessage(BindingCall& call, UInt32 level, const Utf8View& message)
{
	auto host = HostApi(call);
	return host.Services().ReportHostMessage(level, ReadString(message));
}

Result<DataView> RuntimeServices::ReadFile(BindingCall& call, const Utf8View& path)
{
	if (path.length == 0 || path.length > 4096)
	{
		return Result<DataView>::Failure("Invalid VFS path length");
	}
	auto host = HostApi(call);
	const auto bytes = TakeBindingResult(host.Services().ReadScriptFile(ReadString(path)));
	if (bytes.size() > 256ull * 1024 * 1024)
	{
		return Result<DataView>::Failure("VFS file exceeds the transfer budget");
	}
	return Result<DataView>::Success(call.arena.Array(bytes.data(), bytes.size()));
}

Result<void> RuntimeServices::WriteFile(BindingCall& call, const Utf8View& path, const DataView& bytes)
{
	if (path.length == 0 || path.length > 4096 || bytes.length > 256ull * 1024 * 1024)
	{
		return Result<void>::Failure("VFS path or file exceeds the transfer budget");
	}
	auto host = HostApi(call);
	Vector<UInt8> content;
	if (bytes.length)
	{
		const auto* data = static_cast<const UInt8*>(bytes.data);
		content.assign(data, data + Size(bytes.length));
	}
	return host.Services().WriteScriptFile(ReadString(path), content);
}

Result<void> RuntimeServices::SetWindowTitle(BindingCall& call, const Utf8View& title)
{
	if (title.length > 1024)
	{
		return Result<void>::Failure("Window title exceeds 1024 UTF-8 bytes");
	}
	auto& api = SceneApi(call);
	const auto text = ReadString(title);
	if (text.find('\0') != String::npos || !api.setWindowTitle)
	{
		return Result<void>::Failure("Window title requires a window callback and no embedded zero");
	}
	api.setWindowTitle(text);
	return {};
}
} // namespace Ursa::Scripting::Bindings
