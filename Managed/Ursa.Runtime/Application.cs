using System;

namespace Ursa;

public static class Application
{
	public static void SetWindowTitle(string title)
	{
		EngineApi.SetWindowTitle(title ?? throw new ArgumentNullException(nameof(title)));
	}
}
