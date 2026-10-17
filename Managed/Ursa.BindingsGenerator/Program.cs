using System;
using System.Collections.Generic;
using System.IO;
using System.Globalization;
using System.Linq;
using System.Text.Json;

namespace Ursa.BindingsGenerator;

internal static class Program
{
	internal static int Main(string[] args)
	{
		CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
		CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
		try
		{
			if (args.Length == 0 || args.Contains("--help"))
			{
				Console.WriteLine("Ursa.BindingsGenerator --input <source-root> --output <generated-root> [--source <shared.cs>] [--check] [--native-only] [--model-only]");
				return 0;
			}
			string Required(string option)
			{
				int index = Array.IndexOf(args, option);
				if (index < 0 || index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
					throw new BindingDiagnostic("UBG0001", $"Missing {option} value.");
				return Path.GetFullPath(args[index + 1]);
			}
			string input = Required("--input"), output = Required("--output");
			if (!Directory.Exists(input)) throw new BindingDiagnostic("UBG0001", "Input directory does not exist.");
			var extraSources = new List<string>();
			for (int index = 0; index < args.Length; ++index)
				if (args[index] == "--source")
				{
					if (++index >= args.Length || !File.Exists(args[index])) throw new BindingDiagnostic("UBG0001", "--source requires an existing C# file.");
					extraSources.Add(Path.GetFullPath(args[index]));
				}
			var model = ContractReader.Read(input, extraSources);
			IReadOnlyDictionary<string, string> files = args.Contains("--model-only")
				? new Dictionary<string, string> { ["Bindings.model.json"] = JsonSerializer.Serialize(new
				{
					Version = 1,
					Modules = model.Modules.Select(m => new { m.Name, m.Dependencies, m.Hash,
						Methods = m.Methods.Select(f => new { f.Name, f.Identity, Direction = f.Direction.ToString(),
							f.Target, Return = f.Return.Key, Parameters = f.Parameters.Select(p => new { p.Name, Type = p.Type.Key }) }) }),
					Types = model.Types.Select(t => new { t.Key, t.Kind, t.Size, t.Alignment })
				}, new JsonSerializerOptions { WriteIndented = true }) + "\n" }
				: BindingEmitter.Emit(model);
			if (args.Contains("--native-only"))
				files = files.Where(entry => entry.Key.StartsWith("Native/", StringComparison.Ordinal) || entry.Key == "Bindings.manifest.json")
					.ToDictionary(entry => entry.Key, entry => entry.Value, StringComparer.Ordinal);
			OutputPublisher.Publish(input, output, files, args.Contains("--check"));
			Console.WriteLine($"Validated {model.Modules.Count} binding modules, {model.Modules.Sum(m => m.Methods.Count)} methods, {model.Types.Count} wire types.");
			return 0;
		}
		catch (BindingDiagnostic error)
		{
			Console.Error.WriteLine(error);
			return 1;
		}
		catch (Exception error)
		{
			Console.Error.WriteLine($"UBG0000: {error.Message}");
			return 1;
		}
	}
}
