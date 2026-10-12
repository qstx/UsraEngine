using System;
using System.Text;
using System.Text.Json;

namespace Ursa;

internal static unsafe partial class EngineApi
{
	internal static readonly JsonSerializerOptions ResourceJson = new()
	{
		IncludeFields = true,
		PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
		PropertyNameCaseInsensitive = true
	};

	internal static JsonElement ResourceRequest(object request)
	{
		RequireInitialized();
		if (api.resourceRequest == null || api.sceneContext == 0)
		{
			throw new InvalidOperationException("Resource APIs require the version 5 Scene API.");
		}
		byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(request, ResourceJson);
		if (bytes.LongLength > 512L * 1024 * 1024)
		{
			throw new InvalidOperationException("Resource request exceeds the transfer capacity.");
		}
		ulong required = 0;
		fixed (byte* input = bytes)
		{
			int status = api.resourceRequest(api.sceneContext, input, (ulong)bytes.Length, null, 0, &required);
			if (status != 0 && status != 4) Check(status, "execute resource request");
		}
		if (required > 512UL * 1024 * 1024) throw new InvalidOperationException("Resource response exceeds capacity.");
		byte[] output = new byte[checked((int)required)];
		fixed (byte* data = output)
		{
			// Retrieve the completed response without executing the request twice.
			Check(api.resourceRequest(api.sceneContext, null, 0, data, required, &required), "read resource response");
		}
		using JsonDocument document = JsonDocument.Parse(output);
		if (document.RootElement.TryGetProperty("error", out var error))
		{
			throw new InvalidOperationException(error.GetString());
		}
		return document.RootElement.GetProperty("value").Clone();
	}

	internal static T ResourceRequest<T>(object request)
	{
		var value = ResourceRequest(request);
		return value.ValueKind == JsonValueKind.Null ? default! : value.Deserialize<T>(ResourceJson)!;
	}

	internal static bool ResourceValid(NativeHandle handle)
	{
		try { return ResourceRequest<bool>(new { operation = "resource.valid", handle }); }
		catch (InvalidOperationException) { return false; }
	}
}
