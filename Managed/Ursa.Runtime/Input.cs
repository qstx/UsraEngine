namespace Ursa;

public enum KeyCode : uint
{
	Escape,
	Space,
	Left,
	Right,
	Up,
	Down,
	W,
	A,
	S,
	D,
	Unknown,
	Tab,
	Enter,
	Backspace,
	Delete,
	Home,
	End,
	PageUp,
	PageDown,
	Insert,
	LeftShift,
	RightShift,
	LeftControl,
	RightControl,
	LeftAlt,
	RightAlt,
	LeftSuper,
	RightSuper,
	B,
	C,
	E,
	F,
	G,
	H,
	I,
	J,
	K,
	L,
	M,
	N,
	O,
	P,
	Q,
	R,
	T,
	U,
	V,
	X,
	Y,
	Z,
	Digit0,
	Digit1,
	Digit2,
	Digit3,
	Digit4,
	Digit5,
	Digit6,
	Digit7,
	Digit8,
	Digit9
}

public static class Input
{
	public static bool GetKey(KeyCode key) => EngineApi.GetKey((uint)key, 0);

	public static bool GetKeyDown(KeyCode key) => EngineApi.GetKey((uint)key, 1);

	public static bool GetKeyUp(KeyCode key) => EngineApi.GetKey((uint)key, 2);
}
