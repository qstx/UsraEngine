using Ursa;
public sealed class MoveWithInputBehaviour : Behaviour
{
	public float step = 1.0f;

	public override void Update()
	{
		if (!Input.GetKeyDown(KeyCode.Right))
		{
			return;
		}

		Vector3 position = Transform.LocalPosition;
		position.X += step;
		Transform.LocalPosition = position;
	}
}
