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

For a multi-object selection, the Inspector shows the selection count and edits
the common name, active state, transform fields, and supported common native
component properties as one atomic property group. A mixed field is not written
until the user edits it; editing one vector component preserves every target's
other components. Common component identity uses serialized type or exact
script type plus its same-type occurrence order. The Scene Gizmo continues to
operate on the primary object only. Undo/Redo is not part of this editing flow.

Project multi-selection also exposes a shared Inspector for writable source
Materials using the same Shader property identities and for ScriptableObjects
of the same exact script type. Only fields supported by every target are shown;
unsupported ScriptableObject collection and reference fields are reported
without being edited. Each field edit is applied to the selected source drafts
as one group, and **Save Selected** writes all dirty members. Imported subassets
are read-only here, mixed asset kinds and importer sources show a restriction,
and the Inspector footer preview continues to follow the primary asset.

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
