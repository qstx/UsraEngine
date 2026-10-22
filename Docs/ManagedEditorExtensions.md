# C# 编辑器扩展

项目编辑器脚本放在 Assets 中含有 Editor 目录段的路径下，例如 Demo 的 Assets/Scripts/Editor/MoveWithInputInspector.cs。Editor 组引用 Game、Ursa.Runtime 和 Ursa.Editor；Player 仅加载 Game 组。

## 自定义 Inspector

继承 Ursa.Editor.Inspector，使用 `CustomEditor(typeof(组件类型))` 匹配目标：

    using Ursa.Editor;

    [CustomEditor(typeof(MoveWithInputBehaviour))]
    public sealed class MoveInspector : Inspector
    {
        public override void OnInspectorGUI()
        {
            SerializedObject.Update();
            var step = SerializedObject.FindProperty("step");
            EditorGui.PropertyField(step, "Step");
            if (EditorGui.Button("Reset"))
            {
                step.DoubleValue = 1.0;
            }
            SerializedObject.ApplyModifiedProperties();
        }
    }

脚本组件的 `Type` 在当前编辑会话中解析为类型目录的稳定标识；原生组件通过包装类型声明的标识匹配，例如 `[CustomEditor(typeof(CameraComponent))]`。类型转换在注册时完成，场景和 `.meta` 中的稳定标识不变。目标支持组件、独立 Prefab 的 GameObject、ScriptableObject、Mesh、Material、Texture 及 AssetImporter。注册类型必须闭合；确切类型优先，`CustomEditor(typeof(BaseType), editorForChildClasses: true)` 可匹配派生类型，并选择距离最近的声明；同优先级歧义会使注册失败。缺少稳定类型标识同样报错。旧的字符串属性 `CustomInspector` 已移除。

原生 Camera 可编辑字段包括 `verticalFovRadians`、`nearPlane` 和 `farPlane`。原生字段先在临时组件上验证，再提交到原对象。脚本属性校验类型、数值范围、枚举、引用和数组元素；失败时保留原值。

DrawDefaultInspector() 可显示其余持久字段。成功 Apply 会标记文档 Dirty，并进入既有 Scene、独立 Prefab 保存和 Prefab 实例覆盖流程。未注册自定义 Inspector，或扩展被停用时，使用默认视图。

## 菜单与窗口

菜单使用 MenuItem，方法必须为无参数的静态 void 方法。窗口继承 EditorWindow 并声明入口路径：

    [MenuItem("Tools/Project/Save")]
    private static void SaveCurrent()
    {
        EditorContext.Save();
    }

    [EditorWindowMenu("Tools/Project/Overview")]
    public sealed class Overview : EditorWindow
    {
        public override void OnGUI()
        {
            EditorGui.Text("Assets: " + EditorContext.Assets.Length);
            foreach (var selected in EditorContext.Selection)
            {
                EditorGui.Text(selected.Name + " (" + selected.Reference.Kind + ")");
            }
        }
    }

EditorContext 提供选择、当前文档和资产信息，以及 MarkDirty、Save、OpenAsset。OpenAsset 使用 VFS 路径，并在界面安全边界处理文档切换。Run 中现有保存门槛仍生效。

Selection 为 `EditorSelection[]`，Target 为可空 `EditorReference`，Document 为可空 `EditorDocument`，Assets 为 `EditorAsset[]` 快照。文档含 Id/Path/Kind/Dirty；引用含 Kind/PersistentId/TypeId 和可空 AssetReference，资产与子资产保留 GUID/local file ID。`ResolveObject(kind, persistentId)` 与 `ResolveAsset(asset, kind)` 在当前编辑图/资产加载器内校验身份；项目不能构造 native 地址。引用的描述可读取，引用控件或属性写入仍要求当前回调身份。

## 类型化属性

`FindProperty` 使用 Unity 风格的公开路径：`FindProperty("width")` 查找字段，`FindProperty("position.x")` 查找向量分量，`FindProperty("modes.Array.data[1]")` 查找数组或列表元素。`SerializedProperty.PropertyPath` 和 `GetPropertyPaths()` 也返回此语法；`FindPropertyRelative("x")`、`FindPropertyRelative("Array.data[1]")` 分别用于相对分量和相对元素查找。向量和 Quaternion 使用 `x/y/z/w` 分量名，Color 使用 `r/g/b/a`。当前支持现有字段类型的分量和集合元素，不新增自定义嵌套对象的序列化能力。无效路径会报错；公开 API 不接受旧的 `/Fields/...` 存储路径，该格式仅用于内部传输和持久化数据。

`PropertyType`、`TypeName`、`ReferenceKind` 描述可用访问器。标量使用 BoolValue、IntValue/LongValue、UIntValue/ULongValue、FloatValue/DoubleValue、StringValue；数学值使用 Vector2Value/Vector3Value/Vector4Value/QuaternionValue/ColorValue。EnumNames、EnumValueIndex 提供选项，EnumLongValue/EnumULongValue 按 EnumIsUnsigned 保留完整 64 位底层数值。错误访问器抛出异常，不转换为另一种字段类型。

