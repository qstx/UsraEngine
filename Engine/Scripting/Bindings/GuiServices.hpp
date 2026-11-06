#pragma once

#include <BindingTypes.generated.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Gui/GuiSession.hpp>

namespace Ursa::Scripting::Bindings
{
struct GuiServices
{
	using Rect = Wire::Value_Ursa_Rect;
	using Point = Wire::Value_Ursa_Vector2;
	using Handle = Wire::Value_Ursa_NativeHandle;
	using Event = Wire::Value_Ursa_GuiEventData;
	static Gui::Session& Api(BindingCall& call);
	static Result<Event> GetEvent(BindingCall& call, UInt64 token);
	static Result<void> Use(BindingCall& call, UInt64 token);
	static Result<UInt32> GetTypeForControl(BindingCall& call, UInt64 token, Int32 id);
	static Result<Int32> GetControlId(BindingCall& call, Int32 hint, UInt32 focus, const Rect& rect);
	static Result<Int32> GetHotControl(BindingCall& call);
	static Result<void> SetHotControl(BindingCall& call, Int32 id);
	static Result<Int32> GetKeyboardControl(BindingCall& call);
	static Result<void> SetKeyboardControl(BindingCall& call, Int32 id);
	static Result<bool> GetChanged(BindingCall& call);
	static Result<void> SetChanged(BindingCall& call, bool changed);
	static Result<void> Exit(BindingCall& call);
	static Result<Point> ViewportSize(BindingCall& call);
	static Result<void> PushId(BindingCall& call, Int32 id);
	static Result<void> PopId(BindingCall& call);
	static Result<void> BeginGroup(BindingCall& call, const Rect& rect);
	static Result<void> EndGroup(BindingCall& call);
	static Result<void> Label(BindingCall& call, const Rect& rect, const Utf8View& text);
	static Result<bool> Button(BindingCall& call, const Rect& rect, const Utf8View& text);
	static Result<bool> Toggle(BindingCall& call, const Rect& rect, bool value, const Utf8View& text);
	static Result<Float32> HorizontalSlider(BindingCall& call, const Rect& rect, Float32 value, Float32 minimum,
	                                        Float32 maximum);
	static Result<Utf8View> TextField(BindingCall& call, const Rect& rect, const Utf8View& value);
	static Result<void> DrawTexture(BindingCall& call, const Rect& rect, const Handle& texture);
};
} // namespace Ursa::Scripting::Bindings
