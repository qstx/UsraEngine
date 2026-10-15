using System;

namespace Ursa;

// Persistent configuration reference. Kernel import and dispatch are provided by the rendering service.
public sealed class ComputeShader
{
	private ComputeShader(AssetReference reference) { Reference = reference; }

	public AssetReference Reference { get; }

	public int FindKernel(string name)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		return EngineApi.ResourceRequest<int>(new
		{
			operation = "computeShader.findKernel",
			guid = Reference.Guid,
			localFileId = Reference.LocalFileId,
			name
		});
	}

	public (uint X, uint Y, uint Z) GetKernelThreadGroupSizes(int kernel)
	{
		if (kernel < 0) throw new ArgumentOutOfRangeException(nameof(kernel));
		uint[] size = EngineApi.ResourceRequest<uint[]>(new
		{
			operation = "computeShader.threadGroup",
			guid = Reference.Guid,
			localFileId = Reference.LocalFileId,
			kernel
		});
		if (size.Length != 3) throw new InvalidOperationException("Compute kernel thread-group metadata is invalid.");
		return (size[0], size[1], size[2]);
	}

	public static ComputeShader FromReference(AssetReference reference)
	{
		if (string.IsNullOrEmpty(reference.Guid) || reference.Guid == Guid.Empty.ToString("D"))
			throw new ArgumentException("ComputeShader reference requires a nonempty asset GUID.", nameof(reference));
		return new ComputeShader(reference);
	}
}
