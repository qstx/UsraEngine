using Ursa;

[UrsaScriptType("demo.move-with-input", Version = 1)]
public sealed class MoveWithInputBehaviour : Behaviour
{
	public float Step = 1.0f;

	public override void Update()
	{
		if (!Input.GetKeyDown(KeyCode.Right))
		{
			return;
		}
		Vector3 position = Transform.LocalPosition;
		position.X += Step;
		Transform.LocalPosition = position;
	}
}
