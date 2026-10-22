using System;
using Ursa.Bindings;

namespace Ursa.Editor;

internal sealed class PreviewLifetime
{
	[ThreadStatic] internal static PreviewLifetime? Current;
	internal bool Valid = true;
	private readonly int thread = Environment.CurrentManagedThreadId;
	internal void RequireValid()
	{
		_ = EditorApi.Generation;
		if (!Valid || !ReferenceEquals(Current, this) || thread != Environment.CurrentManagedThreadId)
			throw new InvalidOperationException("Texture belongs to a closed or different Preview instance.");
	}
	internal static Texture2D.EditorPixels CreateTexture(int width, int height, bool linear)
	{
		var owner = Current ?? throw new InvalidOperationException("Editor textures require an active Preview instance.");
		owner.RequireValid();
		if ((long)width * height > 16_777_216) throw new ArgumentOutOfRangeException(nameof(width), "Preview texture exceeds the pixel budget.");
		return new() { RequireValid = owner.RequireValid, Width = width, Height = height, Linear = linear, Pixels = new Color[checked(width * height)] };
	}
}

[WireValue]
public struct PreviewCamera
{
	public Vector3 Position;
	public Vector3 Target;
	public Vector3 Up;
	public float FieldOfView;
	public float NearPlane;
	public float FarPlane;
	public PreviewCamera() { Position = new(0, 0, 3); Target = default; Up = new(0, 1, 0); FieldOfView = 45; NearPlane = .1f; FarPlane = 100; }
}

[WireValue]
public struct PreviewInput
{
	public Vector2 MousePosition;
	public Vector2 MouseDelta;
	public float ScrollDelta;
	public bool Dragging;
	public double Time;
	public double DeltaTime;
	public static PreviewInput Current => NativeEditorBindings.GetPreviewInput();
}

// Owns private viewing objects until OnDisable/Cleanup; no object is added to an author Scene.
public sealed class PreviewRenderUtility : IDisposable
{
	private ulong handle;
	public PreviewCamera Camera = new();
	public Color AmbientColor = new(1, 1, 1);
	public float AmbientIntensity = .4f;
	public Color LightColor = new(1, 1, 1);
	public float LightIntensity = 2;
	public Quaternion LightRotation = new(0, 0, 0, 1);
	public PreviewRenderUtility() { _ = EditorApi.Generation; handle = NativeEditorBindings.CreatePreviewUtility(); }
	private ulong Handle { get { _ = EditorApi.Generation; return handle != 0 ? handle : throw new ObjectDisposedException(nameof(PreviewRenderUtility)); } }
	public void BeginPreview(Rect rect)
	{
		NativeEditorBindings.BeginPreview(Handle, rect);
		NativeEditorBindings.SetPreviewCamera(Handle, Camera);
		NativeEditorBindings.SetPreviewLighting(Handle, AmbientColor, AmbientIntensity, LightColor, LightIntensity, LightRotation);
	}
	public void DrawMesh(Mesh mesh, Material material, Vector3 position = default, Quaternion? rotation = null, Vector3? scale = null)
	{
		ArgumentNullException.ThrowIfNull(mesh); ArgumentNullException.ThrowIfNull(material);
		EditorApi.EnsureRuntimeScope();
		NativeEditorBindings.PreviewDrawMesh(Handle, mesh.Handle, material.Handle, position, rotation ?? new(0, 0, 0, 1), scale ?? new(1, 1, 1));
	}
	public void InstantiatePrefab(AssetReference prefab) => NativeEditorBindings.PreviewInstantiatePrefab(Handle, prefab.Guid, prefab.LocalFileId);
	public void EndPreview() => NativeEditorBindings.EndPreview(Handle);
	public void Cleanup()
	{
		if (handle == 0) return;
		NativeEditorBindings.DestroyPreviewUtility(Handle);
		handle = 0;
	}
	public void Dispose() => Cleanup();
}
