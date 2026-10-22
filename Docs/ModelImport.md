# Model import

The Editor imports FBX and OBJ source files through VFS. Source files remain in
`Assets/`; versioned model, node, mesh, material, and embedded-texture artifacts
are published to the rebuildable project `Library` cache. Each model keeps its
asset GUID, while imported sub-assets use stable local file IDs.

Select an FBX or OBJ source in the Project panel to open **Model Import**. The
Inspector shows import settings, the imported hierarchy and node transforms,
sub-assets, dependencies, and importer diagnostics. Change settings and choose
**Reimport** to update the cached artifacts. A failed import keeps the last
successful cache available and reports the source path and error.

模型放入 Scene 或 Prefab 时，静态物体使用 MeshFilter + MeshRenderer，蒙皮物体使用 SkinnedMeshRenderer + Animator。一个源节点包含多个网格时，按源顺序创建渲染子对象，保留原节点变换和网格/材质子资产 local IDs。几何 artifact 使用通用 `.ursamesh`；默认材质分配保存在模型实例化记录中。导入器版本为 7，使用统一稳定产物 key 和显式非零主对象 ID；升级后显式重导入重建 Library，旧子资产 ID 不提供兼容映射。

`ModelImporter::BuildCandidate` 只构造 CPU 候选：节点层级和局部变换、Mesh、材质与纹理绑定、内嵌纹理以及骨骼/动画引用。`ModelImportCandidate::Encode` 在最终修改后统一校验并编码，节点改名不会重分配输出 key；缺失节点、环、失效几何和错误绑定在发布前拒绝。构造期间产生的外部纹理依赖与主模型纳入同一候选目录批次。该原生接口为后续脚本后处理提供基础，公开 C# 处理器仍由对应扩展任务接入。

Mesh Clone/另存保留骨骼 GUID/local file ID；骨骼身份不取决于网格资产 GUID。资源会话显式重载会发布新几何版本并保留共享对象身份，独立副本不被源重导入覆盖。松散 Runtime 内容和 pak 使用同一目录映射加载 Mesh，普通 CMake 构建只编译代码，资源准备使用 Editor 导入或 `UrsaAssetImport`。

Supported settings currently include scale factor, generated normals and
tangents, UV flipping, material import, and texture import. Materials use the
engine's built-in PBR Shader. The rig setting retains one to four influences
per vertex, with four as the default. Supported material values include base color,
emissive color, metallic and roughness factors, opacity, and common color,
metallic-roughness, normal, occlusion, and emissive textures. External texture
files are independent project assets; embedded FBX textures become texture
sub-assets of the model. Unsupported source material properties appear in the
Inspector diagnostics.

FBX can carry node hierarchy, transforms, materials, textures, skinning, and
animation. FBX import records skeleton joints, inverse bind matrices, up to four
normalized joint influences per vertex, and named animation clips with node
keyframes. Adding an animated model to a Scene or Prefab adds a SkinnedMeshRenderer
for each skinned mesh and an Animator on the model root, initialized to the
first imported clip. Animator exposes direct Play, Stop, time, speed, and loop
controls; it does not require an Animator Controller. OBJ imports static
geometry, groups, MTL materials, and texture references; OBJ does not define
skeletal animation.

Runtime pose evaluation runs during component Update before rendering. The
renderer uploads each current joint palette to a storage buffer and applies
four-weight GPU skinning in the PBR vertex shader. Animator Controller graphs,
blend trees, retargeting, root motion, and morph-target animation are not
supported yet.

Selecting a model now opens its complete hierarchy in [Inspector Preview](EditorAssetPreviews.md), without adding it to the authoring Scene. Whole-model and stable Mesh sub-asset views support orbit, zoom and reset. Animated FBX previews start in bind pose and support clip selection, play/pause, looping, positive speed and paused scrubbing through the exact endpoint. This uses a private clock and explicit pose sampling, without Scene Update or gameplay callbacks. Prefab previews similarly use private read-only snapshots; opening or instantiating remains an explicit authoring action.

`UrsaDemoModelImportTests` imports and places the supplied `Cube.fbx` into a
Scene, saves and reopens its hierarchy, and checks an OBJ/MTL fixture using
`UrsaEngine.png`, an embedded-texture FBX fixture from the pinned Assimp
submodule, stable sub-assets, dependency tracking, missing-file diagnostics,
and preservation of the last successful model artifact. An animated FBX fixture
checks clip playback state, bind-pose and animated-pose pixels, and scene
save/reload without an Animator Controller. The OBJ model is also saved and
reopened as both a Scene and a Prefab.

模型目录格式 2 按 Mesh 保存有序材质槽数组，保留空槽及不同 SubMesh 的材质绑定。当前读取器只接受此版本；升级后重新导入，不加载旧目录格式。

模型 importer 版本为 8（内容目标 `model-v8`）。模型材质子资产与脚本生成材质统一使用当前 `.ursamat` 编码和加载器，保留 Shader、属性及纹理 GUID/local file ID；不再读取旧的裸 CBOR 材质产物。升级后重建 Library，源 GUID 和稳定输出 key 不变，Mesh、材质、骨骼和动画身份不随编码改变。
