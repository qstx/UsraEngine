using System;
using System.Collections.Generic;
using Ursa;

internal static class TextureFieldTests
{
	private sealed class Settings : ScriptableObject
	{
		public Texture? texture = null;
		public TextureAsset? legacy = null;
		public List<Texture?> textures = new();
	}

	internal static void Run(TextureAsset texture)
	{
		using var settings = ScriptableObject.CreateInstance<Settings>();
		settings.texture = texture;
		settings.legacy = texture;
		settings.textures = new() { texture, null };
		string payload = ScriptFieldSchema.Serialize(settings);
		settings.texture = null;
		settings.legacy = null;
		settings.textures.Clear();
		ScriptFieldSchema.Deserialize(settings, payload);
		if (settings.texture?.Reference.Guid != texture.Reference.Guid || settings.legacy?.Reference.Guid != texture.Reference.Guid ||
		    settings.textures.Count != 2 || settings.textures[0]?.Reference.Guid != texture.Reference.Guid || settings.textures[1] != null)
			throw new InvalidOperationException("Texture fields or collections did not preserve persistent identity.");
	}
}
