using System;
using System.Collections.Generic;
using System.Text;
using Ursa.Bindings;
using Ursa.Bindings.Generated;

namespace Ursa.Rendering;

public sealed class CommandBuffer : IDisposable
{
	public const int MaximumGlobalParameterArrayElements = 16;

	private readonly BindingCommandWriter writer = new(RenderingCommands.Hash);
	private readonly List<RenderResource> recordedResources = new();
	private readonly List<string> sampleNames = new();
	private uint recordingGeneration;
	private readonly HashSet<uint> temporaryTargetIds = new();
	private int activeTemporaryScopes;
	private bool disposed;
	private string name;

	public CommandBuffer(string name = "") => this.name = name ?? throw new ArgumentNullException(nameof(name));

	public string Name
	{
		get => name;
		set { EnsureAlive(); name = value ?? throw new ArgumentNullException(nameof(value)); }
	}

	public void SetRenderTarget(uint targetId) => SetRenderTarget(new RenderTargetIdentifier(targetId));
	public void SetRenderTarget(RenderTexture target)
	{
		ArgumentNullException.ThrowIfNull(target);
		SetRenderTarget(new RenderTargetIdentifier(target));
	}

	public void SetRenderTarget(RenderTargetIdentifier target)
	{
		EnsureAlive();
		RenderCommands.SetRenderTarget(writer, Target(target));
		Retain(target);
	}

	public void SetRenderTargets(ReadOnlySpan<RenderTargetIdentifier> colors, RenderTargetIdentifier? depth = null)
	{
		EnsureAlive();
		if (colors.Length > 8 || (colors.Length == 0 && depth == null))
			throw new ArgumentOutOfRangeException(nameof(colors), "A target set requires one to eight colors or a depth target.");
		var targets = new RenderTargetValue[colors.Length];
		for (int index = 0; index < colors.Length; ++index) targets[index] = Target(colors[index]);
		RenderCommands.SetRenderTargets(writer, targets, depth.HasValue ? Target(depth.Value) : null);
		foreach (var target in colors) Retain(target);
		if (depth.HasValue) Retain(depth.Value);
	}

	public void ClearTarget(bool color, bool depth, bool stencil, Vector4 colorValue, float depthValue = 1, uint stencilValue = 0)
	{
		EnsureAlive();
		if (!color && !depth && !stencil) throw new ArgumentException("At least one target clear aspect must be selected.");
		Finite(colorValue);
		if (!float.IsFinite(depthValue) || depthValue < 0 || depthValue > 1) throw new ArgumentOutOfRangeException(nameof(depthValue));
		RenderCommands.ClearTarget(writer, (color ? 1u : 0u) | (depth ? 2u : 0u) | (stencil ? 4u : 0u),
			new Color(colorValue.X, colorValue.Y, colorValue.Z, colorValue.W), depthValue, stencilValue);
	}

	public void SetViewport(float x, float y, float width, float height, float minimumDepth = 0, float maximumDepth = 1)
	{
		EnsureAlive();
		if (!float.IsFinite(x) || !float.IsFinite(y) || !float.IsFinite(width) || !float.IsFinite(height) ||
			!float.IsFinite(minimumDepth) || !float.IsFinite(maximumDepth) || width <= 0 || height <= 0 ||
			minimumDepth < 0 || maximumDepth > 1 || minimumDepth > maximumDepth) throw new ArgumentOutOfRangeException(nameof(width));
		RenderCommands.SetViewport(writer, x, y, width, height, minimumDepth, maximumDepth);
	}

	public void SetScissor(int x, int y, uint width, uint height)
	{
		EnsureAlive();
		if (x < 0 || y < 0 || width == 0 || height == 0) throw new ArgumentOutOfRangeException(nameof(width));
		RenderCommands.SetScissor(writer, x, y, width, height);
	}

