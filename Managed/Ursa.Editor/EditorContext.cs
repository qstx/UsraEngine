using System.Text.Json;

namespace Ursa.Editor;

public static class EditorContext
{
	public static JsonElement Selection => EditorApi.Call("Context").GetProperty("Selection");

	public static JsonElement Document => EditorApi.Call("Context").GetProperty("Document");

	public static JsonElement Assets => EditorApi.Call("Assets");

	public static void Save() => EditorApi.Call("Save");

	public static void MarkDirty() => EditorApi.Call("MarkDirty");

	public static void OpenAsset(string virtualPath) => EditorApi.Call("OpenAsset", new { Path = virtualPath });
}
