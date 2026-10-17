using System;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Ursa.Bindings;
using Ursa.Bindings.Generated;
using Ursa;
using CommandCodec = Ursa.Bindings.Generated.FixtureCommands;

namespace BindingFixture;

internal static class Callbacks
{
	private static int[] captured = Array.Empty<int>();
	internal static void Capture(int[] values) => captured = values;
	internal static int[] ReadCaptured() => captured;
	internal static Point Scale(Point value) => new() { X = value.X * 2, Y = value.Y * 2, Enabled = value.Enabled };
	internal static void Fail() => throw new InvalidOperationException("managed fixture error");
}

internal static unsafe class Program
{
	private static void Expect(bool condition, string message)
	{
		if (!condition) throw new InvalidOperationException(message);
	}

	private static void Reject(Action action, string text)
	{
		try { action(); }
		catch (Exception error) when (error.Message.Contains(text, StringComparison.Ordinal)) { return; }
		throw new InvalidOperationException("Expected rejection: " + text);
	}

	private static int Main(string[] args)
	{
		try
		{
			if (args.Length != 4) throw new ArgumentException("Expected native DLL, generator DLL, contract root, scratch root.");
			GeneratorTests(args[1], args[2], args[3]);
			Console.WriteLine("Generator diagnostics and deterministic publication verified.");
			if (args[0] == "--generator-only")
			{
				Console.WriteLine("Bindings generator diagnostic/determinism tests passed.");
				return 0;
			}
			bool hosted = args[0] == "--host-executable";
			nint library = hosted ? NativeLibrary.GetMainProgramHandle() : NativeLibrary.Load(Path.GetFullPath(args[0]));
			try
			{
				var getModule = (delegate* unmanaged[Cdecl]<nint>)NativeLibrary.GetExport(library, "GetModule");
				var getContext = (delegate* unmanaged[Cdecl]<nint>)NativeLibrary.GetExport(library, "GetContext");
				var getModuleById = (delegate* unmanaged[Cdecl]<ulong, nint>)NativeLibrary.GetExport(library, "GetModuleById");
				var getClosedContext = (delegate* unmanaged[Cdecl]<nint>)NativeLibrary.GetExport(library, "GetClosedContext");
				var runCallback = (delegate* unmanaged[Cdecl]<nint, nint, uint, int>)NativeLibrary.GetExport(library, "RunCallback");
				var liveResults = (delegate* unmanaged[Cdecl]<ulong>)NativeLibrary.GetExport(library, "LiveResultAllocations");
				var setBudget = (delegate* unmanaged[Cdecl]<ulong, void>)NativeLibrary.GetExport(library, "SetResultBudget");
				var malformedResult = (delegate* unmanaged[Cdecl]<BindingOutcome*, void>)NativeLibrary.GetExport(library, "MakeMalformedResult");
				var delayedCallback = (delegate* unmanaged[Cdecl]<nint, nint, int>)NativeLibrary.GetExport(library, "RunDelayedCallback");
				nint module = getModule(), context = getContext();
				FixtureModule.Bind(module, context);
				nint closedContext = getClosedContext();
				Reject(() => FixtureModule.Bind(module, closedContext), "closed or invalid");
				Matrix4x4 matrix = new(new float[] { 2, 7, 23, 0, 3, 13, 29, 0, 5, 17, 31, 0, 11, 19, 37, 1 });
				Matrix4x4 translation = Matrix4x4.Translation(new Vector3(2, -3, 1));
				Matrix4x4 nativeProduct = FixtureBindings.MultiplyMatrices(matrix, translation);
				Matrix4x4 managedProduct = matrix * translation;
				for (int column = 0; column < 4; ++column)
					for (int row = 0; row < 4; ++row) Expect(nativeProduct[row, column] == managedProduct[row, column], "Native/managed matrix order differs");
				Vector3 transformed = nativeProduct.MultiplyPoint3x4(new Vector3(1, 2, 3));
				Expect(transformed.X == 34 && transformed.Y == 95 && transformed.Z == 201, "Non-symmetric matrix point transform differs");
				Quaternion quaternion = FixtureBindings.EchoQuaternion(new Quaternion(0.125f, -2, 3, 0.75f));
				Color color = FixtureBindings.EchoColor(new Color(0.125f, 0.25f, 0.75f, 0.5f));
				Expect(quaternion.X == 0.125f && quaternion.W == 0.75f && color.B == 0.75f && color.A == 0.5f, "Quaternion/color native conversion differs");
				MathPacket math = FixtureBindings.EchoMath(new MathPacket
				{
					Uv = new Vector2(0.125f, -0.5f), Position = new Vector3(2, -3, 7), Tangent = new Vector4(1, 2, 3, -1)
				});
				Expect(math.Uv.Y == -0.5f && math.Position.Z == 7 && math.Tangent.W == -1, "Vector2/3/4 wire or native mapping differs");
				Expect(FixtureBindings.Add(9_007_199_254_740_993, 7) == 9_007_199_254_741_000, "64-bit precision lost");
				var scalars = FixtureBindings.EchoScalars(new Scalars
				{
					Small = sbyte.MinValue, Byte = byte.MaxValue, Signed16 = short.MinValue, Unsigned16 = ushort.MaxValue,
					Signed32 = int.MinValue, Unsigned32 = uint.MaxValue, Signed64 = long.MinValue,
					Unsigned64 = ulong.MaxValue, Float = -7.25f, Double = 1.0 / 3.0, Flags = FixtureFlags.Visible | FixtureFlags.Selected,
					Wide = WideFlags.Maximum, Signed = SignedFlags.Minimum
				});
				Expect(scalars.Small == sbyte.MinValue && scalars.Byte == byte.MaxValue && scalars.Signed16 == short.MinValue &&
					scalars.Unsigned16 == ushort.MaxValue && scalars.Signed32 == int.MinValue && scalars.Unsigned32 == uint.MaxValue &&
					scalars.Signed64 == long.MinValue && scalars.Unsigned64 == ulong.MaxValue && scalars.Float == -7.25f &&
					scalars.Double == 1.0 / 3.0 && scalars.Flags == (FixtureFlags.Visible | FixtureFlags.Selected) &&
					scalars.Wide == WideFlags.Maximum && scalars.Signed == SignedFlags.Minimum, "Fixed-width/enum values differ");
				var handle = new FixtureHandle { Session = 17, Index = 1, Generation = 3, Kind = 1 };
				Expect(FixtureBindings.ReadObject(handle) == 1, "Handle layout differs");
				handle.Generation = 4;
				Reject(() => FixtureBindings.ReadObject(handle), "Stale fixture handle");
				var point = new Point { X = -2.25f, Y = 7.125, Enabled = true };
				Point copied = FixtureBindings.EchoPoint(point);
				Expect(copied.X == point.X && copied.Y == point.Y && copied.Enabled, "Wire struct layout differs");
				Expect(FixtureBindings.EchoOptional(null) == null && FixtureBindings.EchoOptional(point)!.Value.Y == point.Y, "Optional point differs");
				Expect(FixtureBindings.EchoText(null) == null && FixtureBindings.EchoText("") == "", "null/empty differs");
				string first = FixtureBindings.EchoText("中文\0返回")!;
				_ = FixtureBindings.EchoText("second result");
				Expect(first == "中文\0返回", "UTF-8/embedded zero/result lifetime differs");
				Point[] points = FixtureBindings.EchoValues(new[] { point, copied });
				Expect(points.Length == 2 && points[1].Y == 7.125, "Array layout differs");
				Expect(FixtureBindings.EchoValues(Array.Empty<Point>()).Length == 0, "Empty array differs");
				var packet = FixtureBindings.EchoPacket(new Packet { Name = "嵌套", Values = points, Identity = ulong.MaxValue });
				Expect(packet.Name == "嵌套" && packet.Values.Length == 2 && packet.Identity == ulong.MaxValue, "Nested DTO differs");
				Expect(FixtureBindings.Sum(new[] { 1, -2, 8 }) == 7, "Span differs");
				uint before = FixtureBindings.GetCount();
				Expect(FixtureBindings.Mutate("只执行一次") == "只执行一次" && FixtureBindings.GetCount() == before + 1, "Mutation executed twice");
				Reject(() => FixtureBindings.Fail(17), "Fixture failure 17");
				Reject(() => FixtureBindings.Fail(17), "session 17");
				Reject(() => FixtureBindings.Add(long.MaxValue, 1), "Addition overflow");
				bool decodedFailed = false;
				try { _ = FixtureBindings.InvalidOutput(); }
				catch (Exception error) when (error.Message.Contains("UTF-8", StringComparison.Ordinal)) { decodedFailed = true; }
				Expect(decodedFailed && liveResults() == 0, "Invalid native output validation leaked native memory");
				Reject(() => FixtureBindings.BorrowedOutput("cannot borrow input as output"), "not owned by its result arena");
				BindingOutcome malformed = default;
				malformedResult(&malformed);
				decodedFailed = false;
				try { _ = BindingMemory.String(malformed.Error, false); }
				catch (System.Text.DecoderFallbackException) { decodedFailed = true; }
				finally { BindingMemory.Release(ref malformed); }
				Expect(decodedFailed && liveResults() == 0, "Managed result decoding failure leaked owned native output");
				Reject(() => FixtureBindings.EchoPacket(new Packet { Name = "allocated first", Values = null! }), "values");
				Expect(BindingArena.CurrentAllocations == 0, "Parameter conversion failure leaked managed-owned input");
				setBudget(0);
				try
				{
					Reject(() => FixtureBindings.Mutate("allocation failure"), "no error text available");
					Reject(() => FixtureBindings.Mutate("allocation failure"), "session 17");
				}
				finally { setBudget(BindingArena.MaximumBytes); }
				Expect(FixtureBindings.GetCount() == before + 1 && liveResults() == 0, "Allocation failure caused mutation or leaked a result");
				bool encodedFailed = false;
				try { _ = FixtureBindings.EchoText("\ud800"); }
				catch (System.Text.EncoderFallbackException) { encodedFailed = true; }
				Expect(encodedFailed && BindingArena.CurrentAllocations == 0, "Invalid UTF-16 was silently replaced or leaked input");
				var nativeTable = (FixtureModule.Table*)((BindingModuleDescriptor*)module)->Functions;
				foreach (ulong length in new[] { 1ul, ulong.MaxValue, BindingArena.MaximumBytes })
				{
					DataView badInput = new() { Length = length, Present = 1 }, result;
					BindingOutcome outcome = default;
					int status = nativeTable->FixtureBindings_EchoValues(context, &badInput, &result, &outcome);
					try { Expect(status != 0, "Invalid native array range accepted"); }
					finally { BindingMemory.Release(ref outcome); }
				}
				FixtureModule.Value_BindingFixture_Point badPoint = new() { F_Enabled = 7 }, pointOutput;
				BindingOutcome badPointOutcome = default;
				int badPointStatus = nativeTable->FixtureBindings_EchoPoint(context, &badPoint, &pointOutput, &badPointOutcome);
				try { Expect(badPointStatus != 0, "Invalid native boolean accepted"); }
				finally { BindingMemory.Release(ref badPointOutcome); }
				Task.Run(() => Reject(() => FixtureBindings.GetCount(), "unavailable")).GetAwaiter().GetResult();
				BindingModuleDescriptor saved = *(BindingModuleDescriptor*)module;
				BindingModuleDescriptor mismatch = saved;
				mismatch.Hash[0] ^= 1;
				bool rejected = false;
				try { FixtureModule.Bind((nint)(&mismatch), context); }
				catch (InvalidOperationException error) { rejected = error.Message.Contains("contract mismatch", StringComparison.Ordinal); }
				Expect(rejected, "Same-size incompatible module accepted");
				int[] delayed = { 2, 5, 9 };
				FixtureBindings.CaptureInput(delayed);
				delayed[0] = 100;
				GC.Collect();
				Expect(FixtureBindings.ReadCaptured().SequenceEqual(new[] { 2, 5, 9 }), "Deferred native work borrowed expired input");
				using (var callbacks = FixtureModule.CreateCallbacks(17))
				{
					Expect(runCallback(callbacks.Descriptor, callbacks.Context, 0) == 0, "Native-to-managed callback failed");
					Expect(runCallback(callbacks.Descriptor, callbacks.Context, 1) == 1, "Managed exception crossed ABI or lost context/stack");
					Expect(delayedCallback(callbacks.Descriptor, callbacks.Context) == 0, "Deferred managed work borrowed native input or leaked output");
					nint oldContext = callbacks.Context;
					var function = ((FixtureModule.Table*)callbacks.Functions)->FixtureBindings_ManagedScale;
					callbacks.Dispose();
					FixtureModule.Value_BindingFixture_Point input = default, output;
					BindingOutcome outcome = default;
					int status = function(oldContext, &input, &output, &outcome);
					try { Expect(status != 0, "Closed callback registration accepted"); }
					finally { BindingMemory.Release(ref outcome); }
				}
				Expect(liveResults() == 0 && BindingArena.CurrentAllocations == 0, "Callback outcome ownership leaked");
				CommandTests(library);
				for (int index = 0; index < 100; ++index) _ = FixtureBindings.GetCount();
				long allocated = GC.GetAllocatedBytesForCurrentThread();
				for (int index = 0; index < 1000; ++index) _ = FixtureBindings.GetCount();
				Expect(GC.GetAllocatedBytesForCurrentThread() == allocated, "Scalar binding allocates after warmup");
				for (int index = 0; index < 10000; ++index) { _ = FixtureBindings.GetCount(); _ = FixtureBindings.MultiplyMatrices(matrix, translation); }
				_ = Stopwatch.GetTimestamp();
				const int scalarSamples = 10000;
				allocated = GC.GetAllocatedBytesForCurrentThread();
				long started = Stopwatch.GetTimestamp();
				for (int index = 0; index < scalarSamples; ++index) _ = FixtureBindings.GetCount();
				long scalarTicks = Stopwatch.GetTimestamp() - started;
				long scalarBytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
				allocated = GC.GetAllocatedBytesForCurrentThread();
				started = Stopwatch.GetTimestamp();
				for (int index = 0; index < scalarSamples; ++index) _ = FixtureBindings.MultiplyMatrices(matrix, translation);
				long matrixTicks = Stopwatch.GetTimestamp() - started;
				long matrixBytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
				Expect(scalarBytes == 0 && matrixBytes == 0, "Warmed scalar/matrix binding allocates");
				Point[] transfer = Enumerable.Repeat(point, 8192).ToArray();
				for (int index = 0; index < 3; ++index) _ = FixtureBindings.EchoValues(transfer);
				const int arraySamples = 20;
				allocated = GC.GetAllocatedBytesForCurrentThread();
				started = Stopwatch.GetTimestamp();
				for (int index = 0; index < arraySamples; ++index)
				{
					var returned = FixtureBindings.EchoValues(transfer);
					Expect(returned.Length == transfer.Length && !ReferenceEquals(returned, transfer), "Array transfer lost copy ownership");
				}
				long arrayTicks = Stopwatch.GetTimestamp() - started;
				long arrayBytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
				Expect(arrayBytes >= (long)arraySamples * transfer.Length * sizeof(Point) && liveResults() == 0 && BindingArena.CurrentAllocations == 0, "Array transfer allocation or ownership accounting differs");
				Console.WriteLine($"Binding measurements: scalar {scalarSamples} calls, {scalarBytes} GC bytes, {scalarTicks * 1e9 / Stopwatch.Frequency / scalarSamples:F1} ns/call; matrix {scalarSamples} calls, {matrixBytes} GC bytes, {matrixTicks * 1e9 / Stopwatch.Frequency / scalarSamples:F1} ns/call; array {arraySamples} x {transfer.Length} Point, {arrayBytes} GC bytes, {arrayTicks * 1e3 / Stopwatch.Frequency / arraySamples:F3} ms/call.");
				var requirement = new BindingRequirement(FixtureModule.Id, "Fixture", FixtureModule.Hash, ((BindingModuleDescriptor*)module)->FunctionCount, false);
				BindingContent.ValidateRequirements(new[] { requirement }, false);
				Reject(() => BindingContent.ValidateRequirements(Array.Empty<BindingRequirement>(), false), "incomplete");
				Reject(() => BindingContent.ValidateRequirements(new[] { requirement with { Hash = new string('0', 64) } }, false), "differs");
				Reject(() => BindingContent.ValidateRequirements(new[] { requirement with { Name = "WrongName" } }, false), "differs");
				Reject(() => BindingContent.ValidateRequirements(new[] { requirement }, true), "incomplete");
				var scopedRequirement = new BindingRequirement(ScopedModule.Id, "Scoped", ScopedModule.Hash, ((BindingModuleDescriptor*)getModuleById(ScopedModule.Id))->FunctionCount, true);
				BindingContent.ValidateRequirements(new[] { scopedRequirement, requirement }, true);
				Reject(() => BindingContent.ValidateRequirements(new[] { scopedRequirement, requirement }, false), "incomplete");
				RegistryTests(module, context, closedContext, getModuleById(DependentModule.Id), getModuleById(ScopedModule.Id));
				FixtureModule.Clear();
				Console.WriteLine("Native values, ownership, callbacks and scalar allocations verified.");
			}
			finally { if (!hosted) NativeLibrary.Free(library); }
			Console.WriteLine("Bindings generator and real bidirectional ABI tests passed.");
			return 0;
		}
		catch (Exception error) { Console.Error.WriteLine(error); return 1; }
	}