对象/组件使用 ObjectReferenceValue，资产及 Mesh/Material/Texture/ScriptableObject/Shader/ComputeShader 使用 AssetReferenceValue。引用种类必须匹配，文档局部 Mesh/Material 身份仍可读取、保留与清空。数组/列表通过 ArraySize、ElementType 和 GetArrayElementAtIndex 编辑；设置 `IsNull = true` 保留可空状态，扩容使用元素类型的默认值，顺序不变。

Update 重新捕获目标并丢弃未应用编辑；调用后须重新 FindProperty，旧属性版本会被拒绝。HasModifiedProperties 查询候选编辑。ApplyModifiedProperties 统一验证类型、数值范围、引用与所有集合元素；失败不部分修改目标或 Dirty，目标被外部修改时要求先 Update。读取和描述访问不产生持久修改。未知字段原样保留并在默认视图显示为不支持。

## 控件与会话约束

EditorGui 支持文本、按钮、数字、布尔、字符串、枚举选项、向量、旋转、颜色、对象／资产选择，以及统一 PropertyField 的数组／列表编辑。Vertical 与 SameLine 提供基本布局。不暴露 ImGui、窗口或图形后端指针。

Editor API 限主线程，且仅在菜单、窗口或 Inspector 回调内有效。不要把 SerializedObject、SerializedProperty 或布局对象留到下次回调；需要长期保存的工具状态使用普通 C# 数据、属性路径和持久 GUID。

Editor 模块由 `NativeEditor.bindings.cs` 中的具体方法生成，按一次回调安装并在退出时撤销。宿主扩展目录与调用也使用生成的 ManagedHost 回调，普通控件及属性传输不经过 JSON operation/exchange。GUI 控件要求活动 ImGui 帧；菜单中的保存、文档查询不需要 GUI 帧。异常退出会平衡未释放的布局组。

注册、项目类型、实例与委托属于对应编辑会话。候选重载验证通过后替换注册；失败保留旧环境。异常会记录扩展标识和堆栈并停用该扩展，其余面板继续运行。关闭项目、重载及退出会注销会话注册并检查上下文卸载。

## 示例与验证

Demo 示例包含移动组件 Inspector、保存菜单和项目概览窗口。[可编译的 Editor 示例](Examples/Editor/TypedInspector.cs) 随 Ursa.EditorFixtures 构建。UrsaEditorBindingsTests 检查生成入口的类型、集合、引用、版本、事务及失效 token；宿主集成测试使用实际 ImGui 帧验证控件绘制、脚本／原生属性 Apply、VFS 保存、线程限制、异常隔离、旧属性句柄及会话注销。完整窗口交互验收单独记录，不能以这些测试代替人工操作证据。


## 源 ScriptableObject 属性

`.ursaasset` 默认 Inspector 使用与组件/importer 相同的 `SerializedObject` 服务。`Update`、字段/集合编辑、引用校验和 Apply 构成原子草稿事务；`Save ScriptableObject` 或 `EditorContext.Save()` 才写入源文件并排队导入。保存重新检查源 GUID、源字节和项目写入租约，拒绝外部修改冲突。未知字段保留，当前目录 schema 决定字段类型、枚举值、默认值与旧字段别名。只读包和 Library 导入产物不能通过该服务改写。

资源 `CustomEditor` 可以把 `Inspector.Target` 转为实际的 ScriptableObject 类型，并读取/修改其字段。字段变化在回调结束、`EditorContext.MarkDirty()` 或 `EditorContext.Save()` 时进入同一属性事务；未修改的 Target 字段不会覆盖期间通过 SerializedObject 提交的其他编辑。Target 是当前回调的字段快照，回调结束后失效，不占用运行资产缓存，也不执行游戏实例的 OnEnable/OnDisable/OnDestroy。资源保存不会标记另一个活动 Scene/Prefab 文档 Dirty。

Importer 的类型化 Target 及 Apply/Revert 示例见 [脚本导入与处理器](ScriptedAssetImporters.md)。


## 类型化组件、材质和 Prefab

`Inspector.Target` 可转换为注册所匹配的组件、ScriptableObject、Material 或独立 Prefab 的 GameObject；AssetImporterEditor 继续提供 AssetImporter 类型的 Target。组件的 GameObject/Transform 与原生组件属性使用当前回调的对象访问层，脚本字段使用当前序列化快照。`GetComponent<T>()` / `GetComponents<T>()` 可取得同一作用域中的脚本组件，修改其持久字段同样提交到统一属性事务；它们不创建游戏 Behaviour 实例或执行 Awake/Start/Update。

