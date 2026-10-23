using System;
using Ursa;
using Ursa.Rendering;

namespace Ursa.SessionFixtures;

public sealed class RenderingBindingsProbe : Behaviour
{
	public string shaderGuid = string.Empty;

	public override void Update()
	{
		var shader = ComputeShader.FromReference(new AssetReference(shaderGuid));
		int kernel = shader.FindKernel("Fill");
		if (shader.GetKernelThreadGroupSizes(kernel) != (1u, 1u, 1u))
			throw new InvalidOperationException("Matrix probe kernel layout differs.");
		using var buffer = GraphicsBuffer.Create(GraphicsBufferTarget.Structured, 2, 16);
		var matrix = new Matrix4x4(new float[] { 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15, 4, 8, 12, 16 });
		using (var context = new ScriptableRenderContext(1))
		{
			using var commands = new CommandBuffer("Matrix layout probe");
			commands.SetComputeMatrix(shader, kernel, "_Row", matrix);
			commands.SetComputeMatrix(shader, kernel, "_Column", matrix);
			commands.SetComputeBuffer(shader, kernel, "_Data", buffer);
			commands.DispatchCompute(shader, kernel, 1);
			context.ExecuteCommandBuffer(commands);
			context.Submit();
		}
		byte[] bytes = buffer.GetRawData(0, 32);
		float[] expected = { 27, 71, 115, 159, 27, 71, 115, 159 };
		for (int index = 0; index < expected.Length; ++index)
			if (BitConverter.ToSingle(bytes, index * 4) != expected[index])
				throw new InvalidOperationException("C# Matrix4x4 and HLSL multiplication disagree.");
		GameObject.Name = "MatrixBindingsPassed";
	}
}
