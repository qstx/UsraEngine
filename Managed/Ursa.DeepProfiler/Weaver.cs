using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using MetadataReaderProvider = System.Reflection.Metadata.MetadataReaderProvider;
using System.Reflection.PortableExecutable;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Mono.Cecil.Rocks;
using MethodDefinition = Mono.Cecil.MethodDefinition;
using TypeDefinition = Mono.Cecil.TypeDefinition;

namespace Ursa.DeepProfiler;

public static class Weaver
{
	public const string Version = "Ursa.DeepProfiler/1";
	private const string ignored = "Ursa.Profiling.IgnoredByDeepProfilerAttribute";
	private const string profiled = "Ursa.Profiling.DeepProfiledAttribute";

	public static void ValidateCompiledOutRuntime(string runtime)
	{
		using var module = ModuleDefinition.ReadModule(runtime);
		TypeDefinition marker = module.GetType("Ursa.Profiling.ProfilerMarker");
		TypeDefinition scope = marker.NestedTypes.Single(type => type.Name == "AutoScope");
		if (marker.Fields.Count != 0 || scope.Fields.Count != 0)
			throw new InvalidDataException("Disabled markers retain sampling state.");
		foreach (MethodDefinition method in marker.Methods.Concat(scope.Methods))
		{
			if (method.HasBody && method.Body.Instructions.Any(instruction => instruction.OpCode.FlowControl == FlowControl.Call ||
				instruction.OpCode == OpCodes.Newobj || instruction.OpCode == OpCodes.Newarr))
				throw new InvalidDataException("Disabled marker contains calls or allocation: " + method.FullName);
		}
	}