	private static void GeneratorTests(string generator, string contractRoot, string scratchRoot)
	{
		Directory.CreateDirectory(scratchRoot);
		(string output, int status) Generate(string source, string destination, bool check = false, bool nativeOnly = false)
		{
			var start = new ProcessStartInfo("dotnet") { RedirectStandardOutput = true, RedirectStandardError = true };
			foreach (string argument in new[] { generator, "--input", source, "--output", destination }) start.ArgumentList.Add(argument);
			start.ArgumentList.Add("--source");
			start.ArgumentList.Add(Path.GetFullPath(Path.Combine(contractRoot, "../../../Managed/Ursa.Runtime/MathValues.cs")));
			if (check) start.ArgumentList.Add("--check");
			if (nativeOnly) start.ArgumentList.Add("--native-only");
			using Process child = Process.Start(start)!;
			Task<string> stdout = child.StandardOutput.ReadToEndAsync(), stderr = child.StandardError.ReadToEndAsync();
			child.WaitForExit();
			return (stdout.GetAwaiter().GetResult() + stderr.GetAwaiter().GetResult(), child.ExitCode);
		}
		string first = Path.Combine(scratchRoot, "First"), second = Path.Combine(scratchRoot, "Second");
		var generated = Generate(contractRoot, first);
		Expect(generated.status == 0, generated.output);
		Expect(Generate(contractRoot, second).status == 0, "Second generation failed");
		foreach (string file in Directory.GetFiles(first, "*", SearchOption.AllDirectories))
			Expect(File.ReadAllBytes(file).SequenceEqual(File.ReadAllBytes(Path.Combine(second, Path.GetRelativePath(first, file)))), "Nondeterministic output");
		Expect(Generate(contractRoot, first, true).status == 0, "Check mode rejected current outputs");
		string manifest = Path.Combine(first, "Bindings.manifest.json");
		File.AppendAllText(manifest, " ");
		byte[] changed = File.ReadAllBytes(manifest);
		Expect(Generate(contractRoot, first, true).status != 0 && File.ReadAllBytes(manifest).SequenceEqual(changed), "Check mode wrote drifted files");
		Expect(Generate(contractRoot, first).status == 0, "Repair generation failed");
		string reordered = Path.Combine(scratchRoot, "Reordered");
		Directory.CreateDirectory(reordered);
		foreach (string file in Directory.GetFiles(contractRoot, "*.cs").OrderDescending(StringComparer.Ordinal))
			File.Copy(file, Path.Combine(reordered, Path.GetFileName(file)), true);
		Expect(Generate(reordered, second).status == 0, "Reordered generation failed");
		string commandContract = Path.Combine(reordered, "Commands.bindings.cs");
		File.Delete(commandContract);
		Expect(Generate(reordered, second).status == 0 &&
			!File.Exists(Path.Combine(second, "Managed/Fixture.Commands.g.cs")) &&
			!File.Exists(Path.Combine(second, "Native/Fixture.Commands.generated.hpp")) &&
			File.Exists(Path.Combine(second, "Managed/Fixture.g.cs")), "Deleting the final command retained stale codecs or removed ordinary bindings");
		File.Copy(Path.Combine(contractRoot, "Commands.bindings.cs"), commandContract);
		Expect(Generate(reordered, second).status == 0, "Restoring the command contract failed");
		foreach (string file in Directory.GetFiles(first, "*", SearchOption.AllDirectories))
			Expect(File.ReadAllBytes(file).SequenceEqual(File.ReadAllBytes(Path.Combine(second, Path.GetRelativePath(first, file)))), "Input enumeration order changed output");
		string oldModule = Path.Combine(reordered, "Old.bindings.cs");
		File.WriteAllText(oldModule, "using Ursa.Bindings; [BindingModule(\"Old\")] internal static partial class OldBindings {}");
		Expect(Generate(reordered, second).status == 0 && File.Exists(Path.Combine(second, "Native/Old.generated.hpp")), "Additional module was not generated");
		File.Delete(oldModule);
		Expect(Generate(reordered, second, true).status != 0, "Stale module outputs were accepted");
		Expect(Generate(reordered, second).status == 0 && !File.Exists(Path.Combine(second, "Native/Old.generated.hpp")) &&
			!File.Exists(Path.Combine(second, "Managed/Old.g.cs")), "Deleted contract outputs were not cleaned");
		string snapshot = Path.Combine(scratchRoot, "NativeSnapshot");
		Expect(Generate(reordered, snapshot, nativeOnly: true).status == 0 && !Directory.Exists(Path.Combine(snapshot, "Managed")),
			"Native snapshot contains managed source outputs");
		Expect(Generate(reordered, snapshot, check: true, nativeOnly: true).status == 0, "Current native snapshot drifted");
		byte[] snapshotIdentity = File.ReadAllBytes(Path.Combine(snapshot, "Bindings.manifest.json"));
		string valuesFile = Path.Combine(reordered, "Values.cs");
		File.WriteAllText(valuesFile, File.ReadAllText(valuesFile).Replace("public bool Enabled;", "public uint Enabled;", StringComparison.Ordinal));
		Expect(Generate(reordered, second).status == 0 && File.ReadAllText(Path.Combine(second, "Bindings.manifest.json")) != File.ReadAllText(manifest),
			"Referenced type change did not invalidate module identity");
		Expect(Generate(reordered, snapshot, check: true, nativeOnly: true).status != 0 &&
			File.ReadAllBytes(Path.Combine(snapshot, "Bindings.manifest.json")).SequenceEqual(snapshotIdentity),
			"Changed shared layout did not invalidate native snapshot or check mode modified it");
		string classLayout = Path.Combine(scratchRoot, "ClassLayout"), classOutput = Path.Combine(scratchRoot, "ClassLayoutOutput");
		Directory.CreateDirectory(classLayout);
		string classFile = Path.Combine(classLayout, "Layout.bindings.cs");
		string layoutContract = "using Ursa.Bindings; [WireValue] public struct LayoutPayload { public float X; public double Y; public bool Enabled; } " +
			"[BindingModule(\"Layout\")][NativeHeader(\"x.hpp\")] internal static partial class LayoutBindings { [NativeMethod(\"F::Echo\")] internal static partial LayoutPayload Echo(LayoutPayload value); }";
		File.WriteAllText(classFile, layoutContract);
		Expect(Generate(classLayout, classOutput).status == 0, "Struct layout fixture failed");
		string structIdentity = File.ReadAllText(Path.Combine(classOutput, "Bindings.manifest.json"));
		string structLayout = File.ReadAllText(Path.Combine(classOutput, "Native/BindingTypes.generated.hpp"));
		Expect(structLayout.Contains("sizeof(Value_LayoutPayload) == 24", StringComparison.Ordinal) &&
			structLayout.Contains("offsetof(Value_LayoutPayload, F_X) == 0", StringComparison.Ordinal), "Unexpected struct layout fixture");
		File.WriteAllText(classFile, layoutContract.Replace("struct LayoutPayload", "class LayoutPayload", StringComparison.Ordinal));
		Expect(Generate(classLayout, classOutput).status == 0, "Class layout fixture failed");
		string referenceLayout = File.ReadAllText(Path.Combine(classOutput, "Native/BindingTypes.generated.hpp"));
		Expect(referenceLayout.Contains("sizeof(Value_LayoutPayload) == 24", StringComparison.Ordinal) &&
			referenceLayout.Contains("offsetof(Value_LayoutPayload, F_X) == 4", StringComparison.Ordinal) &&
			File.ReadAllText(Path.Combine(classOutput, "Bindings.manifest.json")) != structIdentity,
			"Same-size class presence/field offset change did not invalidate the module identity");
		string invalid = Path.Combine(scratchRoot, "Invalid");
		Directory.CreateDirectory(invalid);
		void Invalid(string contract, string code)
		{
			File.WriteAllText(Path.Combine(invalid, "Test.bindings.cs"), "using Ursa.Bindings;\n" + contract);
			var result = Generate(invalid, first);
			Expect(result.status != 0 && result.output.Contains(code, StringComparison.Ordinal), "Expected diagnostic " + code + ": " + result.output);
			Expect(Generate(contractRoot, first, true).status == 0, "Failed generation modified previous good outputs");
		}
		Invalid("[BindingModule(\"Bad\")][NativeHeader(\"x.hpp\")] internal static partial class A { [NativeMethod(\"F::M\")] internal static partial object Bad(object value); }", "UBG0200");
		Invalid("[BindingModule(\"A\", Dependencies = new[] { \"B\" })] internal static partial class A {} [BindingModule(\"B\", Dependencies = new[] { \"A\" })] internal static partial class B {}", "UBG0110");
		Invalid("[BindingModule(\"A\")][NativeHeader(\"x.hpp\")] internal static partial class A { [NativeMethod(\"F::M\")] internal static partial int Same(int value); [NativeMethod(\"F::M\")] internal static partial int Same(float value); }", "UBG0104");
		Invalid("[BindingModule(\"A\")][NativeHeader(\"x.hpp\")] internal static partial class A { [NativeMethod(\"F::M\")] internal static partial int Bad(System.DateTime value); }", "UBG0200");
		Invalid("[BindingModule(\"A\", Required = true, CallScoped = true)] internal static partial class A {}", "UBG0112");
		Invalid("[BindingModule(\"A\")][NativeHeader(\"x.hpp\")] internal static partial class A { [NativeMethod(\"F::M\")] internal static partial int Bad([BindingSession] ulong session); }", "UBG0109");
		Invalid("[BindingModule(\"Runtime\", Dependencies = new[] { \"Editor\" })] internal static partial class A {} [BindingModule(\"Editor\")] internal static partial class B {}", "UBG0111");
		Invalid("[BindingModule(\"A\")] internal static partial class A { [RecordedCommand] internal static partial void Bad(int value); }", "UBG0113");
		Invalid("[SerializedArchive] internal struct Archive { public string Json; } [BindingModule(\"A\")] internal static partial class A { [RecordedCommand] internal static partial void Bad(BindingCommandWriter writer, Archive archive); }", "UBG0115");
	}

