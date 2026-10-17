using System;
using System.Collections.Generic;
using System.Linq;

namespace Ursa.Bindings;

public readonly record struct BindingRequirement(ulong Id, string Name, string Hash, uint FunctionCount, bool CallScoped);

public static class BindingContent
{
	public static void ValidateRequirements(IEnumerable<BindingRequirement> requirements, bool editor)
	{
		ArgumentNullException.ThrowIfNull(requirements);
		BindingRegistry.ValidateRequirements(requirements.ToArray(), editor);
	}
}
