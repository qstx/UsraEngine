using Ursa;

namespace Ursa.Editor;

[BuiltinEditor]
[CustomEditor(typeof(Component), editorForChildClasses: true)]
[CanEditMultipleObjects]
public sealed class DefaultComponentInspector : Inspector
{
	public override void OnInspectorGUI() => DrawDefaultInspector();
}
