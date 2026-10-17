using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace Ursa.BindingsGenerator;

internal static class OutputPublisher
{
	internal static void Publish(string input, string output, IReadOnlyDictionary<string, string> generated, bool check)
	{
		if (string.Equals(input.TrimEnd(Path.DirectorySeparatorChar), output.TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase) ||
			input.StartsWith(output.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
			throw new BindingDiagnostic("UBG0300", "Output cannot be the input directory or its ancestor.");
		string Safe(string relative)
		{
			string path = Path.GetFullPath(Path.Combine(output, relative));
			if (!path.StartsWith(output.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
				throw new BindingDiagnostic("UBG0300", "Output manifest contains an escaping path.");
			return path;
		}
		const string index = "Bindings.outputs.json";
		var files = generated.ToDictionary(p => p.Key, p => p.Value.Replace("\r\n", "\n"), StringComparer.Ordinal);
		files[index] = JsonSerializer.Serialize(generated.Keys.Order(StringComparer.Ordinal).ToArray(), new JsonSerializerOptions { WriteIndented = true }) + "\n";
		string indexPath = Safe(index);
		string[] previous = File.Exists(indexPath) ? JsonSerializer.Deserialize<string[]>(File.ReadAllText(indexPath)) ?? Array.Empty<string>() : Array.Empty<string>();
		var stale = previous.Where(p => !files.ContainsKey(p)).ToArray();
		foreach (string path in previous) _ = Safe(path);
		string[] changed = files.Where(p => !File.Exists(Safe(p.Key)) || File.ReadAllText(Safe(p.Key)) != p.Value).Select(p => p.Key).ToArray();
		if (check)
		{
			if (changed.Length != 0 || stale.Any(p => File.Exists(Safe(p))))
				throw new BindingDiagnostic("UBG0301", "Generated bindings drift: " + string.Join(", ", changed.Concat(stale)) + ". Regenerate with the same --input/--output without --check.");
			return;
		}
		var backups = changed.Concat(stale).Distinct(StringComparer.Ordinal).ToDictionary(p => p,
			p => File.Exists(Safe(p)) ? File.ReadAllBytes(Safe(p)) : null, StringComparer.Ordinal);
		var temporary = new List<string>();
		try
		{
			foreach (string relative in changed.OrderBy(p => p == index ? 1 : 0))
			{
				string destination = Safe(relative);
				Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
				string temp = destination + "." + Guid.NewGuid().ToString("N") + ".tmp";
				temporary.Add(temp);
				File.WriteAllText(temp, files[relative], new UTF8Encoding(false));
				File.Move(temp, destination, true);
			}
			foreach (string relative in stale) File.Delete(Safe(relative));
		}
		catch
		{
			foreach (var backup in backups)
			{
				string path = Safe(backup.Key);
				if (backup.Value != null) File.WriteAllBytes(path, backup.Value);
				else if (File.Exists(path)) File.Delete(path);
			}
			throw;
		}
		finally
		{
			foreach (string path in temporary) if (File.Exists(path)) File.Delete(path);
		}
	}
}