	public static int Instrument(string input, string output, string runtime, string configuration)
	{
		if (configuration != "Debug" && configuration != "Release")
			throw new InvalidOperationException("Deep Profiling is only supported in Debug/Release; Publish must remain uninstrumented.");
		input = Path.GetFullPath(input);
		output = Path.GetFullPath(output);
		runtime = Path.GetFullPath(runtime);
		if (string.Equals(input, output, StringComparison.OrdinalIgnoreCase))
			throw new InvalidOperationException("Instrument into a separate staging directory; input files must remain intact on failure.");
		using var resolver = new DefaultAssemblyResolver();
		resolver.AddSearchDirectory(Path.GetDirectoryName(input)!);
		resolver.AddSearchDirectory(Path.GetDirectoryName(runtime)!);
		resolver.AddSearchDirectory(Path.GetDirectoryName(typeof(object).Assembly.Location)!);
		using var framework = ModuleDefinition.ReadModule(runtime, new ReaderParameters { AssemblyResolver = resolver });
		using var module = ModuleDefinition.ReadModule(input, new ReaderParameters
		{
			AssemblyResolver = resolver, InMemory = true, ReadSymbols = true,
			SymbolReaderProvider = new PortablePdbReaderProvider()
		});
		CheckConfiguration(module, configuration);
		CheckConfiguration(framework, configuration);
		if (module.Assembly.CustomAttributes.Any(attribute => attribute.AttributeType.FullName == profiled))
			throw new InvalidOperationException("Assembly is already instrumented: " + input);
		if (module.Assembly.Name.HasPublicKey)
			throw new InvalidOperationException("Strong-named project assemblies require re-signing and cannot be instrumented by this tool.");
		TypeDefinition marker = framework.GetType("Ursa.Profiling.ProfilerMarker")
			?? throw new InvalidOperationException("Runtime does not provide ProfilerMarker.");
		if (!marker.Fields.Any(field => field.Name == "state"))
			throw new InvalidOperationException("The selected Runtime has profiling compiled out; build the matching development framework with profiling enabled.");
		var markerConstructor = module.ImportReference(marker.Methods.Single(method => method.IsConstructor && method.Parameters.Count == 4));
		var auto = module.ImportReference(marker.Methods.Single(method => method.Name == "Auto"));
		var scopeType = module.ImportReference(marker.NestedTypes.Single(type => type.Name == "AutoScope"));
		var dispose = module.ImportReference(marker.NestedTypes.Single(type => type.Name == "AutoScope").Methods.Single(method => method.Name == "Dispose"));
		TypeDefinition[] types = AllTypes(module.Types).ToArray();
		var ignoredStates = new HashSet<string>(StringComparer.Ordinal);
		var stateNames = new Dictionary<string, string>(StringComparer.Ordinal);
		foreach (MethodDefinition method in types.SelectMany(type => type.Methods))
		{
			foreach (CustomAttribute attribute in method.CustomAttributes.Where(attribute =>
				attribute.AttributeType.FullName is "System.Runtime.CompilerServices.AsyncStateMachineAttribute" or
				"System.Runtime.CompilerServices.IteratorStateMachineAttribute" or
				"System.Runtime.CompilerServices.AsyncIteratorStateMachineAttribute"))
			{
				if (attribute.ConstructorArguments[0].Value is not TypeReference state) continue;
				stateNames[state.FullName] = Name(method);
				if (IsIgnored(method)) ignoredStates.Add(state.FullName);
			}
		}
		var holder = new TypeDefinition("", "__UrsaDeepProfileMarkers",
			TypeAttributes.NotPublic | TypeAttributes.Abstract | TypeAttributes.Sealed | TypeAttributes.BeforeFieldInit,
			module.TypeSystem.Object);
		module.Types.Add(holder);
		var initializer = new MethodDefinition(".cctor", MethodAttributes.Private | MethodAttributes.Static |
			MethodAttributes.HideBySig | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName, module.TypeSystem.Void);
		holder.Methods.Add(initializer);
		ILProcessor initialize = initializer.Body.GetILProcessor();
		int count = 0;
		foreach (MethodDefinition method in types.SelectMany(type => type.Methods).ToArray())
		{
			if (!method.HasBody || method.Body.Instructions.Count == 0 || IsIgnored(method) ||
				ignoredStates.Contains(method.DeclaringType.FullName)) continue;
			try
			{
				if (method.Body.Instructions.Any(instruction => instruction.OpCode == OpCodes.Jmp))
					throw new InvalidOperationException("jmp is not supported inside a profiled exception region.");
				var field = new FieldDefinition("M" + count, FieldAttributes.Assembly | FieldAttributes.Static |
					FieldAttributes.InitOnly, module.ImportReference(marker));
				holder.Fields.Add(field);
				string name = stateNames.TryGetValue(method.DeclaringType.FullName, out string? original)
					? original + " [" + method.Name + "]" : Name(method);
				SequencePoint? point = method.DebugInformation.SequencePoints.FirstOrDefault(point => !point.IsHidden);
				initialize.Append(Instruction.Create(OpCodes.Ldstr, name));
				initialize.Append(Instruction.Create(OpCodes.Ldstr, point?.Document.Url ?? string.Empty));
				initialize.Append(Instruction.Create(OpCodes.Ldstr, name));
				initialize.Append(Instruction.Create(OpCodes.Ldc_I4, point?.StartLine ?? 0));
				initialize.Append(Instruction.Create(OpCodes.Newobj, markerConstructor));
				initialize.Append(Instruction.Create(OpCodes.Stsfld, field));
				InstrumentMethod(method, field, auto, scopeType, dispose);
				++count;
			}
			catch (Exception error)
			{
				throw new InvalidOperationException(method.FullName + ": " + error.Message, error);
			}
		}
		initialize.Append(Instruction.Create(OpCodes.Ret));
		var attributeConstructor = module.ImportReference(framework.GetType(profiled).Methods.Single(method => method.IsConstructor));
		var instrumented = new CustomAttribute(attributeConstructor);
		instrumented.ConstructorArguments.Add(new CustomAttributeArgument(module.TypeSystem.String, Version));
		module.Assembly.CustomAttributes.Add(instrumented);
		Directory.CreateDirectory(Path.GetDirectoryName(output)!);
		module.Write(output, new WriterParameters
		{
			WriteSymbols = true, SymbolWriterProvider = new PortablePdbWriterProvider(), DeterministicMvid = true
		});
		ValidateSymbols(output);
		return count;
	}

