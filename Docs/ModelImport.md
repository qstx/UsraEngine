# Model import

The Editor imports FBX, OBJ, glTF 2.0 (`.gltf`), and binary glTF (`.glb`) source files through VFS. Source files remain in
`Assets/`; versioned model, node, mesh, material, and embedded-texture artifacts
are published to the rebuildable project `Library` cache. Each model keeps its
asset GUID, while imported sub-assets use stable local file IDs.

Select a supported model source in the Project panel to open **Model Import**. The
Inspector shows import settings, the imported hierarchy and node transforms,
sub-assets, dependencies, and importer diagnostics. Change settings and choose
**Reimport** to update the cached artifacts. A failed import keeps the last
successful cache available and reports the source path and error.

模型放入 Scene 或 Prefab 时，静态物体使用 MeshFilter + MeshRenderer，蒙皮物体使用 SkinnedMeshRenderer + Animator。一个源节点包含多个网格时，按源顺序创建渲染子对象，保留原节点变换和网格/材质子资产 local IDs。几何 artifact 使用通用 `.ursamesh`；默认材质分配保存在模型实例化记录中。模型导入器版本为 9（内容目标 `model-v9`）；升级后显式重导入重建 Library，旧子资产 ID 不提供兼容映射。

`ModelImporter::BuildCandidate` 只构造 CPU 候选：节点层级和局部变换、Mesh、材质与纹理绑定、内嵌纹理以及骨骼/动画引用。`ModelImportCandidate::Encode` 在最终修改后统一校验并编码，节点改名不会重分配输出 key；缺失节点、环、失效几何和错误绑定在发布前拒绝。构造期间产生的外部 buffer/纹理依赖与主模型纳入同一候选快照；原生模型脚本后处理在最终候选校验前运行，处理失败不会发布部分产物。

Mesh Clone/另存保留骨骼 GUID/local file ID；骨骼身份不取决于网格资产 GUID。资源会话显式重载会发布新几何版本并保留共享对象身份，独立副本不被源重导入覆盖。松散 Runtime 内容和 pak 使用同一目录映射加载 Mesh，普通 CMake 构建只编译代码，资源准备使用 Editor 导入或 `UrsaAssetImport`。

Supported settings currently include scale factor, generated normals and
tangents, UV flipping, material import, and texture import. Materials use the
engine's built-in PBR Shader. The rig setting retains one to four influences
per vertex, with four as the default. Supported material values include base color,
emissive color, metallic and roughness factors, opacity, and common color,
metallic-roughness, normal, occlusion, and emissive textures. External texture
files are independent project assets; embedded FBX textures become texture
sub-assets of the model. Unsupported source material properties appear in the
Inspector diagnostics. The current material model does not represent glTF alpha
modes/cutoff, double-sided state, nonzero UV sets, texture transforms, or source
sampler settings; the importer reports these properties instead of silently
claiming that their rendering effects were preserved.

glTF base-color and emissive textures use sRGB decoding; normal, metallic-roughness,
and occlusion textures use linear data. The PBR shader reads metallic from the
blue channel and roughness from green in the packed metallic-roughness texture,
and occlusion from red. glTF normal textures use the normal-map import path.

FBX and glTF can carry node hierarchy, transforms, materials, textures, skinning, and
animation. Import records shared node-pose joints, up to four normalized joint
influences per vertex, and named animation clips with node keyframes. Each mesh
stores a skin-local joint-to-pose mapping and its own inverse-bind matrices, so
meshes that share node poses retain independent skin bindings. Adding an animated model to a Scene or Prefab adds a SkinnedMeshRenderer
for each skinned mesh and an Animator on the model root, initialized to the
first imported clip. Animator exposes direct Play, Stop, time, speed, and loop
controls; it does not require an Animator Controller. OBJ imports static
geometry, groups, MTL materials, and texture references; OBJ does not define
skeletal animation.

Runtime pose evaluation runs during component Update before rendering. The
renderer uploads each current joint palette to a storage buffer and applies
four-weight GPU skinning in the PBR vertex shader. Animator samples LINEAR vector
tracks, shortest-path spherical LINEAR rotation, STEP tracks, and CUBICSPLINE tracks
with their authored tangents. Animator Controller graphs, blend trees, retargeting,
root motion, and morph-target animation are not
supported yet.

glTF input is preflighted as version 2.0. GLB magic, container version, total
length, JSON-first chunk, and chunk boundaries are checked before Assimp reads the
source. The default scene is selected when present; otherwise the first scene is
used. Required glTF extensions are rejected because their effects are not
guaranteed through the complete import and runtime path. Used optional extensions
are listed in import diagnostics and may be ignored when their effects are not
represented by the engine. Morph targets and weights animation are outside the
current import scope. External buffers and images resolve through project VFS
paths; percent-escaped URI bytes are decoded before VFS resolution. Data URIs
and GLB embedded buffers/images are handled by Assimp. Invalid percent escapes,
absolute paths and relative paths escaping the VFS project mount are rejected.

Selecting a model now opens its complete hierarchy in [Inspector Preview](EditorAssetPreviews.md), without adding it to the authoring Scene. Whole-model and stable Mesh sub-asset views support orbit, zoom and reset. Animated FBX and glTF previews start in bind pose and support clip selection, play/pause, looping, positive speed and paused scrubbing through the exact endpoint. This uses a private clock and explicit pose sampling, without Scene Update or gameplay callbacks. Prefab previews similarly use private read-only snapshots; opening or instantiating remains an explicit authoring action.

