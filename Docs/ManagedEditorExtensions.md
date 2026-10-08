# C# 编辑器扩展

项目编辑器脚本放在 Assets 中含有 Editor 目录段的路径下，例如 Demo 的 Assets/Scripts/Editor/MoveWithInputInspector.cs。Editor 组引用 Game、Ursa.Runtime 和 Ursa.Editor；Player 仅加载 Game 组。

## 自定义 Inspector

继承 Ursa.Editor.Inspector，使用稳定脚本标识或已暴露的原生组件标识匹配目标：

    using Ursa.Editor;

    [CustomInspector("demo.move-with-input")]
    public sealed class MoveInspector : Inspector
    {
        public override void OnInspectorGUI()
        {
            var step = SerializedObject.FindProperty("/Fields/step");
            EditorGui.PropertyField(step, "Step");
            if (EditorGui.Button("Reset"))
            {
                step.SetValue(1.0);
            }
            SerializedObject.ApplyModifiedProperties();
        }
    }

原生 Camera 的目标标识为 ursa.Camera，可编辑路径包括 /Fields/verticalFovRadians、/Fields/nearPlane 和 /Fields/farPlane。原生字段先在临时组件上验证，再提交到原对象。脚本属性校验类型、数值范围、枚举、引用和数组元素；失败时保留原值。

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
            EditorGui.Text("Assets: " + EditorContext.Assets.GetArrayLength());
            foreach (var selected in EditorContext.Selection.EnumerateArray())
            {
                EditorGui.Text(selected.GetProperty("Name").GetString() ?? "");
            }
        }
    }

EditorContext 提供选择、当前文档和资产信息，以及 MarkDirty、Save、OpenAsset。OpenAsset 使用 VFS 路径，并在界面安全边界处理文档切换。Run 中现有保存门槛仍生效。

## 控件与会话约束

EditorGui 支持文本、按钮、数字、布尔、字符串、枚举选项、向量、旋转、颜色、对象／资产选择，以及统一 PropertyField 的数组／列表编辑。Vertical 与 SameLine 提供基本布局。不暴露 ImGui、窗口或图形后端指针。

Editor API 限主线程，且仅在菜单、窗口或 Inspector 回调内有效。不要把 SerializedObject、SerializedProperty 或布局对象留到下次回调；需要长期保存的工具状态使用普通 C# 数据、属性路径和持久 GUID。

注册、项目类型、实例与委托属于对应编辑会话。候选重载验证通过后替换注册；失败保留旧环境。异常会记录扩展标识和堆栈并停用该扩展，其余面板继续运行。关闭项目、重载及退出会注销会话注册并检查上下文卸载。

## 示例与验证

Demo 示例包含移动组件 Inspector、保存菜单和项目概览窗口。宿主集成测试使用实际 ImGui 帧验证控件绘制、脚本／原生属性 Apply、VFS 保存、线程限制、异常隔离、旧属性句柄及会话注销。完整窗口交互验收单独记录，不能以这些测试代替人工操作证据。
