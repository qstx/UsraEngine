using Ursa;
public sealed class MaterialShowcaseBehaviour : Behaviour
{
	private readonly System.Collections.Generic.List<Material> ownedMaterials = new();
	public AssetReference checkerMaterial =
		new("66666666-6666-4666-8666-666666666666");
	public AssetReference ceramicMaterial =
		new("8b8e4e91-58a2-4a26-a2d5-0a64365d3401");
	public AssetReference goldMaterial =
		new("a3f83e80-98a7-4d9e-a606-1c4a572b376b");

	public override void Awake()
	{
		AddMaterialBox("Checker", -2.2f, MaterialAsset.Load(checkerMaterial), new Color(1, 1, 1));
		AddMaterialBox("Ceramic", 0, MaterialAsset.Load(ceramicMaterial), new Color(1, 1, 1));
		AddMaterialBox("Gold", 2.2f, MaterialAsset.Load(goldMaterial), new Color(1, 1, 1));
		GameObject floor = GameObject.CreatePrimitive();
		floor.Name = "Showcase Floor";
		floor.Parent = GameObject;
		floor.Transform.LocalPosition = new Vector3(0, -1.0f, -0.5f);
		floor.Transform.LocalScale = new Vector3(8.0f, 0.2f, 3.0f);
		var floorMaterial = floor.GetComponent<MeshRendererComponent>()!.Material!;
		floorMaterial.Color = new Color(0.13f, 0.17f, 0.22f).ToLinear();
		ownedMaterials.Add(floorMaterial);
	}

	private void AddMaterialBox(string name, float x, MaterialAsset material, Color color)
	{
		GameObject item = GameObject.CreatePrimitive();
		item.Name = name;
		item.Parent = GameObject;
		item.Transform.LocalPosition = new Vector3(x, 0, 0);
		item.Transform.LocalScale = new Vector3(1.5f, 1.5f, 1.5f);
		var renderer = item.GetComponent<MeshRendererComponent>()!;
		renderer.SharedMaterial = material;
		if (color.R != 1 || color.G != 1 || color.B != 1 || color.A != 1)
		{
			var instance = renderer.Material!;
			var original = instance.Color;
			instance.Color = new Color(original.R * color.R, original.G * color.G, original.B * color.B, original.A * color.A);
			ownedMaterials.Add(instance);
		}
	}
	public override void OnDestroy()
	{
		foreach (var material in ownedMaterials) if (material.IsValid) material.Destroy();
		ownedMaterials.Clear();
	}

}
