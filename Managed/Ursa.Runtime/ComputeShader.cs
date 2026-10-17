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
		return NativeResourceBindings.FindComputeKernel(NativeAssetReference.From(Reference), name);
	}

	public (uint X, uint Y, uint Z) GetKernelThreadGroupSizes(int kernel)
	{
		if (kernel < 0) throw new ArgumentOutOfRangeException(nameof(kernel));
		ComputeThreadGroupSize size = NativeResourceBindings.GetComputeThreadGroupSize(NativeAssetReference.From(Reference), kernel);
		return (size.X, size.Y, size.Z);
	}

	public static ComputeShader FromReference(AssetReference reference)
	{
		if (string.IsNullOrEmpty(reference.Guid) || reference.Guid == Guid.Empty.ToString("D"))
			throw new ArgumentException("ComputeShader reference requires a nonempty asset GUID.", nameof(reference));
		return new ComputeShader(reference);
	}
}