## Editor workflow and recovery

1. Copy a `.gltf` and its referenced files together (or a self-contained `.glb`) under the project's `Assets` folder. Keep external buffer and image paths relative to the model and within that folder.
2. Select the model in Project. **Model Import** shows its hierarchy, settings, dependencies and diagnostics; the Inspector preview shows the model or a selected Mesh without adding it to the open Scene. For animated models, select a clip and use play/pause or paused scrubbing.
3. Drag the model into a Scene or Prefab to create an instance. Save, close and reopen to check its Mesh, materials, Animator and clip references.
4. After changing the source or settings, choose **Reimport**. Unchanged structure keeps sub-asset local IDs. Reopen the preview to see the new output.

If a referenced `.bin` or image is missing, outside the project VFS, or malformed, the diagnostic identifies the model and dependency. Restore the source path and reimport; a failed candidate leaves the previous complete Library artifacts intact, while the current source remains unready until a successful import. Do not delete the last good Library artifacts to recover from a failed reimport.

The model importer is version 9 (`model-v9`). Projects with older model artifacts should rebuild the disposable `Library` and explicitly reimport the source models. Old model artifacts are not considered current, and the engine does not promise a compatibility mapping for local sub-asset IDs from earlier model versions. Keep the original source files and save/inspect Scenes after migration so missing references are visible before packaging.

For headless preparation, run `UrsaAssetImport --project <project> --engine <Engine/Assets> --configuration Debug --profile Editor`; the Editor and command-line importer use the same native model rules. Build Player content with `UrsaPak --engine <Engine/Assets> --project-root <project> --configuration Debug --output <Content.pak>`. Cooked runtime content contains engine artifacts, not glTF source documents or external `.bin` buffers; the source files are still required for future reimports.

`UrsaDemoModelImportTests` imports and places the supplied `Cube.fbx` into a
Scene, saves and reopens its hierarchy, and checks an OBJ/MTL fixture using
`UrsaEngine.png`, an embedded-texture FBX fixture from the pinned Assimp
submodule, stable sub-assets, dependency tracking, missing-file diagnostics,
and preservation of the last successful model artifact. An animated FBX fixture
checks clip playback state, bind-pose and animated-pose pixels, and scene
save/reload without an Animator Controller. The OBJ model is also saved and
reopened as both a Scene and a Prefab.

To validate the importer against a public animated GLB, build
`UrsaModelImportCandidateTests` and run `Tools/ValidatePublicGltf.ps1`. The
script downloads Khronos' `RiggedSimple.glb` into the ignored `Build` directory
when no local file is supplied, then checks candidate construction, mesh skin
bindings, animation clips, and artifact encoding, then imports the model into an
isolated project through `UrsaAssetImport` and verifies the published model-v9
sub-assets. You can pass `-ModelPath`
with another self-contained `.glb` file that contains meshes, a skin, and
animations. `RiggedSimple` is published by the
[Khronos glTF Sample Assets project](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/RiggedSimple)
and is licensed CC BY 4.0; attribution is to Cesium.

模型目录格式 2 按 Mesh 保存有序材质槽数组，保留空槽及不同 SubMesh 的材质绑定。当前读取器只接受此版本；升级后重新导入，不加载旧目录格式。

模型 importer 版本为 9（内容目标 `model-v9`）。模型材质子资产与脚本生成材质统一使用当前 `.ursamat` 编码和加载器，保留 Shader、属性及纹理 GUID/local file ID；Mesh 数据版本 3 保存每网格 skin-local joint 到共享节点姿态的映射及逆绑定矩阵，兼容读取旧 Mesh 数据版本；AnimationClip 编码版本 2 保存轨道插值和三次切线。升级后重建 Library，源 GUID 和稳定输出 key 不变，Mesh、材质、骨骼和动画身份不随编码改变。


## Project 子资产与模型节点

展开模型源文件可以选择具名 Mesh、材质、内嵌 Texture、Skeleton 和 AnimationClip。主模型由源文件行代表，ModelNode 不作为平级资源展示；外部图片仍是它自己源文件的资产。Project 搜索支持可见子资产名称和类型，并显示所属模型源路径。

模型 Inspector 的 Hierarchy 按源父子关系显示空节点、网格节点和骨骼节点，选中节点可检查只读局部位置、旋转、缩放及具名网格关联。该检查不创建 Scene 对象，也不修改导入草稿。实例化模型后，实际 GameObject 节点可在场景 Hierarchy 中展开、选中并按既有文档流程保存。

选中 Skeleton 显示关节和绑定信息；选中 AnimationClip 显示时长、通道及其所属模型中的片段预览，复用现有播放、暂停和定位功能。无可呈现模型时保留片段信息并报告预览不可用，不把动画或骨骼数据当作模型加载。

Library 导入目录使用格式 4，旧目录需重新导入以获得名称和可见性。该升级不改变源 GUID、既有输出 key 或引用格式；原本含源名称/索引的 key 仍受既有源结构稳定性约束。

界面、字段引用和跨流程验证见 [Unity 风格导入资产验收](UnityImportedAssetsAcceptance.md)。
