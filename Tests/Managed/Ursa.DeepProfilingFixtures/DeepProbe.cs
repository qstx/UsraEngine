using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Ursa.Profiling;

namespace Ursa.DeepProfilingFixtures;

public sealed class DeepProbe
{
	private static int cell;
	private readonly int value;
	public DeepProbe(int value) { this.value = value; }
	public int Value => value;
	public static int Factorial(int value) => value <= 1 ? 1 : value * Factorial(value - 1);
	public static T Echo<T>(T value) => value;
	public static int Branch(int value)
	{
		if (value < 0) return -1;
		if (value == 0) return 0;
		return value;
	}
	public static int ThrowAndCatch()
	{
		int result = 0;
		try { Fail(); }
		catch (InvalidOperationException error) when (Filter(error)) { result = 9; }
		finally { result++; }
		return result;
	}
	private static bool Filter(Exception error) => error.Message == "deep failure";
	public static void Fail() => throw new InvalidOperationException("deep failure");
	public static ref int Reference() => ref cell;
	public static int Run()
	{
		var instance = new DeepProbe(3);
		Reference() = 4;
		int Local(int number) => number + 1;
		Func<int, int> lambda = number => number * 2;
		return instance.Value + Factorial(4) + Echo(5) + Branch(-2) + Branch(0) + Branch(2) +
			ThrowAndCatch() + cell + Local(2) + lambda(3);
	}
	public static async Task<int> Async(int value)
	{
		await Task.Yield();
		return Echo(value + 1);
	}
	public static IEnumerable<int> Iterate()
	{
		try
		{
			yield return Echo(1);
			yield return Echo(2);
		}
		finally { cell++; }
	}
	[IgnoredByDeepProfiler]
	public static int Ignored() => 12;
	[IgnoredByDeepProfiler]
	public static async Task<int> IgnoredAsync() { await Task.Yield(); return 13; }
}

[IgnoredByDeepProfiler]
public sealed class IgnoredProbe
{
	public static int Run() => 14;
}

public sealed class GenericProbe<T>
{
	public T Echo(T value) => value;
}
