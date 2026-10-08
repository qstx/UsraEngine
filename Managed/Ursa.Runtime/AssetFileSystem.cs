using System;

namespace Ursa;

public static class AssetFileSystem
{
	public static byte[] ReadAllBytes(string virtualPath)
	{
		if (string.IsNullOrWhiteSpace(virtualPath))
		{
			throw new ArgumentException("A VFS path is required.", nameof(virtualPath));
		}
		return EngineApi.ReadFile(virtualPath);
	}

	public static void WriteAllBytes(string virtualPath, byte[] bytes)
	{
		if (string.IsNullOrWhiteSpace(virtualPath))
		{
			throw new ArgumentException("A VFS path is required.", nameof(virtualPath));
		}
		EngineApi.WriteFile(virtualPath, bytes ?? throw new ArgumentNullException(nameof(bytes)));
	}
}
