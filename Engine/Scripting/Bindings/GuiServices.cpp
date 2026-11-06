#include <Scripting/Bindings/GuiServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <stdexcept>

namespace Ursa::Scripting::Bindings
{
namespace
{
Gui::Rect ReadRect(const GuiServices::Rect& value)
{
	return {value.F_X, value.F_Y, value.F_Width, value.F_Height};
}
} // namespace

Gui::Session& GuiServices::Api(BindingCall& call)
{
	auto& api = RuntimeServices::SceneApi(call);
	api.RequireGameExecution();
	if (!api.frameContext || !api.frameContext->gui)
	{
		throw std::logic_error("GUI requires the current OnGUI callback");
	}
	api.frameContext->gui->CallbackToken();
	return *api.frameContext->gui;
}

Result<GuiServices::Event> GuiServices::GetEvent(BindingCall& call, UInt64 token)
{
	auto& api = Api(call);
	const auto& current = api.Current(token);
	Event result{};
	result.F_Token = api.CallbackToken();
	result.F_Type = static_cast<UInt32>(current.type);
	result.F_RawType = static_cast<UInt32>(current.rawType);
	const auto mouse = api.MousePosition();
	result.F_MousePosition = {mouse.x, mouse.y};
	result.F_Delta = {current.delta.x, current.delta.y};
	result.F_Button = current.button;
	result.F_ClickCount = current.clickCount;
	result.F_KeyCode = static_cast<UInt32>(current.key);
	result.F_Modifiers = current.modifiers;
	result.F_Text = call.arena.StringValue(current.text);
	result.F_CommandName = call.arena.StringValue(current.commandName);
	return Result<Event>::Success(result);
}

Result<void> GuiServices::Use(BindingCall& call, UInt64 token)
{
	Api(call).Use(token);
	return {};
}

Result<UInt32> GuiServices::GetTypeForControl(BindingCall& call, UInt64 token, Int32 id)
{
	return Result<UInt32>::Success(static_cast<UInt32>(Api(call).GetTypeForControl(id, token)));
}

Result<Int32> GuiServices::GetControlId(BindingCall& call, Int32 hint, UInt32 focus, const Rect& rect)
{
	return Result<Int32>::Success(Api(call).GetControlId(hint, static_cast<Gui::FocusType>(focus), ReadRect(rect)));
}

Result<Int32> GuiServices::GetHotControl(BindingCall& call)
{
	return Result<Int32>::Success(Api(call).HotControl());
}

Result<void> GuiServices::SetHotControl(BindingCall& call, Int32 id)
{
	Api(call).SetHotControl(id);
	return {};
}

Result<Int32> GuiServices::GetKeyboardControl(BindingCall& call)
{
	return Result<Int32>::Success(Api(call).KeyboardControl());
}

Result<void> GuiServices::SetKeyboardControl(BindingCall& call, Int32 id)
{
	Api(call).SetKeyboardControl(id);
	return {};
}

Result<bool> GuiServices::GetChanged(BindingCall& call)
{
	return Result<bool>::Success(Api(call).Changed());
}

Result<void> GuiServices::SetChanged(BindingCall& call, bool changed)
{
	Api(call).SetChanged(changed);
	return {};
}

Result<void> GuiServices::Exit(BindingCall& call)
{
	Api(call).Exit();
	return {};
}

Result<GuiServices::Point> GuiServices::ViewportSize(BindingCall& call)
{
	const auto size = Api(call).ViewportSize();
	return Result<Point>::Success({size.x, size.y});
}

Result<void> GuiServices::PushId(BindingCall& call, Int32 id)
{
	Api(call).PushId(id);
	return {};
}

Result<void> GuiServices::PopId(BindingCall& call)
{
	Api(call).PopId();
	return {};
}

Result<void> GuiServices::BeginGroup(BindingCall& call, const Rect& rect)
{
	Api(call).BeginGroup(ReadRect(rect));
	return {};
}

Result<void> GuiServices::EndGroup(BindingCall& call)
{
	Api(call).EndGroup();
	return {};
}

Result<void> GuiServices::Label(BindingCall& call, const Rect& rect, const Utf8View& text)
{
	Api(call).Label(ReadRect(rect), ReadString(text));
	return {};
}

Result<bool> GuiServices::Button(BindingCall& call, const Rect& rect, const Utf8View& text)
{
	return Result<bool>::Success(Api(call).Button(ReadRect(rect), ReadString(text)));
}

Result<bool> GuiServices::Toggle(BindingCall& call, const Rect& rect, bool value, const Utf8View& text)
{
	return Result<bool>::Success(Api(call).Toggle(ReadRect(rect), value, ReadString(text)));
}

Result<Float32> GuiServices::HorizontalSlider(BindingCall& call, const Rect& rect, Float32 value, Float32 minimum,
                                              Float32 maximum)
{
	return Result<Float32>::Success(Api(call).HorizontalSlider(ReadRect(rect), value, minimum, maximum));
}

Result<Utf8View> GuiServices::TextField(BindingCall& call, const Rect& rect, const Utf8View& value)
{
	return Result<Utf8View>::Success(call.arena.StringValue(Api(call).TextField(ReadRect(rect), ReadString(value))));
}

Result<void> GuiServices::DrawTexture(BindingCall& call, const Rect& rect, const Handle& texture)
{
	auto image = RuntimeServices::SceneApi(call).ReadTexture(ReadManagedHandle(texture));
	if (!image)
	{
		return Result<void>::Failure(image.error);
	}
	Api(call).DrawTexture(ReadRect(rect), std::move(image.value));
	return {};
}
} // namespace Ursa::Scripting::Bindings
