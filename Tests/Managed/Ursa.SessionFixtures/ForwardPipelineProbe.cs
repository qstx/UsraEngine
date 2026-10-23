using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;

namespace Ursa.SessionFixtures;

public sealed class ForwardPipelineProbe : Behaviour
{
	public CameraComponent? other = null;
	private readonly List<Camera> cameras = new();
	public override void Update()
	{
		if (cameras.Count == 0)
		{
			cameras.Add(GameObject.GetComponent<CameraComponent>()!);
			if (other != null) cameras.Add(other);
		}
		RenderPipelineManager.Render(cameras, 1);
		GameObject.Name = "Forward:Rendered";
	}
}
