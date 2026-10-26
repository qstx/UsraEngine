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
		double editedFieldOfView = EditorGui.DragFloat("Field of View", originalFieldOfView, 0.5, 1, 179, "%.1f°",
			fieldOfView.HasMultipleDifferentValues);
		EditorGui.Text("Clipping Planes");
		double editedNearPlane = EditorGui.DragFloat("Near", originalNearPlane, 0.01, 0.001, 10000, "%.3f",
			nearPlane.HasMultipleDifferentValues);
		double editedFarPlane = EditorGui.DragFloat("Far", originalFarPlane, 0.1, 0.002, 100000, "%.3f",
			farPlane.HasMultipleDifferentValues);

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

[BuiltinEditor]
[CustomEditor(typeof(DirectionalLightComponent))]
[CanEditMultipleObjects]
public sealed class DirectionalLightComponentInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedProperty colorProperty = SerializedObject.FindProperty("color");
		SerializedProperty intensityProperty = SerializedObject.FindProperty("intensity");
		SerializedProperty castsShadowsProperty = SerializedObject.FindProperty("castsShadows");
		SerializedProperty shadowStrengthProperty = SerializedObject.FindProperty("shadowStrength");
		SerializedProperty shadowBiasProperty = SerializedObject.FindProperty("shadowBias");
		SerializedProperty shadowNormalBiasProperty = SerializedObject.FindProperty("shadowNormalBias");
		SerializedProperty softShadowsProperty = SerializedObject.FindProperty("softShadows");

		Color originalColor = colorProperty.ColorValue;
		Color editedColor = EditorGui.ColorField("Color", originalColor, colorProperty.HasMultipleDifferentValues);
		float originalIntensity = intensityProperty.FloatValue;
		double editedIntensity = EditorGui.DragFloat("Intensity", originalIntensity, 0.05, 0, 10000,
			mixedValue: intensityProperty.HasMultipleDifferentValues);
		bool originalCastsShadows = castsShadowsProperty.BoolValue;
		bool editedCastsShadows = EditorGui.Toggle("Casts Shadows", originalCastsShadows,
			castsShadowsProperty.HasMultipleDifferentValues);
		float originalShadowStrength = shadowStrengthProperty.FloatValue;
		double editedShadowStrength = EditorGui.DragFloat("Shadow Strength", originalShadowStrength, 0.01, 0, 1,
			mixedValue: shadowStrengthProperty.HasMultipleDifferentValues);
		float originalShadowBias = shadowBiasProperty.FloatValue;
		double editedShadowBias = EditorGui.DragFloat("Shadow Bias", originalShadowBias, 0.0001, 0, 1,
			mixedValue: shadowBiasProperty.HasMultipleDifferentValues);
		float originalShadowNormalBias = shadowNormalBiasProperty.FloatValue;
		double editedShadowNormalBias = EditorGui.DragFloat("Shadow Normal Bias", originalShadowNormalBias, 0.001, 0,
			10, mixedValue: shadowNormalBiasProperty.HasMultipleDifferentValues);
		bool originalSoftShadows = softShadowsProperty.BoolValue;
		bool editedSoftShadows = EditorGui.Toggle("Soft Shadows (3x3 PCF)", originalSoftShadows,
			softShadowsProperty.HasMultipleDifferentValues);

		bool changed = editedColor.R != originalColor.R || editedColor.G != originalColor.G ||
		               editedColor.B != originalColor.B || editedColor.A != originalColor.A ||
		               editedIntensity != originalIntensity || editedCastsShadows != originalCastsShadows ||
		               editedShadowStrength != originalShadowStrength || editedShadowBias != originalShadowBias ||
		               editedShadowNormalBias != originalShadowNormalBias || editedSoftShadows != originalSoftShadows;
		if (changed)
		{
			colorProperty.ColorValue = editedColor;
			intensityProperty.FloatValue = (float)editedIntensity;
			castsShadowsProperty.BoolValue = editedCastsShadows;
			shadowStrengthProperty.FloatValue = (float)editedShadowStrength;
			shadowBiasProperty.FloatValue = (float)editedShadowBias;
			shadowNormalBiasProperty.FloatValue = (float)editedShadowNormalBias;
			softShadowsProperty.BoolValue = editedSoftShadows;
			SerializedObject.ApplyModifiedProperties();
		}
	}
}

[BuiltinEditor]
[CustomEditor(typeof(PointLightComponent))]
[CanEditMultipleObjects]
public sealed class PointLightComponentInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		SerializedProperty colorProperty = SerializedObject.FindProperty("color");
		SerializedProperty intensityProperty = SerializedObject.FindProperty("intensity");
		SerializedProperty rangeProperty = SerializedObject.FindProperty("range");

		Color originalColor = colorProperty.ColorValue;
		Color editedColor = EditorGui.ColorField("Color", originalColor, colorProperty.HasMultipleDifferentValues);
		float originalIntensity = intensityProperty.FloatValue;
		double editedIntensity = EditorGui.DragFloat("Intensity", originalIntensity, 0.05, 0, 10000,
			mixedValue: intensityProperty.HasMultipleDifferentValues);
		float originalRange = rangeProperty.FloatValue;
		double editedRange = EditorGui.DragFloat("Range", originalRange, 0.05, 0.001, 10000,
			mixedValue: rangeProperty.HasMultipleDifferentValues);

		bool changed = editedColor.R != originalColor.R || editedColor.G != originalColor.G ||
		               editedColor.B != originalColor.B || editedColor.A != originalColor.A ||
	               editedIntensity != originalIntensity || editedRange != originalRange;
		if (changed)
		{
			colorProperty.ColorValue = editedColor;
			intensityProperty.FloatValue = (float)editedIntensity;
			rangeProperty.FloatValue = (float)editedRange;
			SerializedObject.ApplyModifiedProperties();
		}
	}
}
