using System;
using Ursa;

namespace Ursa.Editor;

internal static class BuiltinMeshInspectorGui
{
	private static string assetPath = "prj:Meshes/NewMesh.ursamesh";
	private static string diagnostic = string.Empty;
	private static bool replaceBinding = true;

	internal static void DrawDetails(Mesh? mesh)
	{
		if (mesh is not { IsValid: true }) return;
		MeshData data = mesh.GetData();
		EditorGui.Text("Vertices: " + data.Positions.Length + "  Indices: " + data.Indices.Length +
		               "  Submeshes: " + data.SubMeshes.Length);
		EditorGui.Text("Version: " + mesh.Version + "  " + (mesh.AssetIdentity.HasValue ? "Asset" : "Local instance"));
		EditorGui.Text("Bounds: " + data.Bounds.Minimum + " / " + data.Bounds.Maximum);
		assetPath = EditorGui.TextField("Asset path", assetPath);
		replaceBinding = EditorGui.Toggle("Replace this binding", replaceBinding);
		if (EditorGui.Button("Save Mesh As..."))
		{
			try
			{
				EditorContext.SaveMeshComponentAs(assetPath, replaceBinding);
				diagnostic = string.Empty;
			}
			catch (Exception error)
			{
				diagnostic = error.Message;
			}
		}
		if (!string.IsNullOrEmpty(diagnostic)) EditorGui.Text(diagnostic);
	}
}

[BuiltinEditor]
[CustomEditor(typeof(GameObject))]
[CanEditMultipleObjects]
public sealed class GameObjectTransformInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		EditorGui.PropertyField(SerializedObject.FindProperty("position"), "Position");
		EditorGui.EulerAnglesField("Rotation", SerializedObject.FindProperty("rotation"));
		EditorGui.PropertyField(SerializedObject.FindProperty("scale"), "Scale");
		SerializedObject.ApplyModifiedProperties();
	}
}

[BuiltinEditor]
[CustomEditor(typeof(MeshFilterComponent))]
[CanEditMultipleObjects]
public sealed class MeshFilterComponentInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		EditorGui.PropertyField(SerializedObject.FindProperty("mesh"), "Mesh");
		SerializedObject.ApplyModifiedProperties();
		if (!SerializedObject.IsEditingMultipleObjects && Target is MeshFilterComponent filter)
			BuiltinMeshInspectorGui.DrawDetails(filter.SharedMesh);
	}
}

[BuiltinEditor]
[CustomEditor(typeof(MeshRendererComponent))]
[CanEditMultipleObjects]
public sealed class MeshRendererComponentInspector : Inspector
{
	internal static void DrawMaterials(RendererComponent renderer)
	{
		Material?[] materials = renderer.SharedMaterials;
		for (int index = 0; index < materials.Length; ++index)
		{
			Material? material = materials[index];
			if (material == null) continue;
			AssetReference? asset = material.AssetIdentity;
			if (asset != null)
			{
				EditorGui.Text("Asset material");
				if (EditorGui.Button("Make Local Copy##material" + index))
				{
					material = material.Clone();
					materials[index] = material;
					renderer.SharedMaterials = materials;
				}
			}
			if (material.AssetIdentity == null)
				EditorGui.DrawMaterialProperties(material, "Material " + index);
		}
	}

	public override void OnInspectorGUI()
	{
		EditorGui.PropertyField(SerializedObject.FindProperty("castShadows"), "Cast Shadows");
		EditorGui.PropertyField(SerializedObject.FindProperty("receiveShadows"), "Receive Shadows");
		EditorGui.Separator();
		EditorGui.PropertyField(SerializedObject.FindProperty("materials"), "Materials");
		SerializedObject.ApplyModifiedProperties();
		if (SerializedObject.IsEditingMultipleObjects) return;
		if (Target is not RendererComponent renderer) return;
		DrawMaterials(renderer);
	}
}