	public void DrawRendererList(RendererList list, ScriptableRenderContext context)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(context);
		context.ValidateRendererList(list);
		RenderCommands.DrawRendererList(writer, new RenderResultValue { Context = list.Context, Index = list.Index, Generation = list.Generation });
	}

	public void DrawMesh(Mesh mesh, Material material, Matrix4x4 localToWorld, uint subMesh = 0, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(mesh);
		ArgumentNullException.ThrowIfNull(material);
		Finite(localToWorld);
		Pass(pass);
		RenderCommands.DrawMesh(writer, mesh.Handle, material.Handle, localToWorld, subMesh, pass);
		recordedResources.Add(mesh);
		recordedResources.Add(material);
	}

	public void CopyTexture(RenderTexture source, RenderTexture destination)
	{
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(destination);
		CopyTexture(new RenderTargetIdentifier(source), new RenderTargetIdentifier(destination));
	}

	public void CopyTexture(RenderTargetIdentifier source, RenderTargetIdentifier destination)
	{
		EnsureAlive();
		RenderCommands.CopyTexture(writer, Target(source), Target(destination));
		Retain(source);
		Retain(destination);
	}

	public void Blit(RenderTexture source, RenderTexture destination, Material material, int pass = -1)
	{
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(destination);
		Blit(new RenderTargetIdentifier(source), new RenderTargetIdentifier(destination), material, pass);
	}

	public void Blit(RenderTargetIdentifier source, RenderTargetIdentifier destination, Material material, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(material);
		if (source.Texture == destination.Texture && source.Id == destination.Id)
			throw new InvalidOperationException("Blit cannot sample and write the same target.");
		Pass(pass);
		RenderCommands.Blit(writer, Target(source), Target(destination), material.Handle, pass);
		Retain(source);
		Retain(destination);
		recordedResources.Add(material);
	}

	public void DrawFullscreen(Material material, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(material);
		Pass(pass);
		RenderCommands.DrawFullscreen(writer, material.Handle, pass);
		recordedResources.Add(material);
	}

	public void CopyBuffer(GraphicsBuffer source, GraphicsBuffer destination, ulong size, ulong sourceOffset = 0, ulong destinationOffset = 0)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(destination);
		if (size == 0) throw new ArgumentOutOfRangeException(nameof(size));
		RenderCommands.CopyBuffer(writer, source.Handle, destination.Handle, size, sourceOffset, destinationOffset);
		recordedResources.Add(source);
		recordedResources.Add(destination);
	}

	public void SetGlobalFloat(string name, float value)
	{
		Parameter(name);
		if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
		RenderCommands.SetGlobalFloat(writer, name, value);
	}

	public void SetGlobalInt(string name, int value) { Parameter(name); RenderCommands.SetGlobalInt(writer, name, value); }
	public void SetGlobalVector(string name, Vector4 value) { Parameter(name); Finite(value); RenderCommands.SetGlobalVector(writer, name, value); }
	public void SetGlobalMatrix(string name, Matrix4x4 value) { Parameter(name); Finite(value); RenderCommands.SetGlobalMatrix(writer, name, value); }
	public void SetGlobalVectorArray(string name, ReadOnlySpan<Vector4> values)
	{
		Parameter(name);
		ValidateArrayLength(values.Length, nameof(values));
		foreach (Vector4 value in values) Finite(value);
		RenderCommands.SetGlobalVectorArray(writer, name, values);
	}
	public void SetGlobalMatrixArray(string name, ReadOnlySpan<Matrix4x4> values)
	{
		Parameter(name);
		ValidateArrayLength(values.Length, nameof(values));
		foreach (Matrix4x4 value in values) Finite(value);
		RenderCommands.SetGlobalMatrixArray(writer, name, values);
	}
	public void SetViewProjectionMatrix(Matrix4x4 worldToClip) => SetGlobalMatrix("_UrsaViewProjection", worldToClip);
	public void RestoreViewProjectionMatrix(Camera camera)
	{
		ArgumentNullException.ThrowIfNull(camera);
		SetViewProjectionMatrix(camera.ProjectionMatrix * camera.ViewMatrix);
	}

	/// <summary>Overrides raster depth bias for subsequent scene draws in this camera command segment.</summary>
	public void SetDepthBias(int constantFactor, float slopeFactor)
	{
		EnsureAlive();
		if (!float.IsFinite(slopeFactor)) throw new ArgumentOutOfRangeException(nameof(slopeFactor));
		SetGlobalInt("_UrsaDepthBiasOverride", 1);
		SetGlobalInt("_UrsaDepthBiasConstant", constantFactor);
		SetGlobalFloat("_UrsaDepthBiasSlope", slopeFactor);
	}

	/// <summary>Restores the Shader Pass depth bias for subsequent scene draws.</summary>
	public void RestoreDepthBias()
	{
		EnsureAlive();
		SetGlobalInt("_UrsaDepthBiasOverride", 0);
	}

	public void SetGlobalTexture(string name, RenderTexture texture)
	{
		ArgumentNullException.ThrowIfNull(texture);
		SetGlobalTexture(name, new RenderTargetIdentifier(texture));
	}

	public void SetGlobalTexture(string name, RenderTargetIdentifier texture)
	{
		Parameter(name);
		RenderCommands.SetGlobalTexture(writer, name, Target(texture));
		Retain(texture);
	}

	public void SetGlobalBuffer(string name, GraphicsBuffer buffer)
	{
		Parameter(name);
		ArgumentNullException.ThrowIfNull(buffer);
		RenderCommands.SetGlobalBuffer(writer, name, buffer.Handle);
		recordedResources.Add(buffer);
	}

	public void GetTemporaryTarget(uint id, RenderTextureDescriptor descriptor)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(descriptor);
		if (id == 0 || temporaryTargetIds.Contains(id)) throw new ArgumentException("Temporary target ID must be nonzero and unique until released.", nameof(id));
		RenderCommands.GetTemporaryTarget(writer, id, new RenderTextureValue
		{
			Width = descriptor.Width, Height = descriptor.Height, ColorFormat = descriptor.ColorFormat,
			DepthBufferBits = descriptor.DepthBufferBits, EnableRandomWrite = descriptor.EnableRandomWrite, MsaaSamples = descriptor.MsaaSamples
		});
		temporaryTargetIds.Add(id);
	}

	public TemporaryRenderTargetScope CreateTemporaryTargetScope()
	{
		EnsureAlive();
		++activeTemporaryScopes;
		return new TemporaryRenderTargetScope(this, recordingGeneration);
	}

	public void ReleaseTemporaryTarget(uint id)
	{
		EnsureAlive();
		if (id == 0) throw new ArgumentOutOfRangeException(nameof(id));
		if (!temporaryTargetIds.Contains(id)) throw new InvalidOperationException("Temporary target ID is not active in this CommandBuffer.");
		RenderCommands.ReleaseTemporaryTarget(writer, id);
		temporaryTargetIds.Remove(id);
	}

	public void SetComputeFloat(ComputeShader shader, int kernel, string name, float value)
	{
		Parameter(name);
		if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
		RenderCommands.SetComputeFloat(writer, Compute(shader, kernel), kernel, name, value);
	}

	public void SetComputeInt(ComputeShader shader, int kernel, string name, int value)
	{
		Parameter(name);
		RenderCommands.SetComputeInt(writer, Compute(shader, kernel), kernel, name, value);
	}

	public void SetComputeVector(ComputeShader shader, int kernel, string name, Vector4 value)
	{
		Parameter(name);
		Finite(value);
		RenderCommands.SetComputeVector(writer, Compute(shader, kernel), kernel, name, value);
	}

	public void SetComputeMatrix(ComputeShader shader, int kernel, string name, Matrix4x4 value)
	{
		Parameter(name);
		Finite(value);
		RenderCommands.SetComputeMatrix(writer, Compute(shader, kernel), kernel, name, value);
	}

	public void SetComputeTexture(ComputeShader shader, int kernel, string name, RenderTargetIdentifier texture)
	{
		Parameter(name);
		RenderCommands.SetComputeTexture(writer, Compute(shader, kernel), kernel, name, Target(texture));
		Retain(texture);
	}

	public void SetComputeBuffer(ComputeShader shader, int kernel, string name, GraphicsBuffer buffer)
	{
		Parameter(name);
		ArgumentNullException.ThrowIfNull(buffer);
		RenderCommands.SetComputeBuffer(writer, Compute(shader, kernel), kernel, name, buffer.Handle);
		recordedResources.Add(buffer);
	}

	public void DispatchCompute(ComputeShader shader, int kernel, uint groupsX, uint groupsY = 1, uint groupsZ = 1)
	{
		EnsureAlive();
		if (groupsX == 0 || groupsY == 0 || groupsZ == 0) throw new ArgumentOutOfRangeException(nameof(groupsX));
		RenderCommands.DispatchCompute(writer, Compute(shader, kernel), kernel, groupsX, groupsY, groupsZ);
	}

	public void BeginSample(string sampleName)
	{
		EnsureAlive();
		ValidateSampleName(sampleName);
		RenderCommands.BeginSample(writer, sampleName);
		sampleNames.Add(sampleName);
	}

	public void EndSample(string sampleName)
	{
		EnsureAlive();
		ValidateSampleName(sampleName);
		if (sampleNames.Count == 0 || sampleNames[^1] != sampleName)
			throw new InvalidOperationException("EndSample must match the most recently opened sample.");
		RenderCommands.EndSample(writer, sampleName);
		sampleNames.RemoveAt(sampleNames.Count - 1);
	}

	internal uint RecordingGeneration => recordingGeneration;
	internal bool IsAlive => !disposed;
	internal void ReleaseTemporaryScope()
	{
		if (activeTemporaryScopes <= 0) throw new InvalidOperationException("Temporary target scope accounting is invalid.");
		--activeTemporaryScopes;
	}
	internal void EndSampleIfCurrent(string sampleName, uint generation)
	{
		if (disposed || generation != recordingGeneration) return;
		EndSample(sampleName);
	}

	public void Clear()
	{
		EnsureAlive();
		writer.Clear();
		recordedResources.Clear();
		temporaryTargetIds.Clear();
		sampleNames.Clear();
		++recordingGeneration;
	}

	public void Execute(ScriptableRenderContext context)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(context);
		if (activeTemporaryScopes != 0)
			throw new InvalidOperationException("All temporary target scopes must be disposed before executing their CommandBuffer.");
		context.Submit(BuildBatch(), recordedResources, name);
	}

	internal byte[] BuildBatch()
	{
		EnsureAlive();
		if (activeTemporaryScopes != 0)
			throw new InvalidOperationException("All temporary target scopes must be disposed before building their CommandBuffer.");
		if (sampleNames.Count != 0)
			throw new InvalidOperationException($"CommandBuffer has {sampleNames.Count} unclosed profiling sample(s).");
		byte[] batch = writer.ToArray();
		RenderingCommands.Validate(batch);
		return batch;
	}

	public void Dispose()
	{
		if (disposed) return;
		writer.Dispose();
		recordedResources.Clear();
		temporaryTargetIds.Clear();
		sampleNames.Clear();
		++recordingGeneration;
		disposed = true;
	}

	private static RenderTargetValue Target(RenderTargetIdentifier target) => new()
	{
		Kind = target.Texture == null ? RenderTargetKind.Temporary : RenderTargetKind.Texture,
		Id = target.Id, Texture = target.Texture?.Handle
	};

	private void Retain(RenderTargetIdentifier target) { if (target.Texture != null) recordedResources.Add(target.Texture); }
	private void EnsureAlive() { if (disposed) throw new ObjectDisposedException(nameof(CommandBuffer)); }
	private static void ValidateArrayLength(int count, string parameterName)
	{
		if (count < 1 || count > MaximumGlobalParameterArrayElements)
			throw new ArgumentOutOfRangeException(parameterName,
				$"Uniform arrays must contain between 1 and {MaximumGlobalParameterArrayElements} elements.");
	}
	private static void Pass(int pass) { if (pass < -1) throw new ArgumentOutOfRangeException(nameof(pass)); }
	private void Parameter(string name)
	{
		EnsureAlive();
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		if (Encoding.UTF8.GetByteCount(name) > 1024 || name.Contains('\0')) throw new ArgumentOutOfRangeException(nameof(name));
	}

	private static void ValidateSampleName(string sampleName)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(sampleName);
		if (sampleName.Contains('\0') || Encoding.UTF8.GetByteCount(sampleName) > 1024)
			throw new ArgumentOutOfRangeException(nameof(sampleName), "Sample names must fit within 1024 UTF-8 bytes and contain no null character.");
	}

	private static NativeAssetReference Compute(ComputeShader shader, int kernel)
	{
		ArgumentNullException.ThrowIfNull(shader);
		if (kernel < 0) throw new ArgumentOutOfRangeException(nameof(kernel));
		return NativeAssetReference.From(shader.Reference);
	}

	private static void Finite(Vector4 value)
	{
		if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z) || !float.IsFinite(value.W))
			throw new ArgumentOutOfRangeException(nameof(value));
	}

	private static void Finite(Matrix4x4 value)
	{
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				if (!float.IsFinite(value[row, column])) throw new ArgumentOutOfRangeException(nameof(value));
	}
}