	private static void CommandTests(nint library)
	{
		var run = (delegate* unmanaged[Cdecl]<byte*, ulong, uint, int>)NativeLibrary.GetExport(library, "RunCommandBatch");
		var nativeOnly = (delegate* unmanaged[Cdecl]<int>)NativeLibrary.GetExport(library, "CheckNativeOnlyCommand");
		using var writer = new BindingCommandWriter(CommandCodec.Hash);
		Point[] points = { new() { X = 1.25f, Y = 2, Enabled = true }, new() { X = 3, Y = -7.125 } };
		int[] values = { -7, 2, 9 };
		var packet = new Packet { Name = "接口\0data", Values = points, Identity = ulong.MaxValue };
		FixtureCommands.Capture(writer, packet, values, Matrix4x4.Translation(new Vector3(11, 2, 3)));
		byte[] batch = writer.ToArray();
		points[0].X = 999;
		values[0] = 999;
		packet.Name = "changed after recording";
		CommandCodec.Validate(batch);
		fixed (byte* data = batch) Expect(run(data, (ulong)batch.Length, 0) == 0, "Generated native command decode/encode differs from managed recording");
		fixed (byte* data = batch) Expect(run(data, (ulong)batch.Length, 2) == 1, "Decoded command arrays exceeded their call arena budget");
		fixed (byte* data = batch) Expect(run(data, (ulong)batch.Length, 0) == 0, "A failed decode damaged the next valid command batch");
		byte[] preserved = writer.ToArray();
		packet.Name = "\ud800";
		Reject(() => FixtureCommands.Capture(writer, packet, values, Matrix4x4.Identity), "");
		Expect(writer.ToArray().SequenceEqual(preserved), "Failed command encoding damaged the preceding records");
		Reject(() => FixtureCommands.Reset(writer), "reserved");
		Expect(nativeOnly() == 0, "Native-only commands were accepted from a managed batch or rejected by native execution");
		using (var other = new BindingCommandWriter(new string('0', 64)))
			Reject(() => FixtureCommands.Flag(other, true), "different binding contract");
		void Invalid(byte[] invalid)
		{
			Reject(() => CommandCodec.Validate(invalid), "");
			fixed (byte* data = invalid) Expect(run(data, (ulong)invalid.Length, 1) == 1, "Native decoder accepted a malformed command batch");
		}
		foreach (int cut in new[] { 0, BindingCommandWriter.HeaderSize - 1, BindingCommandWriter.HeaderSize, batch.Length - 1 })
			Invalid(batch[..cut]);
		byte[] changed = (byte[])batch.Clone();
		changed[16] ^= 1;
		Invalid(changed);
		changed = (byte[])batch.Clone();
		BinaryPrimitives.WriteUInt32LittleEndian(changed.AsSpan(BindingCommandWriter.HeaderSize), 0);
		Invalid(changed);
		changed = (byte[])batch.Clone();
		BinaryPrimitives.WriteUInt32LittleEndian(changed.AsSpan(BindingCommandWriter.HeaderSize + 4), uint.MaxValue);
		Invalid(changed);
		writer.Clear();
		FixtureCommands.Flag(writer, true);
		changed = writer.ToArray();
		BinaryPrimitives.WriteUInt32LittleEndian(changed.AsSpan(BindingCommandWriter.HeaderSize + 8), 7);
		Invalid(changed);
		changed = writer.ToArray();
		changed[^1] = 1;
		Invalid(changed);
		writer.Clear();
		FixtureCommands.Values(writer, values);
		changed = writer.ToArray();
		BinaryPrimitives.WriteUInt64LittleEndian(changed.AsSpan(BindingCommandWriter.HeaderSize + 12), ulong.MaxValue);
		Invalid(changed);
		Console.WriteLine("Generated command codecs, captured inputs, atomic recording, malformed batches and native-only isolation verified.");
	}

