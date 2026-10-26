using System;
using Ursa;

namespace Ursa.Editor;

[BuiltinEditor]
[CustomEditor(typeof(CameraComponent))]
[CanEditMultipleObjects]
public sealed class CameraComponentInspector : Inspector
{
	private const double RadiansToDegrees = 57.29577951308232;
	private const double DegreesToRadians = 0.017453292519943295;

	public override void OnInspectorGUI()
	{
		SerializedProperty fieldOfView = SerializedObject.FindProperty("verticalFovRadians");
		SerializedProperty nearPlane = SerializedObject.FindProperty("nearPlane");
		SerializedProperty farPlane = SerializedObject.FindProperty("farPlane");

		double originalFieldOfView = (float)(fieldOfView.FloatValue * RadiansToDegrees);
		double originalNearPlane = nearPlane.FloatValue;
		double originalFarPlane = farPlane.FloatValue;
		double editedFieldOfView = EditorGui.DragFloat("Field of View", originalFieldOfView, 0.5, 1, 179, "%.1f°");
		EditorGui.Text("Clipping Planes");
		double editedNearPlane = EditorGui.DragFloat("Near", originalNearPlane, 0.01, 0.001, 10000, "%.3f");
		double editedFarPlane = EditorGui.DragFloat("Far", originalFarPlane, 0.1, 0.002, 100000, "%.3f");

		bool changed = editedFieldOfView != originalFieldOfView ||
		               editedNearPlane != originalNearPlane ||
		               editedFarPlane != originalFarPlane;
		if (changed)
		{
			if (!double.IsFinite(editedFieldOfView) || !double.IsFinite(editedNearPlane) ||
			    !double.IsFinite(editedFarPlane) || editedFieldOfView <= 0 || editedFieldOfView >= 180 ||
			    editedNearPlane <= 0 || editedFarPlane <= editedNearPlane)
			{
				EditorGui.Text("Camera requires 0 < FOV < 180 and 0 < Near < Far");
			}
			else
			{
				fieldOfView.FloatValue = (float)(editedFieldOfView * DegreesToRadians);
				nearPlane.FloatValue = (float)editedNearPlane;
				farPlane.FloatValue = (float)editedFarPlane;
				SerializedObject.ApplyModifiedProperties();
			}
		}

		if (EditorContext.Document?.Kind == EditorDocumentKind.Scene && EditorGui.Button("Use as game camera"))
		{
			EditorContext.SetActiveCamera();
		}
	}
}
