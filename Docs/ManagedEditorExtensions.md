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

脚本组件的 `Type` 在当前编辑会话中解析为类型目录的稳定标识；原生组件通过包装类型声明的标识匹配，例如 `[CustomEditor(typeof(CameraComponent))]`。类型转换在注册时完成，场景和 `.meta` 中的稳定标识不变。目标必须是非开放泛型的组件类型，按确切类型匹配；重复注册或缺少类型标识会报错。旧的字符串属性 `CustomInspector` 已移除。

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

Editor 模块由 `NativeEditor.bindings.cs` 的 36 个具体方法生成，按一次回调安装并在退出时撤销。宿主扩展目录与调用也使用生成的 ManagedHost 回调，普通控件及属性传输不经过 JSON operation/exchange。GUI 控件要求活动 ImGui 帧；菜单中的保存、文档查询不需要 GUI 帧。异常退出会平衡未释放的布局组。

注册、项目类型、实例与委托属于对应编辑会话。候选重载验证通过后替换注册；失败保留旧环境。异常会记录扩展标识和堆栈并停用该扩展，其余面板继续运行。关闭项目、重载及退出会注销会话注册并检查上下文卸载。

## 示例与验证

Demo 示例包含移动组件 Inspector、保存菜单和项目概览窗口。[可编译的 Editor 示例](Examples/Editor/TypedInspector.cs) 随 Ursa.EditorFixtures 构建。UrsaEditorBindingsTests 检查生成入口的类型、集合、引用、版本、事务及失效 token；宿主集成测试使用实际 ImGui 帧验证控件绘制、脚本／原生属性 Apply、VFS 保存、线程限制、异常隔离、旧属性句柄及会话注销。完整窗口交互验收单独记录，不能以这些测试代替人工操作证据。
