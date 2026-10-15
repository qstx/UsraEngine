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
	private uint pixelWidth;
	private uint pixelHeight;
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
	internal void Update(CameraFramePayload frame)
	{
		RequireValid();
		settings = CameraRenderingSettings.FromPayload(frame.Settings);
		pixelWidth = frame.Width;
		pixelHeight = frame.Height;
	}

	private void RequireValid()
	{
		ScriptableObjectRegistry.RequireThread();
		if (scriptSession != BehaviourRegistry.currentSession)
			throw new ObjectDisposedException(nameof(Camera), "Camera belongs to another or closed script session.");
	}
}

internal sealed class CameraFramePayload
{
	public string Identity { get; set; } = string.Empty;
	public uint Width { get; set; }
	public uint Height { get; set; }
	public CameraSettingsPayload Settings { get; set; } = new();
}