	private static void InstrumentMethod(MethodDefinition method, FieldReference marker, MethodReference auto,
		TypeReference scopeType, MethodReference dispose)
	{
		var body = method.Body;
		body.SimplifyMacros();
		ILProcessor il = body.GetILProcessor();
		Instruction[] original = body.Instructions.ToArray();
		Instruction start = original[0];
		if (method.IsConstructor && !method.IsStatic && !method.DeclaringType.IsValueType)
		{
			Instruction? initialize = original.FirstOrDefault(instruction => instruction.OpCode == OpCodes.Call &&
				instruction.Operand is MethodReference reference && reference.Name == ".ctor" &&
				(reference.DeclaringType.GetElementType().FullName == method.DeclaringType.FullName ||
				 reference.DeclaringType.GetElementType().FullName == method.DeclaringType.BaseType?.GetElementType().FullName));
			if (initialize?.Next == null) throw new InvalidOperationException("Cannot find the instance initialization call.");
			start = initialize.Next;
		}
		var scope = new VariableDefinition(scopeType);
		body.Variables.Add(scope);
		body.InitLocals = true;
		il.InsertBefore(start, Instruction.Create(OpCodes.Ldsflda, marker));
		il.InsertBefore(start, Instruction.Create(OpCodes.Call, auto));
		il.InsertBefore(start, Instruction.Create(OpCodes.Stloc, scope));
		var epilogue = Instruction.Create(OpCodes.Nop);
		var finallyStart = Instruction.Create(OpCodes.Nop);
		VariableDefinition? result = null;
		if (method.ReturnType.MetadataType != MetadataType.Void)
		{
			result = new VariableDefinition(method.ReturnType);
			body.Variables.Add(result);
		}
		foreach (Instruction instruction in original)
		{
			if (instruction.OpCode == OpCodes.Tail) instruction.OpCode = OpCodes.Nop;
			if (instruction.OpCode != OpCodes.Ret) continue;
			if (result != null)
			{
				instruction.OpCode = OpCodes.Stloc;
				instruction.Operand = result;
				il.InsertAfter(instruction, Instruction.Create(OpCodes.Leave, epilogue));
			}
			else
			{
				instruction.OpCode = OpCodes.Leave;
				instruction.Operand = epilogue;
			}
		}
		foreach (ExceptionHandler handler in body.ExceptionHandlers)
		{
			handler.TryEnd ??= finallyStart;
			handler.HandlerEnd ??= finallyStart;
		}
		il.Append(Instruction.Create(OpCodes.Leave, epilogue));
		il.Append(finallyStart);
		il.Append(Instruction.Create(OpCodes.Ldloca, scope));
		il.Append(Instruction.Create(OpCodes.Call, dispose));
		il.Append(Instruction.Create(OpCodes.Endfinally));
		il.Append(epilogue);
		if (result != null) il.Append(Instruction.Create(OpCodes.Ldloc, result));
		il.Append(Instruction.Create(OpCodes.Ret));
		body.ExceptionHandlers.Add(new ExceptionHandler(ExceptionHandlerType.Finally)
		{
			TryStart = start, TryEnd = finallyStart, HandlerStart = finallyStart, HandlerEnd = epilogue
		});
		method.ImplAttributes &= ~MethodImplAttributes.AggressiveInlining;
		method.ImplAttributes |= MethodImplAttributes.NoInlining;
		body.OptimizeMacros();
	}

	private static IEnumerable<TypeDefinition> AllTypes(IEnumerable<TypeDefinition> types)
	{
		foreach (TypeDefinition type in types)
		{
			yield return type;
			foreach (TypeDefinition nested in AllTypes(type.NestedTypes)) yield return nested;
		}
	}

	private static bool IsIgnored(MethodDefinition method)
	{
		if (method.CustomAttributes.Any(attribute => attribute.AttributeType.FullName == ignored)) return true;
		for (TypeDefinition? type = method.DeclaringType; type != null; type = type.DeclaringType)
			if (type.CustomAttributes.Any(attribute => attribute.AttributeType.FullName == ignored)) return true;
		return false;
	}

	private static string Name(MethodDefinition method) => method.DeclaringType.FullName.Replace('/', '.') + "." +
		method.Name + "(" + string.Join(",", method.Parameters.Select(parameter => parameter.ParameterType.FullName)) + ")";

	private static void CheckConfiguration(ModuleDefinition module, string expected)
	{
		string? configuration = module.Assembly.CustomAttributes.FirstOrDefault(attribute =>
			attribute.AttributeType.FullName == "System.Reflection.AssemblyConfigurationAttribute")?.ConstructorArguments[0].Value as string;
		if (configuration != expected)
			throw new InvalidOperationException($"{module.Name} has configuration '{configuration}', expected '{expected}'. Build the matching framework and project first.");
	}

	public static void ValidateSymbols(string assembly)
	{
		using var pe = new PEReader(File.OpenRead(assembly));
		using var pdb = MetadataReaderProvider.FromPortablePdbStream(File.OpenRead(Path.ChangeExtension(assembly, ".pdb")));
		var codeView = pe.ReadDebugDirectory().First(entry => entry.Type == DebugDirectoryEntryType.CodeView);
		Guid identity = pe.ReadCodeViewDebugDirectoryData(codeView).Guid;
		if (new Guid(pdb.GetMetadataReader().DebugMetadataHeader!.Id.AsSpan(0, 16)) != identity)
			throw new InvalidDataException("Instrumented DLL and portable PDB identities differ.");
	}
}
