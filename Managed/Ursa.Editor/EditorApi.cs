using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace Ursa.Editor;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeEditorApi
{
	internal uint version;
	internal uint size;
	internal nint context;
	internal delegate* unmanaged[Cdecl]<nint, byte*, ulong, byte*, ulong, ulong*, int> exchange;
}

internal static unsafe class EditorApi
{
	[ThreadStatic] private static NativeEditorApi api;
	[ThreadStatic] private static bool active;
	[ThreadStatic] private static ulong generation;

	internal static ulong Generation => active ? generation :
		throw new InvalidOperationException("Editor API is available only inside a main-thread extension callback.");

	internal static void Begin(nint callbacks, int size)
	{
		if (active || callbacks == 0 || size < sizeof(NativeEditorApi)) throw new InvalidOperationException("Invalid or reentrant Editor API scope.");
		api = *(NativeEditorApi*)callbacks;
		if (api.version != 1 || api.size != sizeof(NativeEditorApi) || api.exchange == null || api.context == 0)
			throw new InvalidOperationException("Editor API layout/version mismatch.");
		active = true;
		++generation;
	}

	internal static void End()
	{
		active = false;
		api = default;
	}

	internal static void RequireGeneration(ulong expected)
	{
		if (Generation != expected) throw new InvalidOperationException("Serialized editor target belongs to a previous callback.");
	}

	internal static JsonElement Call(string operation, object? parameters = null)
	{
		_ = Generation;
		byte[] request = JsonSerializer.SerializeToUtf8Bytes(new { Operation = operation, Parameters = parameters });
		fixed (byte* input = request)
		{
			ulong required = 0;
			int status = api.exchange(api.context, input, (ulong)request.Length, null, 0, &required);
			if (status != 4 || required == 0 || required > 16ul * 1024 * 1024)
				throw new InvalidOperationException($"Editor operation '{operation}' failed ({status}).");
			byte[] output = new byte[(int)required];
			fixed (byte* result = output)
			{
				status = api.exchange(api.context, input, (ulong)request.Length, result, required, &required);
			}
			if (status != 0) throw new InvalidOperationException($"Editor operation '{operation}' failed ({status}).");
			using JsonDocument document = JsonDocument.Parse(output);
			if (document.RootElement.ValueKind == JsonValueKind.Object && document.RootElement.TryGetProperty("Error", out JsonElement error)) throw new InvalidOperationException(error.GetString());
			return document.RootElement.Clone();
		}
	}
}
