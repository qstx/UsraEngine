using System;
using System.Text.Json;
using System.Collections.Generic;

namespace Ursa.Editor;

public class AssetImporterEditor : Inspector
{
	private AssetImporter? target;
	private ulong generation;
	public override AssetImporter Target
	{
		get
		{
			EditorApi.EnsureRuntimeScope();
			ulong current = EditorApi.Generation;
			if (generation != current)
			{
				generation = current;
				target = EditorImporterScope.Create(SerializedObject);
			}
			return target!;
		}
	}
	internal override void SynchronizeTarget()
	{
		if (generation != EditorApi.Generation || target is not ScriptedImporter) return;
		target.EditorScope!.SynchronizeTarget();
	}
	public bool HasModified() => SerializedObject.HasModifiedProperties || NativeEditorBindings.HasModifiedImporterSettings();
	public bool Apply() { SynchronizeTarget(); SerializedObject.ApplyModifiedProperties(); return NativeEditorBindings.ApplyImporterSettings(); }
	public void Revert() { NativeEditorBindings.RevertImporterSettings(); SerializedObject.Update(); target = null; generation = 0; }
	protected void ApplyRevertGUI()
	{
		SynchronizeTarget();
		SerializedObject.ApplyModifiedProperties();
		if (HasModified())
		{
			if (EditorGui.Button("Revert")) Revert();
			EditorGui.SameLine();
			if (NativeEditorBindings.CanApplyImporterSettings() && EditorGui.Button("Apply")) Apply();
		}
		NativeEditorBindings.MarkImporterControlsDrawn();
		SerializedObject.Update();
		target = null; generation = 0;
	}
	public override void OnInspectorGUI() { DrawDefaultInspector(); ApplyRevertGUI(); }
}

public class ScriptedImporterEditor : AssetImporterEditor { }

internal sealed class EditorImporterScope(EditorImporterInfo info, SerializedObject properties)
{
	private readonly ulong generation = EditorApi.Generation;
	private ScriptedImporter? scripted;
	private string baseline = string.Empty;
	internal EditorImporterInfo Info => info;
	internal void RequireValid() => EditorApi.RequireGeneration(generation);
	private SerializedProperty Field(string name) { RequireValid(); return properties.FindProperty(name); }
	internal bool Apply() { RequireValid(); SynchronizeTarget(); properties.ApplyModifiedProperties(); return NativeEditorBindings.ApplyImporterSettings(); }
	internal void SynchronizeTarget()
	{
		RequireValid();
		if (scripted == null) return;
		using var payload = JsonDocument.Parse(ScriptFieldSchema.Serialize(scripted));
		string current = payload.RootElement.GetProperty("Fields").GetRawText();
		if (current == baseline) return;
		using var previous = JsonDocument.Parse(baseline);
		var changed = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
		foreach (JsonProperty field in payload.RootElement.GetProperty("Fields").EnumerateObject())
			if (!previous.RootElement.TryGetProperty(field.Name, out JsonElement old) || old.GetRawText() != field.Value.GetRawText()) changed.Add(field.Name, field.Value);
		if (changed.Count != 0) NativeEditorBindings.ApplyTargetFields(JsonSerializer.Serialize(changed));
		baseline = current;
	}
	internal static AssetImporter Create(SerializedObject properties)
	{
		var info = NativeEditorBindings.GetImporterInfo() ?? throw new InvalidOperationException("No importer Inspector target is active.");
		var scope = new EditorImporterScope(info, properties);
		if (info.Identity == "ursa.Texture") return new TextureImporter(scope);
		if (info.Identity == "ursa.Model") return new ModelImporter(scope);
		Type type = BehaviourRegistry.ResolveType(info.ClrTypeName) ?? throw new InvalidOperationException("Scripted importer target type is unavailable.");
		if (Activator.CreateInstance(type) is not ScriptedImporter importer) throw new InvalidOperationException("Importer target is not a ScriptedImporter.");
		importer.BindEditor(scope);
		ScriptFieldSchema.Deserialize(importer, NativeEditorBindings.GetTargetPayload());
		scope.scripted = importer;
		using (var initial = JsonDocument.Parse(ScriptFieldSchema.Serialize(importer))) scope.baseline = initial.RootElement.GetProperty("Fields").GetRawText();
		return importer;
	}
	internal TextureImporterSettingsValue ReadTexture() => new()
	{
		Type = checked((uint)Field("type").EnumLongValue), SRgb = Field("sRgb").BoolValue, Alpha = Field("alpha").BoolValue,
		GenerateMipmaps = Field("generateMipmaps").BoolValue, Filter = checked((uint)Field("filter").EnumLongValue),
		Wrap = checked((uint)Field("wrap").EnumLongValue), MaxDimension = Field("maxDimension").UIntValue
	};
	internal void WriteTexture(TextureImporterSettingsValue value)
	{
		Field("type").EnumLongValue = value.Type; Field("sRgb").BoolValue = value.SRgb; Field("alpha").BoolValue = value.Alpha;
		Field("generateMipmaps").BoolValue = value.GenerateMipmaps; Field("filter").EnumLongValue = value.Filter;
		Field("wrap").EnumLongValue = value.Wrap; Field("maxDimension").UIntValue = value.MaxDimension;
		properties.ApplyModifiedProperties();
	}
	internal ModelImporterSettingsValue ReadModel() => new()
	{
		ScaleFactor = Field("scaleFactor").FloatValue, GenerateNormals = Field("generateNormals").BoolValue,
		GenerateTangents = Field("generateTangents").BoolValue, FlipUvs = Field("flipUvs").BoolValue,
		ImportMaterials = Field("importMaterials").BoolValue, ImportTextures = Field("importTextures").BoolValue,
		MaxBoneInfluences = Field("maxBoneInfluences").UIntValue
	};
	internal void WriteModel(ModelImporterSettingsValue value)
	{
		Field("scaleFactor").FloatValue = value.ScaleFactor; Field("generateNormals").BoolValue = value.GenerateNormals;
		Field("generateTangents").BoolValue = value.GenerateTangents; Field("flipUvs").BoolValue = value.FlipUvs;
		Field("importMaterials").BoolValue = value.ImportMaterials; Field("importTextures").BoolValue = value.ImportTextures;
		Field("maxBoneInfluences").UIntValue = value.MaxBoneInfluences;
		properties.ApplyModifiedProperties();
	}
}
