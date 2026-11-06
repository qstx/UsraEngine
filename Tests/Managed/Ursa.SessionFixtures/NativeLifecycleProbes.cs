using System;
using System.Text;
using Ursa;

namespace Ursa.SessionFixtures;

// Native host integration uses a valid Game assembly and a real type catalog.
// ProtocolTests also contains intentionally invalid schemas and Editor fixtures.
public sealed class NativeProtocolBehaviour : Behaviour
{
	public override void Awake() => Append("Awake");
	public override void OnEnable() => Append("OnEnable");
	public override void Start() => Append("Start");
	public override void FixedUpdate() => Append("FixedUpdate");
	public override void Update() => Append("Update");
	public override void LateUpdate() => Append("LateUpdate");
	public override void OnDisable() => Append("OnDisable");
	public override void OnDestroy()
	{
		Append("OnDestroy");
		AssetFileSystem.WriteAllBytes("user:ManagedBehaviourOnDestroy.txt", Encoding.UTF8.GetBytes(GameObject.Name));
	}
	private void Append(string name) => GameObject.Name += $":{name}";
}

public sealed class NativeSerializedProbeBehaviour : Behaviour
{
	public int savedValue;
	public override void Awake() => GameObject.Name += $":Awake:{savedValue}";
}

public sealed class NativeThrowOnAwakeBehaviour : Behaviour
{
	public override void Awake() => throw new InvalidOperationException("Synthetic Awake failure");
}

public sealed class NativeThrowOnFixedUpdateBehaviour : Behaviour
{
	public override void FixedUpdate() => throw new InvalidOperationException("Synthetic FixedUpdate failure");
}

public sealed class NativeThrowOnUpdateBehaviour : Behaviour
{
	public override void Update() => throw new InvalidOperationException("Synthetic Update failure");
}

public sealed class NativeThrowOnDisableBehaviour : Behaviour
{
	public override void OnDisable() => throw new InvalidOperationException("Synthetic OnDisable failure");
}

public sealed class NativeThrowOnDestroyBehaviour : Behaviour
{
	public override void OnDestroy() => throw new InvalidOperationException("Synthetic OnDestroy failure");
}

public sealed class NativeMoveWithInputBehaviour : Behaviour
{
	public float Step = 1.0f;
	public override void Update()
	{
		if (!Input.GetKeyDown(KeyCode.Right)) return;
		Vector3 position = Transform.LocalPosition;
		position.X += Step;
		Transform.LocalPosition = position;
	}
}
