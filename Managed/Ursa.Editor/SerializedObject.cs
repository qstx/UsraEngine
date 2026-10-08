using System.Text.Json;

namespace Ursa.Editor;

public sealed class SerializedObject
{
	private readonly ulong generation = EditorApi.Generation;

	internal SerializedObject() { }

	public SerializedProperty FindProperty(string path)
	{
		EditorApi.RequireGeneration(generation);
		EditorApi.Call("ReadProperty", new { Path = path });
		return new SerializedProperty(path, generation);
	}

	public bool ApplyModifiedProperties()
	{
		EditorApi.RequireGeneration(generation);
		return EditorApi.Call("ApplyProperties").GetBoolean();
	}
}

public sealed class SerializedProperty
{
	private readonly ulong generation;

	internal SerializedProperty(string path, ulong generation) { Path = path; this.generation = generation; }

	public string Path { get; }

	internal void RequireValid() => EditorApi.RequireGeneration(generation);

	public JsonElement Value
	{
		get { RequireValid(); return EditorApi.Call("ReadProperty", new { Path }); }
		set { RequireValid(); EditorApi.Call("WriteProperty", new { Path, Value = value }); }
	}

	public void SetValue(bool value) => Value = JsonSerializer.SerializeToElement(value);

	public void SetValue(long value) => Value = JsonSerializer.SerializeToElement(value);

	public void SetValue(ulong value) => Value = JsonSerializer.SerializeToElement(value);

	public void SetValue(double value) => Value = JsonSerializer.SerializeToElement(value);

	public void SetValue(string? value) => Value = JsonSerializer.SerializeToElement(value);
}
