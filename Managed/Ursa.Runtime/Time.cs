namespace Ursa;

public static class Time
{
	public static float DeltaTime => NativeRuntimeBindings.GetTime().DeltaTime;
	public static float FixedDeltaTime => NativeRuntimeBindings.GetTime().FixedDeltaTime;
	public static double ElapsedTime => NativeRuntimeBindings.GetTime().ElapsedTime;
	public static double FixedElapsedTime => NativeRuntimeBindings.GetTime().FixedElapsedTime;
	public static ulong FrameCount => NativeRuntimeBindings.GetTime().FrameCount;
}
