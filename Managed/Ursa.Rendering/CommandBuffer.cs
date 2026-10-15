using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;
using Ursa;

namespace Ursa.Rendering;

public sealed class CommandBuffer : IDisposable
{
	private readonly List<byte> records = new();
	private readonly List<RenderResource> recordedResources = new();
	private readonly HashSet<uint> temporaryTargetIds = new();
	private bool disposed;
	private uint commandCount;
	private string name;

	public CommandBuffer(string name = "")
	{
		this.name = name ?? throw new ArgumentNullException(nameof(name));
	}

	public string Name
	{
		get => name;
		set
		{
			EnsureAlive();
			name = value ?? throw new ArgumentNullException(nameof(value));
		}
	}

	public void SetRenderTarget(uint targetId)
	{
		EnsureAlive();
		byte[] payload = new byte[4];
		BinaryPrimitives.WriteUInt32LittleEndian(payload, targetId);
		Append(RenderCommandOpcode.SetRenderTarget, payload);
	}

	public void SetRenderTarget(RenderTexture target)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(target);
		byte[] payload = new byte[24];
		WriteHandle(payload, 0, target.Handle);
		Append(RenderCommandOpcode.SetRenderTarget, payload);
		recordedResources.Add(target);
	}

	public void SetRenderTarget(RenderTargetIdentifier target)
	{
		EnsureAlive();
		byte[] payload = new byte[32];
		WriteTarget(payload, 0, target);
		Append(RenderCommandOpcode.SetRenderTarget, payload);
	}

	public void SetRenderTargets(ReadOnlySpan<RenderTargetIdentifier> colors, RenderTargetIdentifier? depth = null)
	{
		EnsureAlive();
		if (colors.Length > 8 || (colors.Length == 0 && depth is null))
			throw new ArgumentOutOfRangeException(nameof(colors), "A target set requires one to eight colors or a depth target.");
		byte[] payload = new byte[checked(8 + (colors.Length + (depth is null ? 0 : 1)) * 32)];
		BinaryPrimitives.WriteUInt32LittleEndian(payload, (uint)colors.Length);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), depth is null ? 0u : 1u);
		for (int index = 0; index < colors.Length; ++index) WriteTarget(payload, 8 + index * 32, colors[index]);
		if (depth is not null) WriteTarget(payload, 8 + colors.Length * 32, depth.Value);
		Append(RenderCommandOpcode.SetRenderTargets, payload);
	}

	public void ClearTarget(bool color, bool depth, bool stencil, Vector4 colorValue, float depthValue = 1.0f,
		uint stencilValue = 0)
	{
		EnsureAlive();
		if (!color && !depth && !stencil)
			throw new ArgumentException("At least one target clear aspect must be selected.");
		if (!float.IsFinite(colorValue.X) || !float.IsFinite(colorValue.Y) || !float.IsFinite(colorValue.Z) ||
			!float.IsFinite(colorValue.W) || !float.IsFinite(depthValue) || depthValue < 0 || depthValue > 1)
			throw new ArgumentOutOfRangeException(nameof(colorValue));
		byte[] payload = new byte[28];
		uint flags = (color ? 1u : 0u) | (depth ? 2u : 0u) | (stencil ? 4u : 0u);
		BinaryPrimitives.WriteUInt32LittleEndian(payload, flags);
		WriteFloat(payload, 4, colorValue.X);
		WriteFloat(payload, 8, colorValue.Y);
		WriteFloat(payload, 12, colorValue.Z);
		WriteFloat(payload, 16, colorValue.W);
		WriteFloat(payload, 20, depthValue);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(24), stencilValue);
		Append(RenderCommandOpcode.ClearTarget, payload);
	}

	public void SetViewport(float x, float y, float width, float height, float minimumDepth = 0, float maximumDepth = 1)
	{
		EnsureAlive();
		if (!float.IsFinite(x) || !float.IsFinite(y) || !float.IsFinite(width) || !float.IsFinite(height) ||
			!float.IsFinite(minimumDepth) || !float.IsFinite(maximumDepth) || width <= 0 || height <= 0 ||
		minimumDepth < 0 || maximumDepth > 1 || minimumDepth > maximumDepth)
			throw new ArgumentOutOfRangeException(nameof(width));
		byte[] payload = new byte[24];
		WriteFloat(payload, 0, x);
		WriteFloat(payload, 4, y);
		WriteFloat(payload, 8, width);
		WriteFloat(payload, 12, height);
		WriteFloat(payload, 16, minimumDepth);
		WriteFloat(payload, 20, maximumDepth);
		Append(RenderCommandOpcode.SetViewport, payload);
	}

	public void SetScissor(int x, int y, uint width, uint height)
	{
		EnsureAlive();
		if (x < 0 || y < 0 || width == 0 || height == 0)
			throw new ArgumentOutOfRangeException(nameof(width));
		byte[] payload = new byte[16];
		BinaryPrimitives.WriteInt32LittleEndian(payload, x);
		BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(4), y);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), width);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(12), height);
		Append(RenderCommandOpcode.SetScissor, payload);
	}

	public void DrawRendererList(RendererList list, ScriptableRenderContext context)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(context);
		context.ValidateRendererList(list);
		byte[] payload = new byte[32];
		WriteContext(payload, list.Context);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(24), list.Index);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(28), list.Generation);
		Append(RenderCommandOpcode.DrawRendererList, payload);
	}

	public void DrawMesh(Mesh mesh, Material material, ReadOnlySpan<float> localToWorld, uint subMesh = 0, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(mesh);
		ArgumentNullException.ThrowIfNull(material);
		if (localToWorld.Length != 16)
			throw new ArgumentException("DrawMesh transform must contain sixteen floats.", nameof(localToWorld));
		foreach (float value in localToWorld)
			if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(localToWorld));
		if (pass < -1) throw new ArgumentOutOfRangeException(nameof(pass));
		byte[] payload = new byte[120];
		WriteHandle(payload, 0, mesh.Handle);
		WriteHandle(payload, 24, material.Handle);
		for (int index = 0; index < 16; ++index) WriteFloat(payload, 48 + index * 4, localToWorld[index]);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(112), subMesh);
		BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(116), pass);
		Append(RenderCommandOpcode.DrawMesh, payload);
		recordedResources.Add(mesh);
		recordedResources.Add(material);
	}

	public void CopyTexture(RenderTexture source, RenderTexture destination)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(destination);
		byte[] payload = new byte[48];
		WriteHandle(payload, 0, source.Handle);
		WriteHandle(payload, 24, destination.Handle);
		Append(RenderCommandOpcode.CopyTexture, payload);
		recordedResources.Add(source);
		recordedResources.Add(destination);
	}

	public void Blit(RenderTexture source, RenderTexture destination, Material material, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(destination);
		ArgumentNullException.ThrowIfNull(material);
		if (source == destination)
			throw new InvalidOperationException("Blit cannot sample and write the same render texture.");
		byte[] payload = new byte[76];
		WriteHandle(payload, 0, source.Handle);
		WriteHandle(payload, 24, destination.Handle);
		WriteHandle(payload, 48, material.Handle);
		BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(72), pass);
		Append(RenderCommandOpcode.Blit, payload);
		recordedResources.Add(source);
		recordedResources.Add(destination);
		recordedResources.Add(material);
	}

	public void Blit(RenderTargetIdentifier source, RenderTargetIdentifier destination, Material material, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(material);
		if (source.Texture == destination.Texture && source.Id == destination.Id)
			throw new InvalidOperationException("Blit cannot sample and write the same target.");
		if (pass < -1) throw new ArgumentOutOfRangeException(nameof(pass));
		byte[] payload = new byte[92];
		WriteTarget(payload, 0, source);
		WriteTarget(payload, 32, destination);
		WriteHandle(payload, 64, material.Handle);
		BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(88), pass);
		Append(RenderCommandOpcode.Blit, payload);
		recordedResources.Add(material);
	}

	public void CopyTexture(RenderTargetIdentifier source, RenderTargetIdentifier destination)
	{
		EnsureAlive();
		byte[] payload = new byte[64];
		WriteTarget(payload, 0, source);
		WriteTarget(payload, 32, destination);
		Append(RenderCommandOpcode.CopyTexture, payload);
	}

	public void DrawFullscreen(Material material, int pass = -1)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(material);
		if (pass < -1) throw new ArgumentOutOfRangeException(nameof(pass));
		byte[] payload = new byte[28];
		WriteHandle(payload, 0, material.Handle);
		BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(24), pass);
		Append(RenderCommandOpcode.DrawFullscreen, payload);
		recordedResources.Add(material);
	}

	public void CopyBuffer(GraphicsBuffer source, GraphicsBuffer destination, ulong size, ulong sourceOffset = 0,
		ulong destinationOffset = 0)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(source);
		ArgumentNullException.ThrowIfNull(destination);
		if (size == 0) throw new ArgumentOutOfRangeException(nameof(size));
		byte[] payload = new byte[72];
		WriteHandle(payload, 0, source.Handle);
		WriteHandle(payload, 24, destination.Handle);
		BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(48), size);
		BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(56), sourceOffset);
		BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(64), destinationOffset);
		Append(RenderCommandOpcode.CopyBuffer, payload);
		recordedResources.Add(source);
		recordedResources.Add(destination);
	}

	public void SetGlobalFloat(string name, float value)
	{
		if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
		Span<byte> data = stackalloc byte[4];
		WriteFloat(data, 0, value);
		AppendGlobal(name, 1, data);
	}

	public void SetGlobalVector(string name, Vector4 value)
	{
		if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z) || !float.IsFinite(value.W))
			throw new ArgumentOutOfRangeException(nameof(value));
		Span<byte> data = stackalloc byte[16];
		WriteFloat(data, 0, value.X);
		WriteFloat(data, 4, value.Y);
		WriteFloat(data, 8, value.Z);
		WriteFloat(data, 12, value.W);
		AppendGlobal(name, 2, data);
	}

	public void SetGlobalTexture(string name, RenderTexture texture)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(texture);
		byte[] utf8 = ValidateParameterName(name);
		byte[] payload = new byte[checked(32 + utf8.Length)];
		BinaryPrimitives.WriteUInt32LittleEndian(payload, 3);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), (uint)utf8.Length);
		utf8.CopyTo(payload, 8);
		WriteHandle(payload, 8 + utf8.Length, texture.Handle);
		Append(RenderCommandOpcode.SetGlobalParameters, payload);
		recordedResources.Add(texture);
	}

	public void SetGlobalTexture(string name, RenderTargetIdentifier texture)
	{
		EnsureAlive();
		byte[] value = new byte[32];
		WriteTarget(value, 0, texture);
		AppendGlobal(name, 4, value);
	}

	public void SetGlobalInt(string name, int value)
	{
		Span<byte> data = stackalloc byte[4];
		BinaryPrimitives.WriteInt32LittleEndian(data, value);
		AppendGlobal(name, 6, data);
	}

	public void SetGlobalMatrix(string name, ReadOnlySpan<float> values)
	{
		if (values.Length != 16) throw new ArgumentException("A matrix requires sixteen values.", nameof(values));
		Span<byte> data = stackalloc byte[64];
		for (int index = 0; index < 16; ++index)
		{
			if (!float.IsFinite(values[index])) throw new ArgumentOutOfRangeException(nameof(values));
			WriteFloat(data, index * 4, values[index]);
		}
		AppendGlobal(name, 5, data);
	}

	public void SetGlobalBuffer(string name, GraphicsBuffer buffer)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(buffer);
		byte[] data = new byte[24];
		WriteHandle(data, 0, buffer.Handle);
		AppendGlobal(name, 7, data);
		recordedResources.Add(buffer);
	}

	public void GetTemporaryTarget(uint id, RenderTextureDescriptor descriptor)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(descriptor);
		if (id == 0 || temporaryTargetIds.Contains(id))
			throw new ArgumentException("Temporary target ID must be nonzero and unique until released.", nameof(id));
		byte[] payload = new byte[28];
		BinaryPrimitives.WriteUInt32LittleEndian(payload, id);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), descriptor.Width);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), descriptor.Height);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(12), (uint)descriptor.ColorFormat);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(16), descriptor.DepthBufferBits);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(20), descriptor.EnableRandomWrite ? 1u : 0u);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(24), descriptor.MsaaSamples);
		Append(RenderCommandOpcode.GetTemporaryTarget, payload);
		temporaryTargetIds.Add(id);
	}

	public void SetComputeFloat(ComputeShader shader, int kernel, string name, float value)
	{
		if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
		Span<byte> data = stackalloc byte[4];
		WriteFloat(data, 0, value);
		AppendCompute(shader, kernel, name, 1, data);
	}

	public void SetComputeInt(ComputeShader shader, int kernel, string name, int value)
	{
		Span<byte> data = stackalloc byte[4];
		BinaryPrimitives.WriteInt32LittleEndian(data, value);
		AppendCompute(shader, kernel, name, 6, data);
	}

	public void SetComputeVector(ComputeShader shader, int kernel, string name, Vector4 value)
	{
		if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z) || !float.IsFinite(value.W))
			throw new ArgumentOutOfRangeException(nameof(value));
		Span<byte> data = stackalloc byte[16];
		WriteFloat(data, 0, value.X);
		WriteFloat(data, 4, value.Y);
		WriteFloat(data, 8, value.Z);
		WriteFloat(data, 12, value.W);
		AppendCompute(shader, kernel, name, 2, data);
	}

	public void SetComputeMatrix(ComputeShader shader, int kernel, string name, ReadOnlySpan<float> values)
	{
		if (values.Length != 16) throw new ArgumentException("A matrix requires sixteen values.", nameof(values));
		Span<byte> data = stackalloc byte[64];
		for (int index = 0; index < 16; ++index)
		{
			if (!float.IsFinite(values[index])) throw new ArgumentOutOfRangeException(nameof(values));
			WriteFloat(data, index * 4, values[index]);
		}
		AppendCompute(shader, kernel, name, 5, data);
	}

	public void SetComputeTexture(ComputeShader shader, int kernel, string name, RenderTargetIdentifier texture)
	{
		EnsureAlive();
		byte[] data = new byte[32];
		WriteTarget(data, 0, texture);
		AppendCompute(shader, kernel, name, 4, data);
	}

	public void SetComputeBuffer(ComputeShader shader, int kernel, string name, GraphicsBuffer buffer)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(buffer);
		byte[] data = new byte[24];
		WriteHandle(data, 0, buffer.Handle);
		AppendCompute(shader, kernel, name, 7, data);
		recordedResources.Add(buffer);
	}

	public void DispatchCompute(ComputeShader shader, int kernel, uint groupsX, uint groupsY = 1, uint groupsZ = 1)
	{
		EnsureAlive();
		if (groupsX == 0 || groupsY == 0 || groupsZ == 0) throw new ArgumentOutOfRangeException(nameof(groupsX));
		byte[] data = new byte[40];
		WriteComputeReference(data, shader, kernel);
		BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(28), groupsX);
		BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(32), groupsY);
		BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(36), groupsZ);
		Append(RenderCommandOpcode.DispatchCompute, data);
	}

	public void ReleaseTemporaryTarget(uint id)
	{
		EnsureAlive();
		if (id == 0) throw new ArgumentOutOfRangeException(nameof(id));
		byte[] payload = new byte[4];
		BinaryPrimitives.WriteUInt32LittleEndian(payload, id);
		Append(RenderCommandOpcode.ReleaseTemporaryTarget, payload);
		temporaryTargetIds.Remove(id);
	}

	public void Clear()
	{
		EnsureAlive();
		records.Clear();
		recordedResources.Clear();
		temporaryTargetIds.Clear();
		commandCount = 0;
	}

	public void Execute(ScriptableRenderContext context)
	{
		EnsureAlive();
		ArgumentNullException.ThrowIfNull(context);
		context.Submit(BuildBatch(), recordedResources);
	}

	public void Dispose()
	{
		if (disposed)
			return;
		records.Clear();
		recordedResources.Clear();
		temporaryTargetIds.Clear();
		commandCount = 0;
		disposed = true;
	}

	internal byte[] BuildBatch()
	{
		EnsureAlive();
		int length = checked(RenderCommandProtocol.batchHeaderSize + records.Count);
		byte[] batch = new byte[length];
		BinaryPrimitives.WriteUInt32LittleEndian(batch, RenderCommandProtocol.version);
		BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(4), RenderCommandProtocol.batchHeaderSize);
		BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(8), commandCount);
		BinaryPrimitives.WriteUInt32LittleEndian(batch.AsSpan(12), checked((uint)length));
		records.CopyTo(batch, RenderCommandProtocol.batchHeaderSize);
		RenderCommandProtocol.Validate(batch);
		return batch;
	}

	private void Append(RenderCommandOpcode opcode, byte[] payload)
	{
		int recordBytes = checked(RenderCommandProtocol.recordHeaderSize + payload.Length);
		int alignedBytes = checked((recordBytes + 7) & ~7);
		int newBatchBytes = checked(RenderCommandProtocol.batchHeaderSize + records.Count + alignedBytes);
		if (newBatchBytes > RenderCommandProtocol.maxByteSize)
			throw new InvalidOperationException("Render command batch exceeds the 64 MiB limit.");
		byte[] record = new byte[alignedBytes];
		BinaryPrimitives.WriteUInt32LittleEndian(record, (uint)opcode);
		BinaryPrimitives.WriteUInt32LittleEndian(record.AsSpan(4), (uint)alignedBytes);
		payload.CopyTo(record, RenderCommandProtocol.recordHeaderSize);
		records.AddRange(record);
		commandCount = checked(commandCount + 1);
	}

	private void AppendGlobal(string name, uint valueKind, ReadOnlySpan<byte> value)
	{
		EnsureAlive();
		byte[] utf8 = ValidateParameterName(name);
		byte[] payload = new byte[checked(8 + utf8.Length + value.Length)];
		BinaryPrimitives.WriteUInt32LittleEndian(payload, valueKind);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), (uint)utf8.Length);
		utf8.CopyTo(payload, 8);
		value.CopyTo(payload.AsSpan(8 + utf8.Length));
		Append(RenderCommandOpcode.SetGlobalParameters, payload);
	}

	private void AppendCompute(ComputeShader shader, int kernel, string name, uint kind, ReadOnlySpan<byte> value)
	{
		EnsureAlive();
		byte[] utf8 = ValidateParameterName(name);
		byte[] payload = new byte[checked(36 + utf8.Length + value.Length)];
		WriteComputeReference(payload, shader, kernel);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(28), kind);
		BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(32), (uint)utf8.Length);
		utf8.CopyTo(payload, 36);
		value.CopyTo(payload.AsSpan(36 + utf8.Length));
		Append(RenderCommandOpcode.SetComputeParameters, payload);
	}

	private static void WriteComputeReference(Span<byte> destination, ComputeShader shader, int kernel)
	{
		ArgumentNullException.ThrowIfNull(shader);
		if (kernel < 0) throw new ArgumentOutOfRangeException(nameof(kernel));
		if (!Guid.TryParse(shader.Reference.Guid, out Guid guid) || guid == Guid.Empty ||
			!guid.TryWriteBytes(destination[..16], true, out int count) || count != 16)
			throw new ArgumentException("ComputeShader requires a valid persistent reference.", nameof(shader));
		BinaryPrimitives.WriteUInt64LittleEndian(destination[16..], shader.Reference.LocalFileId);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[24..], (uint)kernel);
	}

	private static byte[] ValidateParameterName(string name)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		byte[] utf8 = Encoding.UTF8.GetBytes(name);
		if (utf8.Length > 1024 || name.Contains('\0')) throw new ArgumentOutOfRangeException(nameof(name));
		return utf8;
	}

	private void EnsureAlive()
	{
		if (disposed)
			throw new ObjectDisposedException(nameof(CommandBuffer));
	}

	private static void WriteContext(Span<byte> destination, RenderContextToken token)
	{
		BinaryPrimitives.WriteUInt64LittleEndian(destination, token.sessionId);
		BinaryPrimitives.WriteUInt64LittleEndian(destination[8..], token.frameId);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[16..], token.index);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[20..], token.generation);
	}

	private static void WriteHandle(Span<byte> destination, int offset, NativeHandle handle)
	{
		BinaryPrimitives.WriteUInt64LittleEndian(destination[offset..], handle.sessionId);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[(offset + 8)..], handle.index);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[(offset + 12)..], handle.generation);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[(offset + 16)..], handle.kind);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[(offset + 20)..], handle.reserved);
	}

	private void WriteTarget(Span<byte> destination, int offset, RenderTargetIdentifier target)
	{
		BinaryPrimitives.WriteUInt32LittleEndian(destination[offset..], target.Texture is null ? 0u : 1u);
		BinaryPrimitives.WriteUInt32LittleEndian(destination[(offset + 4)..], target.Id);
		if (target.Texture is not null)
		{
			WriteHandle(destination, offset + 8, target.Texture.Handle);
			recordedResources.Add(target.Texture);
		}
	}

	private static void WriteFloat(Span<byte> destination, int offset, float value) =>
		BinaryPrimitives.WriteUInt32LittleEndian(destination[offset..], BitConverter.SingleToUInt32Bits(value));
}
