using System;

namespace Ursa;

public enum EventType : uint
{
	Layout, Repaint, MouseDown, MouseUp, MouseMove, MouseDrag, ScrollWheel, KeyDown, KeyUp,
	ValidateCommand, ExecuteCommand, Used, Ignore
}

public enum FocusType : uint { Passive, Keyboard }

[Flags]
public enum EventModifiers : uint { None = 0, Shift = 1, Control = 2, Alt = 4, Command = 8 }

/// <summary>A wrapper for one OnGUI callback. Copy property values to retain an event snapshot.</summary>
public sealed class Event
{
	private readonly ulong token;
	private Event(ulong token) { this.token = token; }
	private GuiEventData Data => NativeGuiBindings.GetEvent(token);
	public static Event Current => new(NativeGuiBindings.GetEvent(0).Token);
	public EventType Type => (EventType)Data.Type;
	public EventType RawType => (EventType)Data.RawType;
	public Vector2 MousePosition => Data.MousePosition;
	public Vector2 Delta => Data.Delta;
	public int Button => Data.Button;
	public int ClickCount => Data.ClickCount;
	public KeyCode KeyCode => (KeyCode)Data.KeyCode;
	public EventModifiers Modifiers => (EventModifiers)Data.Modifiers;
	public string Text => Data.Text;
	public string CommandName => Data.CommandName;
	public void Use() => NativeGuiBindings.Use(token);
	public EventType GetTypeForControl(int controlId) => (EventType)NativeGuiBindings.GetTypeForControl(token, controlId);
}

/// <summary>Normal control flow that ends the current GUI event. Do not swallow this exception.</summary>
public sealed class ExitGUIException : Exception
{
	internal ExitGUIException() : base("The current GUI event has exited.") { }
}

public static class GUIUtility
{
	public static int GetControlId(int hint, FocusType focusType, Rect rect) => NativeGuiBindings.GetControlId(hint, (uint)focusType, rect);
	public static int HotControl { get => NativeGuiBindings.GetHotControl(); set => NativeGuiBindings.SetHotControl(value); }
	public static int KeyboardControl { get => NativeGuiBindings.GetKeyboardControl(); set => NativeGuiBindings.SetKeyboardControl(value); }
	public static void ExitGUI()
	{
		NativeGuiBindings.Exit();
		throw new ExitGUIException();
	}
}

/// <summary>Immediate mode controls in target pixels, with a top-left origin. Values change only on input events.</summary>
public static class GUI
{
	public static bool Changed { get => NativeGuiBindings.GetChanged(); set => NativeGuiBindings.SetChanged(value); }
	public static Vector2 ViewportSize => NativeGuiBindings.ViewportSize();
	public static void Label(Rect rect, string text) => NativeGuiBindings.Label(rect, text ?? string.Empty);
	public static bool Button(Rect rect, string text) => NativeGuiBindings.Button(rect, text ?? string.Empty);
	public static bool Toggle(Rect rect, bool value, string text) => NativeGuiBindings.Toggle(rect, value, text ?? string.Empty);
	public static float HorizontalSlider(Rect rect, float value, float minimum, float maximum) => NativeGuiBindings.HorizontalSlider(rect, value, minimum, maximum);
	public static string TextField(Rect rect, string value) => NativeGuiBindings.TextField(rect, value ?? string.Empty);
	public static void DrawTexture(Rect rect, Texture texture) => NativeGuiBindings.DrawTexture(rect, (texture ?? throw new ArgumentNullException(nameof(texture))).Handle);
	public static void BeginGroup(Rect rect) => NativeGuiBindings.BeginGroup(rect);
	public static void EndGroup() => NativeGuiBindings.EndGroup();
	public static void PushId(int id) => NativeGuiBindings.PushId(id);
	public static void PopId() => NativeGuiBindings.PopId();
}
