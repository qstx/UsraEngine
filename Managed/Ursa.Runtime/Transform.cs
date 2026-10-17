using System;

namespace Ursa;

public sealed class Transform
{
	private readonly NativeHandle handle;

	internal Transform(NativeHandle handle)
	{
		this.handle = handle;
	}

	public Vector3 Position
	{
		get => EngineApi.GetWorldPosition(handle);
		set => EngineApi.SetWorldPosition(handle, value);
	}

	public Vector3 LocalPosition
	{
		get => NativeRuntimeBindings.GetLocalTransform(handle).Position;
		set
		{
			LocalTransformValue current = NativeRuntimeBindings.GetLocalTransform(handle);
			current.Position = value;
			NativeRuntimeBindings.SetLocalTransform(handle, current);
		}
	}

	public Quaternion LocalRotation
	{
		get => NativeRuntimeBindings.GetLocalTransform(handle).Rotation;
		set
		{
			LocalTransformValue current = NativeRuntimeBindings.GetLocalTransform(handle);
			current.Rotation = value;
			NativeRuntimeBindings.SetLocalTransform(handle, current);
		}
	}

	public Vector3 LocalScale
	{
		get => NativeRuntimeBindings.GetLocalTransform(handle).Scale;
		set
		{
			LocalTransformValue current = NativeRuntimeBindings.GetLocalTransform(handle);
			current.Scale = value;
			NativeRuntimeBindings.SetLocalTransform(handle, current);
		}
	}
}
