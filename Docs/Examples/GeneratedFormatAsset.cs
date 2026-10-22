using Ursa;

namespace Ursa.Examples;

// Place in Assets. The script asset metadata supplies its stable type identity.
public sealed class GeneratedFormatAsset : ScriptableObject
{
	public string label = string.Empty;
	public float size;
	public Mesh mesh = null!;
	public Material material = null!;
	public Texture2D texture = null!;
}
