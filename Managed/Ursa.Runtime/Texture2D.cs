using System;
using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct Texture2DInfo
{
	public uint Width;
	public uint Height;
	public bool Linear;
}

// Import candidates use RGBA8 pixels. Mutation is synchronous and requires their owning invocation.
public sealed class Texture2D : Texture
{
	internal sealed class EditorPixels
	{
		internal required Action RequireValid;
		internal required int Width;
		internal required int Height;
		internal required bool Linear;
		internal required Color[] Pixels;
	}
	[ThreadStatic] internal static Func<int, int, bool, EditorPixels>? EditorFactory;
	internal EditorPixels? EditorStorage { get; private set; }
	public override bool Equals(RenderResource? other) => EditorStorage != null ? ReferenceEquals(this, other) : base.Equals(other);
	public override int GetHashCode() => EditorStorage != null ? System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this) : base.GetHashCode();
	private readonly ulong invocation;
	private Texture2D(AssetReference reference, NativeHandle handle) : base(reference, handle) { }
	internal static Texture2D FromImportHandle(NativeHandle handle) => new(handle);
	internal static Texture2D FromEditorHandle(AssetReference reference, NativeHandle handle) => new(reference, handle);
	private Texture2D(NativeHandle handle) : base(default, handle) { invocation = ImportExecution.Current.Invocation; NativeImportBindings.RequireCandidate(handle); }
	public new static Texture2D Load(AssetReference reference) => new(reference, EngineApi.LoadTexture(reference.Guid, reference.LocalFileId));
	public Texture2D(int width, int height, bool linear = false)
		: base(default, Create(width, height, linear))
	{
		if (ImportExecution.Active) invocation = ImportExecution.Current.Invocation;
		else EditorStorage = EditorFactory!(width, height, linear);
	}
	private static NativeHandle Create(int width, int height, bool linear)
	{
		if (width <= 0 || height <= 0) throw new ArgumentOutOfRangeException(nameof(width), "Texture dimensions must be positive.");
		if (!ImportExecution.Active && EditorFactory != null) return default;
		_ = ImportExecution.Current;
		return NativeImportBindings.CreateTexture2D(checked((uint)width), checked((uint)height), linear);
	}
	private void RequireOwner()
	{
		if (EditorStorage is { } editor) { editor.RequireValid(); return; }
		if (invocation == 0)
		{
			if (!base.IsValid) throw new ObjectDisposedException(nameof(Texture2D));
			return;
		}
		if (!ImportExecution.Active || ImportExecution.Current.Invocation != invocation)
			throw new ObjectDisposedException(nameof(Texture2D), "Texture belongs to another or completed import invocation.");
		NativeImportBindings.RequireCandidate(Handle);
	}
	public override bool IsValid
	{
		get
		{
			if (EditorStorage is { } editor)
			{
				try { editor.RequireValid(); return true; } catch (InvalidOperationException) { return false; }
			}
			if (invocation == 0) return base.IsValid;
			if (!ImportExecution.Active || ImportExecution.Current.Invocation != invocation) return false;
			try { NativeImportBindings.RequireCandidate(Handle); return true; }
			catch (InvalidOperationException) { return false; }
		}
	}
	private Texture2DInfo Info { get { RequireOwner(); return EditorStorage is { } editor ? new() { Width = checked((uint)editor.Width), Height = checked((uint)editor.Height), Linear = editor.Linear }
		: invocation == 0 ? NativeResourceBindings.GetTextureInfo(Handle) : NativeImportBindings.GetTexture2DInfo(Handle); } }
	public int Width => checked((int)Info.Width);
	public int Height => checked((int)Info.Height);
	public bool IsLinear => Info.Linear;
	public Color[] GetPixels() { RequireOwner(); return EditorStorage is { } editor ? (Color[])editor.Pixels.Clone() : invocation == 0 ? NativeResourceBindings.GetTexturePixels(Handle) : NativeImportBindings.GetTexture2DPixels(Handle); }
	public void SetPixels(Color[] pixels)
	{
		RequireOwner();
		if (EditorStorage is { } editor)
		{
			ArgumentNullException.ThrowIfNull(pixels);
			if (pixels.Length != editor.Pixels.Length) throw new ArgumentException("Pixel count differs from texture dimensions.", nameof(pixels));
			foreach (Color color in pixels)
				if (!float.IsFinite(color.R) || !float.IsFinite(color.G) || !float.IsFinite(color.B) || !float.IsFinite(color.A))
					throw new ArgumentException("Texture colors must be finite.", nameof(pixels));
			editor.Pixels = (Color[])pixels.Clone();
			return;
		}
		if (invocation == 0) throw new InvalidOperationException("Loaded texture assets are read-only; create an Import candidate to modify pixels.");
		NativeImportBindings.SetTexture2DPixels(Handle, pixels ?? throw new ArgumentNullException(nameof(pixels)));
	}
}
