# Inspector components and Editor extensions

Hierarchy displays ordered GameObjects and selection. The Inspector follows the
selection in either a Scene or a PrefabAsset. It contains the object's name,
enabled state and transform, then a section for each attached component.

Component creation is registered through Editor::ComponentEditors. Each
ComponentTypeDescriptor has a stable serialized type ID, display name, category,
factory, optional eligibility check and duplicate policy. The Add Component
browser groups entries by category and filters names as the user types. Built-in
types register in Engine; projects add descriptors during EditorProjectSetup.

Register a persistent component factory and optional Inspector drawer on the
project extension setup callback. The working Snake example registers both
SnakeController and SnakePresentation with factories, service requirements,
categories and Inspector drawers.
The InspectorUI controls cover booleans, bounded floats, colors, stable-ID
choices and diagnostic text. Extensions report edits through Result<bool> so the
Editor marks the current document dirty. A custom Inspector receives the
selected GameObject, its root set, and either its Scene or PrefabAsset context.
It does not depend on Scene membership to inspect a detached Prefab.

Editor::EditorTools registers categorized tools with an availability check and
a callback. The callback receives the selected objects, current document and
resource session, plus the same Editor-owned control API. Call
EditorToolContext::MarkDirty() after a change. Tool windows exist only while the
Editor is running and are not required by packaged game startup.

Native C++ components and project extensions use these contracts directly. A
future script runtime can adapt them using stable component IDs, serialized
values and Editor handles. Script language/runtime integration and script asset
compilation are separate work; scripts must not receive ImGui internals, RHI
handles, or direct host-file access.

Demo/Editor/SnakeEditors.cpp shows custom component Inspector registration.
UrsaComponentEditorsTests covers categories, factories, Scene/Prefab targets,
missing services, Editor controls and tool dirty tracking.
