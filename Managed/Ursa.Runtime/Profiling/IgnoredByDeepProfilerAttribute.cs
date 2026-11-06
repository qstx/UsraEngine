using System;

namespace Ursa.Profiling;

/// <summary>Excludes a project method or type, including its state machines, from Deep Profiling.</summary>
[AttributeUsage(AttributeTargets.Method | AttributeTargets.Constructor | AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class IgnoredByDeepProfilerAttribute : Attribute
{
}

/// <summary>Identifies assemblies produced by the offline Deep Profiling tool.</summary>
[AttributeUsage(AttributeTargets.Assembly)]
public sealed class DeepProfiledAttribute(string version) : Attribute
{
	public string Version { get; } = version;
}