	private static void RegistryTests(nint module, nint context, nint closedContext, nint dependent, nint scoped)
	{
		BindingModuleReference root = new() { Id = FixtureModule.Id, Descriptor = module, Context = context };
		BindingModuleReference child = new() { Id = DependentModule.Id, Descriptor = dependent, Context = context };
		BindingModuleReference scope = new() { Id = ScopedModule.Id, Descriptor = scoped, Context = context };
		BindingRegistry.Bind(new[] { root });
		BindingSnapshot first = BindingRegistry.Capture();
		Reject(() => BindingRegistry.Bind(Array.Empty<BindingModuleReference>()), "Missing required");
		Reject(() => BindingRegistry.Bind(new[] { child }), "missing dependency");
		Reject(() => BindingRegistry.EnterCallScope(scope), "missing dependency");
		Reject(() => BindingRegistry.Bind(new[] { root, root }), "Duplicate");
		BindingModuleReference invalid = child;
		invalid.Context = closedContext;
		Reject(() => BindingRegistry.Bind(new[] { root, invalid }), "closed or invalid");
		Expect(ReferenceEquals(first, BindingRegistry.Capture()) && FixtureBindings.Add(1, 2) == 3, "Rejected candidate changed the active view");
		BindingRegistry.Bind(new[] { root, child });
		BindingSnapshot second = BindingRegistry.Capture();
		Expect(DependentBindings.Add(3, 4) == 7, "Dependency module was not installed");
		Reject(() => BindingRegistry.Bind(new[] { root, child, scope }), "requires a call scope");
		Reject(() => ScopedBindings.Add(1, 2), "unavailable");
		using (BindingRegistry.EnterCallScope(scope))
		{
			Expect(ScopedBindings.Add(1, 2) == 3, "Scoped module was not installed");
			using (BindingRegistry.EnterCallScope(scope)) Expect(ScopedBindings.Add(2, 3) == 5, "Nested call scope failed");
			Expect(ScopedBindings.Add(3, 4) == 7, "Nested scope did not restore its parent");
		}
		Reject(() => ScopedBindings.Add(1, 2), "unavailable");
		BindingRegistry.Restore(first);
		Reject(() => DependentBindings.Add(1, 2), "unavailable");
		BindingRegistry.Restore(second);
		IDisposable outer = BindingRegistry.EnterCallScope(scope);
		IDisposable inner = BindingRegistry.EnterCallScope(scope);
		Reject(outer.Dispose, "reverse order");
		Task.Run(() => Reject(inner.Dispose, "owner thread")).GetAwaiter().GetResult();
		inner.Dispose();
		outer.Dispose();
		IDisposable expiredScope = BindingRegistry.EnterCallScope(scope);
		BindingRegistry.Restore(first);
		expiredScope.Dispose();
		Reject(() => ScopedBindings.Add(1, 2), "unavailable");
		BindingRegistry.Invalidate(second);
		Reject(() => BindingRegistry.Restore(second), "closed host or script session");
		Expect(FixtureBindings.Add(1, 2) == 3, "Invalidating an inactive session cleared the current view");
		BindingRegistry.Register(new BindingModuleRegistration(1, "RejectedConnector", FixtureModule.Hash,
			((BindingModuleDescriptor*)module)->FunctionCount, false, false, new[] { FixtureModule.Id },
			static (_, _) => throw new InvalidOperationException("Injected connector failure"), static () => { }, null));
		BindingModuleReference broken = root;
		broken.Id = 1;
		Reject(() => BindingRegistry.Bind(new[] { root, broken }), "Injected connector failure");
		Expect(ReferenceEquals(first, BindingRegistry.Capture()) && FixtureBindings.Add(1, 2) == 3, "Commit failure did not restore the previous view");
		Task.Run(() => BindingRegistry.Invalidate(first)).GetAwaiter().GetResult();
		Reject(() => FixtureBindings.Add(1, 2), "closed host or script session");
		BindingRegistry.Bind(new[] { root });
		first = BindingRegistry.Capture();
		var exports = BindingRegistry.DescribeCallbacks();
		Expect(exports.Length == 1 && exports[0].Id == FixtureModule.Id, "Generated callback module registration differs");
		BindingRegistry.Shutdown();
		Reject(() => BindingRegistry.Restore(first), "closed host or script session");
		Reject(() => FixtureBindings.Add(1, 2), "unavailable");
		Console.WriteLine("Module dependencies, candidate rollback, scoped contexts and session invalidation verified.");
	}
}
