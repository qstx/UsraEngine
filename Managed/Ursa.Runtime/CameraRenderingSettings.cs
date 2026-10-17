using System;

namespace Ursa;

[Ursa.Bindings.WireValue]
public enum CameraType : uint { Game, SceneView, Preview }

[Ursa.Bindings.WireValue]
public enum CameraClearFlags : uint { ColorAndDepth, Depth, Nothing }

[Ursa.Bindings.WireValue]
public struct Viewport
{
	public Viewport(float x, float y, float width, float height) { X = x; Y = y; Width = width; Height = height; }
	public float X { get; set; }
	public float Y { get; set; }
	public float Width { get; set; }
	public float Height { get; set; }
}

[Ursa.Bindings.WireValue]
public sealed class CameraRenderingSettings
{
	public CameraType Type { get; set; } = CameraType.Game;
	public float Depth { get; set; }
	public CameraClearFlags ClearFlags { get; set; } = CameraClearFlags.ColorAndDepth;
	public Color Background { get; set; } = new(0.05f, 0.05f, 0.05f, 1);
	public uint CullingMask { get; set; } = uint.MaxValue;
	public Viewport Viewport { get; set; } = new(0, 0, 1, 1);
	public uint TargetWidth { get; set; }
	public uint TargetHeight { get; set; }

	internal CameraSettingsPayload ToPayload() => new()
	{
		Type = Type, Depth = Depth, ClearFlags = ClearFlags, CullingMask = CullingMask,
		Background = new[] { Background.R, Background.G, Background.B, Background.A },
		Viewport = new[] { Viewport.X, Viewport.Y, Viewport.Width, Viewport.Height },
		TargetWidth = TargetWidth, TargetHeight = TargetHeight
	};

	internal static CameraRenderingSettings FromPayload(CameraSettingsPayload payload)
	{
		if (payload.Background.Length != 4 || payload.Viewport.Length != 4)
			throw new InvalidOperationException("Camera settings have an invalid color/viewport component count.");
		return new CameraRenderingSettings
		{
			Type = payload.Type, Depth = payload.Depth, ClearFlags = payload.ClearFlags, CullingMask = payload.CullingMask,
			Background = new Color(payload.Background[0], payload.Background[1], payload.Background[2], payload.Background[3]),
			Viewport = new Viewport(payload.Viewport[0], payload.Viewport[1], payload.Viewport[2], payload.Viewport[3]),
			TargetWidth = payload.TargetWidth, TargetHeight = payload.TargetHeight
		};
	}
}

internal sealed class CameraSettingsPayload
{
	public CameraType Type { get; set; }
	public float Depth { get; set; }
	public CameraClearFlags ClearFlags { get; set; }
	public uint CullingMask { get; set; }
	public float[] Background { get; set; } = Array.Empty<float>();
	public float[] Viewport { get; set; } = Array.Empty<float>();
	public uint TargetWidth { get; set; }
	public uint TargetHeight { get; set; }
}
