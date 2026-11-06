using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct GuiEventData
{
	public ulong Token;
	public uint Type;
	public uint RawType;
	public Vector2 MousePosition;
	public Vector2 Delta;
	public int Button;
	public int ClickCount;
	public uint KeyCode;
	public uint Modifiers;
	public string Text;
	public string CommandName;
}

[BindingModule("Runtime", Required = true)]
[NativeHeader("Scripting/Bindings/GuiServices.hpp")]
internal static partial class NativeGuiBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::GetEvent")]
	internal static partial GuiEventData GetEvent(ulong token);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::Use")]
	internal static partial void Use(ulong token);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::GetTypeForControl")]
	internal static partial uint GetTypeForControl(ulong token, int id);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::GetControlId")]
	internal static partial int GetControlId(int hint, uint focus, Rect rect);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::GetHotControl")]
	internal static partial int GetHotControl();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::SetHotControl")]
	internal static partial void SetHotControl(int id);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::GetKeyboardControl")]
	internal static partial int GetKeyboardControl();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::SetKeyboardControl")]
	internal static partial void SetKeyboardControl(int id);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::GetChanged")]
	internal static partial bool GetChanged();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::SetChanged")]
	internal static partial void SetChanged(bool changed);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::Exit")]
	internal static partial void Exit();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::ViewportSize")]
	internal static partial Vector2 ViewportSize();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::PushId")]
	internal static partial void PushId(int id);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::PopId")]
	internal static partial void PopId();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::BeginGroup")]
	internal static partial void BeginGroup(Rect rect);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::EndGroup")]
	internal static partial void EndGroup();
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::Label")]
	internal static partial void Label(Rect rect, string text);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::Button")]
	internal static partial bool Button(Rect rect, string text);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::Toggle")]
	internal static partial bool Toggle(Rect rect, bool value, string text);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::HorizontalSlider")]
	internal static partial float HorizontalSlider(Rect rect, float value, float minimum, float maximum);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::TextField")]
	internal static partial string TextField(Rect rect, string value);
	[NativeMethod("Ursa::Scripting::Bindings::GuiServices::DrawTexture")]
	internal static partial void DrawTexture(Rect rect, [HandleKind(3)] NativeHandle texture);
}
