using System;
using System.IO;

namespace Ursa.DeepProfiler;

internal static class Program
{
	private static int Main(string[] args)
	{
		try
		{
			if (args.Length == 2 && args[0] == "--verify-publish")
			{
				Weaver.ValidateCompiledOutRuntime(args[1]);
				Console.WriteLine("Compiled-out marker layout and IL verified.");
				return 0;
			}
			if (args.Length != 4)
				throw new ArgumentException("Usage: Ursa.DeepProfiler <input.dll> <output.dll> <Ursa.Runtime.dll> <Debug|Release>");
			int methods = Weaver.Instrument(args[0], args[1], args[2], args[3]);
			Console.WriteLine($"Deep Profiling: instrumented {methods} methods in {Path.GetFileName(args[0])}.");
			return 0;
		}
		catch (Exception error)
		{
			Console.Error.WriteLine("Deep Profiling failed: " + error.Message);
			return 1;
		}
	}
}
