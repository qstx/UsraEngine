using System;
using System.Runtime.CompilerServices;
using Ursa;

namespace Ursa.Rendering;

// Host camera data is independent of a scene component, including editor/preview cameras.
public sealed class Camera
{
	private static readonly ConditionalWeakTable<CameraComponent, Camera> wrappers = new();
	private CameraRenderingSettings settings = new();
	private readonly string identity;
	private readonly ulong scriptSession = BehaviourRegistry.currentSession;
	private ulong outputIdentity;
	private ulong outputGeneration;
	private bool outputIsSrgb;
	private uint pixelWidth;
	private uint pixelHeight;
	private Matrix4x4 view = Matrix4x4.Identity;
	private Matrix4x4 projection = Matrix4x4.Identity;
	private Matrix4x4 inverseViewProjection = Matrix4x4.Identity;
	private Vector3 position;
	private float nearPlane = 0.1f;
	private float farPlane = 100.0f;
	private bool orthographic;
	private CameraRenderingSettings Settings { get { RequireValid(); return settings; } }
	internal string PersistentId
	{
		get
		{
			RequireValid();
			if (Component != null) _ = Component.PersistentId;
			return identity;
		}
	}
	public CameraComponent? Component { get; }
	public CameraType Type => Settings.Type;
	public float Depth => Settings.Depth;
	public CameraClearFlags ClearFlags => Settings.ClearFlags;
	public Color Background => Settings.Background;
	public uint CullingMask => Settings.CullingMask;
	public Viewport Viewport => Settings.Viewport;
	public uint PixelWidth { get { RequireValid(); return pixelWidth; } }
	public uint PixelHeight { get { RequireValid(); return pixelHeight; } }
	public Matrix4x4 ViewMatrix { get { RequireValid(); return view; } }
	public Matrix4x4 ProjectionMatrix { get { RequireValid(); return projection; } }
	public Matrix4x4 InverseViewProjectionMatrix { get { RequireValid(); return inverseViewProjection; } }
	public Vector3 Position { get { RequireValid(); return position; } }
	public float NearPlane { get { RequireValid(); return nearPlane; } }
	public float FarPlane { get { RequireValid(); return farPlane; } }
	public bool Orthographic { get { RequireValid(); return orthographic; } }
	public ulong OutputTargetIdentity { get { RequireValid(); return outputIdentity; } }
	public ulong OutputTargetGeneration { get { RequireValid(); return outputGeneration; } }
	public bool OutputIsSrgb { get { RequireValid(); return outputIsSrgb; } }

	private Camera(CameraComponent component)
	{
		Component = component;
		identity = component.PersistentId;
	}
	internal Camera(string identity) => this.identity = identity;
	public static Camera FromComponent(CameraComponent component)
	{
		ArgumentNullException.ThrowIfNull(component);
		ScriptableObjectRegistry.RequireThread();
		if (wrappers.TryGetValue(component, out Camera? previous) && previous.scriptSession != BehaviourRegistry.currentSession)
			wrappers.Remove(component);
		return wrappers.GetValue(component, value => new Camera(value));
	}
	public static implicit operator Camera(CameraComponent component) => FromComponent(component);
	internal void Update(CameraFrameValue frame)
	{
		RequireValid();
		settings = frame.Settings;
		outputIdentity = frame.OutputIdentity;
		outputGeneration = frame.OutputGeneration;
		outputIsSrgb = frame.OutputIsSrgb;
		pixelWidth = frame.Width;
		pixelHeight = frame.Height;
		view = frame.View;
		projection = frame.Projection;
		inverseViewProjection = frame.InverseViewProjection;
		position = frame.Position;
		nearPlane = frame.NearPlane;
		farPlane = frame.FarPlane;
		orthographic = frame.Orthographic;
	}

	private void RequireValid()
	{
		ScriptableObjectRegistry.RequireThread();
		if (scriptSession != BehaviourRegistry.currentSession)
			throw new ObjectDisposedException(nameof(Camera), "Camera belongs to another or closed script session.");
	}
}

[Ursa.Bindings.WireValue]
internal sealed class CameraFrameValue
{
	public string Identity { get; set; } = string.Empty;
	public ulong OutputIdentity { get; set; }
	public ulong OutputGeneration { get; set; }
	public bool OutputIsSrgb { get; set; }
	public uint Width { get; set; }
	public uint Height { get; set; }
	public Matrix4x4 View { get; set; } = Matrix4x4.Identity;
	public Matrix4x4 Projection { get; set; } = Matrix4x4.Identity;
	public Matrix4x4 InverseViewProjection { get; set; } = Matrix4x4.Identity;
	public Vector3 Position { get; set; }
	public float NearPlane { get; set; } = 0.1f;
	public float FarPlane { get; set; } = 100.0f;
	public bool Orthographic { get; set; }
	public CameraRenderingSettings Settings { get; set; } = new();
}
