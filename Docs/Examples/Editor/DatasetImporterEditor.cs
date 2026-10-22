using Ursa.Editor;

namespace Ursa.Examples;

[CustomEditor(typeof(DatasetImporter))]
public sealed class DatasetImporterEditor : ScriptedImporterEditor
{
	public override void OnInspectorGUI()
	{
		var importer = (DatasetImporter)Target;
		EditorGui.Text(importer.AssetPath);
		SerializedObject.Update();
		EditorGui.PropertyField(SerializedObject.FindProperty("scale"), "Scale");
		SerializedObject.ApplyModifiedProperties();
		ApplyRevertGUI();
	}
}
