# Inspector components and Editor extensions

Component headers and Add Component results show type icons alongside readable
names. Built-in cameras, meshes, renderers and lights have dedicated icons;
project behaviours use a script icon and unknown native types use a generic
component icon. Existing registration APIs require no icon argument.

Each editable component has a tooltip-labeled More button. Its menu retains the
explicit Remove component command and existing availability rules. Enable
controls remain visible only for components that support them. Popup identity
uses the component's persistent identity so repeated types stay independent.
Scene, Prefab and runtime inspection use the same icon mapping; runtime edits
retain their existing temporary behavior. Reference pickers and list operations
use the shared icon style without changing reference types or saved identities.

Hierarchy displays ordered GameObjects and selection. The Inspector follows the
selection in either a Scene or a PrefabAsset. It contains the object's name,
enabled state and transform, then a section for each attached component.

Native component creation is registered through `Editor::ComponentEditors`.
Each `ComponentTypeDescriptor` has a stable serialized type ID, display name,
category, factory, optional eligibility check and duplicate policy. Built-in
types register in Engine. C# projects register scripts and custom Inspectors
through `Ursa.Editor`; the Add Component browser presents both providers through
the same selection flow.

The Inspector controls cover booleans, bounded numbers, colors, asset fields
and diagnostic text. Extensions report edits through the serialized-property
API so the Editor marks the document dirty. Custom Inspectors receive the
selected GameObject and either its Scene or Prefab context, including detached
Prefab objects.

Editor::EditorTools registers categorized tools with an availability check and
a callback. The callback receives the selected objects, current document and
resource session, plus the same Editor-owned control API. Call
EditorToolContext::MarkDirty() after a change. Tool windows exist only while the
Editor is running and are not required by packaged game startup.

Native Engine components use these contracts directly. Project behavior and
project-specific Inspector, menu, and tool extensions are authored in C# under
`Assets/Scripts/Editor/`. C# extensions receive serialized values and stable
asset identities through `Ursa.Editor`; they do not receive ImGui internals,
RHI handles, or direct host-file access.

Demo's `Assets/Scripts/Editor/SnakeEditor.cs` defines Snake Inspector, menu, and
window extensions. `UrsaProjectEditorExtensionsTests` verifies registration and
reloading against the Demo project. `UrsaComponentEditorsTests` covers native
categories, factories, Scene/Prefab targets, missing services, Editor controls,
and tool dirty tracking.
