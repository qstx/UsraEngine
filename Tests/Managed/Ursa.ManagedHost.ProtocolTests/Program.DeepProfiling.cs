using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using Ursa.Bindings;
using Ursa.Profiling;

internal static unsafe partial class Program
{
	private static void TestDeepProfiling(string input, string tool, string configuration, string scratch)
	{
		string runtime = typeof(ProfilerMarker).Assembly.Location;
		Directory.CreateDirectory(scratch);
		string output = Path.Combine(scratch, "Woven", Path.GetFileName(input));
		int RunTool(string source, string destination, string selected, out string message)
		{
			var start = new ProcessStartInfo(Environment.ProcessPath!)
			{
				UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true,
				CreateNoWindow = true
			};
			foreach (string argument in new[] { tool, source, destination, runtime, selected }) start.ArgumentList.Add(argument);
			using var process = Process.Start(start)!;
			Task<string> errors = process.StandardError.ReadToEndAsync();
			string text = process.StandardOutput.ReadToEnd();
			process.WaitForExit();
			message = text + errors.GetAwaiter().GetResult();
			return process.ExitCode;
		}
#if !URSA_ENABLE_PROFILER
		Expect(RunTool(input, output, configuration, out string disabledError) != 0 && !File.Exists(output),
			"Disabled/Publish framework accepted Deep Profiling: " + disabledError);
#else
		byte[] original = SHA256.HashData(File.ReadAllBytes(input));
		Expect(RunTool(input, output, configuration, out string message) == 0, "IL instrumentation failed: " + message);
		Ursa.ManagedHost.ScriptDebugSymbols.Validate(File.ReadAllBytes(output), File.ReadAllBytes(Path.ChangeExtension(output, ".pdb")));
		using var registration = InstallRuntimeBindings();
		WeakReference context = ExecuteDeepProfiledAssembly(output);
		for (int attempt = 0; context.IsAlive && attempt < 20; ++attempt)
		{
			GC.Collect();
			GC.WaitForPendingFinalizers();
			GC.Collect();
			Thread.Sleep(10);
		}
		Expect(!context.IsAlive, "Profiler state kept the instrumented project context alive.");
		Expect(SpinWait.SpinUntil(() => cpuBegins == cpuEnds, 2000) && cpuBegins > 30,
			"Deep scope return/exception/state machine pairing failed.");
		Expect(cpuThreads.Count >= 2, "Async continuation was not profiled on its worker thread.");
		Expect(cpuMetadata.Any(name => name.Contains("Factorial", StringComparison.Ordinal)) &&
			cpuMetadata.Any(name => name.Contains("[MoveNext]", StringComparison.Ordinal)) &&
			cpuMetadata.Any(name => name.Contains("DeepProbe.cs", StringComparison.Ordinal)), "Method/state machine/source metadata missing.");
		Expect(!cpuMetadata.Any(name => name.Contains("Ignored(", StringComparison.Ordinal) ||
			name.Contains("IgnoredAsync", StringComparison.Ordinal) || name.Contains("IgnoredProbe", StringComparison.Ordinal)),
			"Ignored method, type or state machine was instrumented.");
		Expect(RunTool(output, Path.Combine(scratch, "Repeat", Path.GetFileName(input)), configuration, out message) != 0 &&
			message.Contains("already instrumented", StringComparison.Ordinal), "Repeated instrumentation was accepted.");
		Expect(RunTool(input, Path.Combine(scratch, "Publish", Path.GetFileName(input)), "Publish", out message) != 0 &&
			message.Contains("Publish", StringComparison.Ordinal), "Publish instrumentation was accepted.");
		Expect(original.SequenceEqual(SHA256.HashData(File.ReadAllBytes(input))), "Instrumentation modified the original compiler output.");
		string bad = Path.Combine(scratch, "Bad", Path.GetFileName(input));
		Directory.CreateDirectory(Path.GetDirectoryName(bad)!);
		File.Copy(input, bad, overwrite: true);
		File.WriteAllText(Path.ChangeExtension(bad, ".pdb"), "invalid PDB");
		byte[] saved = SHA256.HashData(File.ReadAllBytes(output));
		Expect(RunTool(bad, output, configuration, out message) != 0 && saved.SequenceEqual(SHA256.HashData(File.ReadAllBytes(output))),
			"Invalid symbols replaced a valid output assembly.");
		BindingRegistry.Clear();
#endif
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static WeakReference ExecuteDeepProfiledAssembly(string path)
	{
		var context = new AssemblyLoadContext("Ursa.DeepProfiling.Test", isCollectible: true);
		context.Resolving += (_, name) => name.Name == typeof(ProfilerMarker).Assembly.GetName().Name
			? typeof(ProfilerMarker).Assembly : null;
		try
		{
			using var image = File.OpenRead(path);
			using var symbols = File.OpenRead(Path.ChangeExtension(path, ".pdb"));
			Assembly assembly = context.LoadFromStream(image, symbols);
			Expect(assembly.IsDefined(typeof(DeepProfiledAttribute)), "Instrumented assembly has no version marker.");
			Type probe = assembly.GetType("Ursa.DeepProfilingFixtures.DeepProbe", throwOnError: true)!;
			Expect((int)probe.GetMethod("Run")!.Invoke(null, null)! == 56, "Instrumented synchronous behavior changed.");
			var task = (Task<int>)probe.GetMethod("Async")!.Invoke(null, new object[] { 20 })!;
			Expect(task.GetAwaiter().GetResult() == 21, "Async result changed.");
			var iterator = ((IEnumerable<int>)probe.GetMethod("Iterate")!.Invoke(null, null)!).GetEnumerator();
			using (iterator)
			{
				Expect(iterator.MoveNext() && iterator.Current == 1 && cpuScopes.Count == 0, "Iterator retained a scope across yield.");
				Expect(iterator.MoveNext() && iterator.Current == 2 && !iterator.MoveNext(), "Iterator behavior changed.");
			}
			Expect((int)probe.GetMethod("Ignored")!.Invoke(null, null)! == 12, "Ignored method changed.");
			Expect(((Task<int>)probe.GetMethod("IgnoredAsync")!.Invoke(null, null)!).GetAwaiter().GetResult() == 13,
				"Ignored async method changed.");
			Type generic = assembly.GetType("Ursa.DeepProfilingFixtures.GenericProbe`1")!.MakeGenericType(typeof(int));
			Expect((int)generic.GetMethod("Echo")!.Invoke(Activator.CreateInstance(generic), new object[] { 8 })! == 8,
				"Generic declaring type failed.");
			return new WeakReference(context);
		}
		finally { context.Unload(); }
	}
}
