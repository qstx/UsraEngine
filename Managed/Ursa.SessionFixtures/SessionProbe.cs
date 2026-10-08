using System;
using Ursa;

namespace Ursa.SessionFixtures;

[UrsaScriptType("test.session-probe")]
public sealed class SessionProbe : Behaviour
{
	private static int counter;
	public int value = Ursa.SessionDependency.ProbeDefaults.Value;
	public Vector3 position = new(1, 2, 3);
	public ProbeMode mode = ProbeMode.Second;
	public System.Collections.Generic.List<ProbeMode> modes = new() { ProbeMode.First, ProbeMode.Second };

	public override void Awake()
	{
		GameObject.Name = $"Session:{++counter}:{value}";
	}
}

// Deliberately violates the ownership contract to exercise the unload fault gate.
public sealed class LeakingProbe : Behaviour
{
	public override void Awake()
	{
		AppDomain.CurrentDomain.ProcessExit += OnProcessExit;
	}

	private static void OnProcessExit(object? sender, EventArgs arguments)
	{
	}
}

[UrsaScriptType("test.spawner")]
public sealed class SpawnerProbe : Behaviour
{
	private bool spawned;

	public override void Update()
	{
		if (spawned) return;
		spawned = true;
		float delta = Time.DeltaTime;
		var added = GameObject.AddComponent<SelfQueryProbe>();
		if (Time.DeltaTime != delta)
			throw new InvalidOperationException("Nested lifecycle dispatch lost the outer frame context.");
		if (!added.awakeSeen || !ReferenceEquals(GameObject.GetComponent<SelfQueryProbe>(), added))
			throw new InvalidOperationException("AddComponent returned before binding and Awake completed.");
		GameObject.Name = "Spawned:Bound:Awake";
	}
}

[UrsaScriptType("test.self-query")]
public sealed class SelfQueryProbe : Behaviour
{
	public bool awakeSeen;

	public override void Awake()
	{
		if (!ReferenceEquals(GameObject.GetComponent<SelfQueryProbe>(), this))
			throw new InvalidOperationException("Awake did not observe its registered Behaviour instance.");
		awakeSeen = true;
	}

	public override void OnEnable()
	{
		_ = Time.DeltaTime;
	}
}

public enum ProbeMode : byte
{
	First = 1,
	Second = 3
}

[UrsaScriptType("test.managed-render-component-probe")]
public sealed class ManagedRenderComponentProbe : Behaviour
{
	public override void Awake()
	{
		GameObject.Name = "RenderProbe:Awake";
		CameraComponent camera = GameObject.AddComponent<CameraComponent>();
		GameObject.Name = "RenderProbe:Camera";
		camera.VerticalFovRadians = 1.1f;
		camera.NearPlane = 0.2f;
		camera.FarPlane = 80.0f;

		BoxRenderComponent box = GameObject.AddComponent<BoxRenderComponent>();
		GameObject.Name = "RenderProbe:Box";
		box.Color = new Color(0.25f, 0.5f, 0.75f, 1.0f);
	}
}

[UrsaScriptType("test.destroyer")]
public sealed class DestroyerProbe : Behaviour
{
	public override void Update()
	{
		GameObject created = GameObject.Create("Disposable");
		created.Parent = GameObject;
		created.Transform.Position = new Vector3(4, 5, 6);
		if (created.Transform.Position.X != 4) throw new InvalidOperationException("World position assignment failed.");
		created.Destroy();
	}
}
