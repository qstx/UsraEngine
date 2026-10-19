using Ursa;
public sealed class ScriptPlayerBehaviour : Behaviour
{
	public AssetReference texture;
	public AssetReference material;
	public GameObject? target;
	public float speed = 2.0f;
	private TextureAsset? loadedTexture;
	private MaterialAsset? loadedMaterial;

	public override void Awake()
	{
		if (target == null)
		{
			throw new System.InvalidOperationException("Script Player target reference is missing.");
		}
		loadedTexture = TextureAsset.Load(texture);
		loadedMaterial = MaterialAsset.Load(material);
		Debug.Log("Script Player Awake: texture, material and target references resolved.");
	}

	public override void Update()
	{
		if (target == null)
		{
			return;
		}
		Vector3 position = target.Transform.LocalPosition;
		float distance = speed * Time.DeltaTime;
		if (Input.GetKey(KeyCode.Right)) position.X += distance;
		if (Input.GetKey(KeyCode.Left)) position.X -= distance;
		target.Transform.LocalPosition = position;
	}
}
