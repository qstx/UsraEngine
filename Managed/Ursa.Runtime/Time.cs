namespace Ursa;

public static unsafe class Time
{
	public static float DeltaTime
	{
		get
		{
			float delta = 0;
			float fixedDelta = 0;
			double time = 0;
			double fixedTime = 0;
			ulong frame = 0;
			EngineApi.GetTime(&delta, &fixedDelta, &time, &fixedTime, &frame);
			return delta;
		}
	}

	public static float FixedDeltaTime
	{
		get
		{
			float delta = 0;
			float fixedDelta = 0;
			double time = 0;
			double fixedTime = 0;
			ulong frame = 0;
			EngineApi.GetTime(&delta, &fixedDelta, &time, &fixedTime, &frame);
			return fixedDelta;
		}
	}

	public static double ElapsedTime
	{
		get
		{
			float delta = 0;
			float fixedDelta = 0;
			double time = 0;
			double fixedTime = 0;
			ulong frame = 0;
			EngineApi.GetTime(&delta, &fixedDelta, &time, &fixedTime, &frame);
			return time;
		}
	}

	public static double FixedElapsedTime
	{
		get
		{
			float delta = 0;
			float fixedDelta = 0;
			double time = 0;
			double fixedTime = 0;
			ulong frame = 0;
			EngineApi.GetTime(&delta, &fixedDelta, &time, &fixedTime, &frame);
			return fixedTime;
		}
	}

	public static ulong FrameCount
	{
		get
		{
			float delta = 0;
			float fixedDelta = 0;
			double time = 0;
			double fixedTime = 0;
			ulong frame = 0;
			EngineApi.GetTime(&delta, &fixedDelta, &time, &fixedTime, &frame);
			return frame;
		}
	}
}