[BuiltinEditor]
[CustomEditor(typeof(SkinnedMeshRendererComponent))]
[CanEditMultipleObjects]
public sealed class SkinnedMeshRendererComponentInspector : Inspector
{
	public override void OnInspectorGUI()
	{
		EditorGui.PropertyField(SerializedObject.FindProperty("mesh"), "Mesh");
		EditorGui.PropertyField(SerializedObject.FindProperty("castShadows"), "Cast Shadows");
		EditorGui.PropertyField(SerializedObject.FindProperty("receiveShadows"), "Receive Shadows");
		EditorGui.Separator();
		EditorGui.PropertyField(SerializedObject.FindProperty("materials"), "Materials");
		SerializedObject.ApplyModifiedProperties();
		if (SerializedObject.IsEditingMultipleObjects || Target is not RendererComponent renderer) return;
		MeshRendererComponentInspector.DrawMaterials(renderer);
		if (Target is SkinnedMeshRendererComponent skinned)
			BuiltinMeshInspectorGui.DrawDetails(skinned.SharedMesh);
	}
}

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
		SerializedProperty orthographic = SerializedObject.FindProperty("orthographic");
		SerializedProperty orthographicSize = SerializedObject.FindProperty("orthographicSize");
		SerializedProperty nearPlane = SerializedObject.FindProperty("nearPlane");
		SerializedProperty farPlane = SerializedObject.FindProperty("farPlane");

		int originalProjection = orthographic.BoolValue ? 1 : 0;
		int selectedProjection = EditorGui.Popup("Projection",
			orthographic.HasMultipleDifferentValues ? -1 : originalProjection,
			new[] { "Perspective", "Orthographic" }, orthographic.HasMultipleDifferentValues);
		bool projectionChanged = selectedProjection >= 0 &&
		                         (orthographic.HasMultipleDifferentValues || selectedProjection != originalProjection);
		bool editedOrthographic = selectedProjection >= 0 ? selectedProjection == 1 : orthographic.BoolValue;
		bool showProjectionSpecific = !orthographic.HasMultipleDifferentValues || selectedProjection >= 0;

		double originalFieldOfView = (float)(fieldOfView.FloatValue * RadiansToDegrees);
		double originalOrthographicSize = orthographicSize.FloatValue;
		double originalNearPlane = nearPlane.FloatValue;
		double originalFarPlane = farPlane.FloatValue;
		double editedFieldOfView = originalFieldOfView;
		double editedOrthographicSize = originalOrthographicSize;
		if (showProjectionSpecific && editedOrthographic)
		{
			editedOrthographicSize = EditorGui.DragFloat("Size", originalOrthographicSize, 0.05, 0.001, 100000,
				mixedValue: orthographicSize.HasMultipleDifferentValues);
		}
		else if (showProjectionSpecific)
		{
			editedFieldOfView = EditorGui.DragFloat("Field of View", originalFieldOfView, 0.5, 1, 179, "%.1f°",
				fieldOfView.HasMultipleDifferentValues);
		}
		EditorGui.Text("Clipping Planes");
		double editedNearPlane = EditorGui.DragFloat("Near", originalNearPlane, 0.01, 0.001, 10000, "%.3f",
			nearPlane.HasMultipleDifferentValues);
		double editedFarPlane = EditorGui.DragFloat("Far", originalFarPlane, 0.1, 0.002, 100000, "%.3f",
			farPlane.HasMultipleDifferentValues);

		bool changed = projectionChanged || editedFieldOfView != originalFieldOfView ||
		               editedOrthographicSize != originalOrthographicSize ||
		               editedNearPlane != originalNearPlane ||
		               editedFarPlane != originalFarPlane;
		if (changed)
		{
			if ((showProjectionSpecific && !editedOrthographic &&
			     (!double.IsFinite(editedFieldOfView) || editedFieldOfView <= 0 || editedFieldOfView >= 180)) ||
			    (showProjectionSpecific && editedOrthographic &&
			     (!double.IsFinite(editedOrthographicSize) || editedOrthographicSize <= 0)) ||
			    !double.IsFinite(editedNearPlane) || !double.IsFinite(editedFarPlane) || editedNearPlane <= 0 ||
			    editedFarPlane <= editedNearPlane)
			{
				EditorGui.Text("Camera requires a valid projection and 0 < Near < Far");
			}
			else
			{
				if (projectionChanged)
				{
					orthographic.BoolValue = editedOrthographic;
				}
				if (showProjectionSpecific && editedOrthographic &&
				    editedOrthographicSize != originalOrthographicSize)
				{
					orthographicSize.FloatValue = (float)editedOrthographicSize;
				}
				else if (showProjectionSpecific && !editedOrthographic && editedFieldOfView != originalFieldOfView)
				{
					fieldOfView.FloatValue = (float)(editedFieldOfView * DegreesToRadians);
				}
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