材质 Target 在私有候选上操作，回调成功或显式 Save 时把变化提交到源材质草稿。默认材质字段来自实际 Shader 声明，保留显示名、Range、HDR 和 HideInInspector；清空纹理引用恢复 Shader 默认值，未知覆盖与其他状态保留。已有原生 Shader 自定义面板仍可注册。材质与 Prefab 选中时使用自己的源文档；该资源已打开时复用对应草稿，另一个作者文档保持独立。Save Asset、EditorContext.Save 和 File/Save 使用对应源保存命令。未保存的独立资产草稿切换选择或退出前提供 Save/Discard/Cancel。

回调结束后，临时对象、属性及组件包装失效。被读取的依赖 ScriptableObject 使用独立临时对象缓存，既不污染编辑会话原缓存，也不触发快照的 OnEnable/OnDisable/OnDestroy。通过显式 CreateInstance 创建的对象保留自己的初始化/清理回调。包及只读源不接受字段提交或保存；读取的持久 Mesh/Material 必须先 Clone 才能修改，不能通过资源引用改写其他已发布资产。


## 资源 Preview 与生命周期

`Inspector` 的 `OnEnable` / `OnDisable` 配对管理当前所选资源的查看状态。`OnInspectorGUI` 与该目标的 Preview 回调使用同一个实例，因此相机角度、显示模式等普通 C# 字段可以共享。每次回调重新取得 `Target` 和 `SerializedObject`，不能缓存回调包装；持久身份使用 `AssetReference`。Project 展开源文件后可独立选择主对象及子资产，稳定 GUID/local file ID 也用于拖拽引用。导入产物 Inspector 只读，不提供源保存或 importer Apply。

覆写 `HasPreviewGUI()` 返回 true 后，宿主调用 `OnPreviewSettings()` 绘制工具栏，调用 `OnInteractivePreviewGUI(Rect)` 绘制图像。后者默认转发给 `OnPreviewGUI(Rect)`，`GetInfoString()` 提供说明；`RequiresConstantRepaint()` 适用于动画。没有自定义 Preview 时保留材质、纹理、模型、Mesh、Prefab 的默认预览。importer 编辑器的 Target 是 importer，其 Preview 可用已发布资源引用；独立产物的 Target 则是对应资源类型。

`Rect` 使用当前预览图像区域内的逻辑坐标，宿主负责裁剪、窗口 DPI 和 GPU 尺寸。二维图像使用 `EditorGui.DrawTexture(rect, texture)`；`new Texture2D(width, height, linear)` / `SetPixels` 可创建查看用 CPU 图像，或调用 `DrawPreviewPixels`。RGBA 的透明度和线性/sRGB 信息保留。布局控件仅用于属性和 `OnPreviewSettings`，图像回调使用给定 Rect。

三维预览使用 `PreviewRenderUtility`：在 `OnEnable` 创建，在 `OnDisable` 调用 `Cleanup`；每次绘制先 `BeginPreview(rect)`，再 `DrawMesh` 或 `InstantiatePrefab(assetReference)`，最后 `EndPreview()`。`Camera` 的 Position/Target/Up、FieldOfView（角度）及裁剪面可配置，AmbientColor/Intensity、LightColor/Intensity、LightRotation 控制私有灯光。每个 utility 的场景、相机、灯光、实例与作者 Scene/Play Scene 隔离；运行中的 SRP 以 Preview 相机类型绘制，窗口共用既有 Inspector 页脚和渲染调度。当前一次图像回调发布一幅二维图像或一个 utility 结果。

`PreviewInput.Current` 提供图像内 MousePosition、拖动时的 MouseDelta、悬停滚轮 ScrollDelta、Dragging、可见时间 Time 和 DeltaTime。隐藏、折叠、零区域及最小化时停止绘制和时钟，恢复首帧 DeltaTime 为零。输入不传递给 Game/Gizmo；属性或工具栏控件变化会请求重绘，持续重绘不会把当前内容标成过时。临时 Mesh/Material 和 utility 可以跨该 Editor 的 Preview 回调使用；不能跨 Editor 实例、线程或关闭后的环境使用。已发布资源只读，克隆后才可修改。查看操作不保存源文件、不标记作者 Dirty，也不执行游戏生命周期。

同一目标绘制失败时，显示扩展 ID、资源 GUID/local file ID 和异常，保留并标识最后成功图像；切换目标后清空旧图像。失败的 Preview 不静默回退为默认成功结果。重新加载脚本或重新导入可重建该实例。成功脚本重载先完成旧实例 `OnDisable` 再关闭其会话；失败编译继续保留旧环境。关闭项目、切换目标和提交中的选择变化均持有必要资源到渲染结束，并撤销对应图像注册。

完整可编译示例见 [GeneratedFormatAssetEditor](Examples/Editor/GeneratedFormatAssetEditor.cs) 与 [示例 README](Examples/README.md)。无窗口的生命周期/能力测试与真实 GPU、DPI、SRP 窗口测试分别记录在 [验收记录](ScriptedAssetEditorExtensionsAcceptance.md)。
