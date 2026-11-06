# 运行场景与发布列表

一次 Player 启动或 Editor Run 拥有一个脚本环境和运行场景容器。`LoadScene` 替换场景图，保留静态字段和运行级资产。Stop/退出才关闭脚本环境；再次 Run 重新初始化静态字段。测试页需要重置自己的共享状态时应显式处理。

## BuildSettings

`ProjectSettings/BuildSettings.json` 使用版本化格式：

```json
{
  "format": "UrsaBuildSettings",
  "version": 1,
  "scenes": [
    { "enabled": true, "scene": { "guid": "场景的 .meta GUID", "localFileId": 0 } }
  ]
}
```

Scene 使用主资源稳定身份，启用项按声明顺序分配连续的 Build Index。禁用项不参与运行列表；启用项缺失、类型错误或重复身份会使导入失败。资源重命名后 GUID 保持不变。导入发布生成只读 `prj:__UrsaSettings/Scenes.json`，Player 从当前松散内容或 Pak 读取，不扫描源码。

需要目录驱动的项目可在 BuildSettings 中设置可选 `sceneCatalog: { guid, localFileId }`，引用项目 ScriptableObject。版本 1 的目录字段契约为 `menuScene`、`ids`、`titles`、`descriptions`、`scenes`；四个列表长度相同，ID/标题非空且 ID 唯一，菜单引用必须对应启用索引 0，其余 Scene 按启用顺序逐一对应。引擎只读取这个数据契约，不依赖项目类名或资源路径。导入和打包前的目录失配会阻止发布，保留上一有效 Ready 产物。

无 `--scene` 的 Player 选择首个启用 Scene。显式 `--scene` 优先，可使用发布列表中的名字或资产路径；旧项目没有 BuildSettings 时仍可显式指定资产路径。二者都缺失时启动失败并给出配置诊断。Editor 无显式启动资产且没有恢复文档时，回退打开第一个启用 Scene。IDE 默认调试参数只传项目目录。

## 公共 API

```csharp
using Ursa.SceneManagement;

Scene current = SceneManager.GetActiveScene();
SceneManager.LoadScene("Tests/OnGUI/OnGUI.ursascene");
// 也支持 Build Index 和 AssetReference：
SceneManager.LoadScene(1);
SceneManager.LoadScene(current.AssetReference);
```

`Scene` 提供 Name、Path、BuildIndex、AssetReference、IsLoaded，并以运行会话/加载代数比较身份。`SceneManager.IsLoading` 表示存在待处理请求；`LastLoadError` 保存最近加载诊断。错误请求会抛出异常，后续正确请求可清理错误并重试。同一目标的在途请求合并，不同目标不能覆盖已排队请求。

字符串查找使用发布清单中的名称或路径并区分大小写；同名 Scene 应使用完整路径、Build Index 或稳定引用消除歧义。

首版只支持 `LoadSceneMode.Single`；Additive 明确失败。LoadSceneAsync、DontDestroyOnLoad 和多场景编辑尚未提供。Ursa 遵循 PascalCase 命名；API 不承诺 Unity 全版本的所有边缘行为。

## 激活与失败边界

LoadScene 只排队。宿主在下一可绘制帧、离开脚本与 OnGUI 回调栈后准备候选图和资源，验证脚本类型、字段及引用。准备失败保留旧场景、输入与可操作性，不派发生命周期。

提交顺序为旧 OnDisable/OnDestroy、旧对象句柄失效、SceneUnloaded、新 Awake/OnEnable、SceneLoaded、Start。通知中再次请求加载只排到后续边界，不递归激活。SceneLoaded 的脚本委托逐个隔离异常。提交开始后，native/宿主致命错误结束运行，不声称已经回滚用户回调的副作用。

场景替换保留全局模拟时间/帧号，清除固定步积压及跨场景输入沿、GUI 焦点和捕获。加载耗时不在下一帧补步。GPU 在途资源仍由原提交 fence 保留。Editor Pause 中可切换并准备新生命周期，但不执行 Update/FixedUpdate；Scene/Game 同时显示新运行图，Stop 恢复整个 Run 最初的作者文档、Dirty 与选择。

## 验证

`UrsaSceneBuildSettingsTests` 验证列表解析、名称/路径、索引和稳定身份；`UrsaRuntimeSceneManagerTests` 验证队列、准备失败、代数、旧句柄及通知重入；`UrsaManagedGuiTests` 驱动真实托管按钮排队加载并验证静态保留、实例重建、旧 C# 对象拒绝访问、生命周期以及暂停导航。

Unity 对齐参考为本地 UnityCsReference 的 SceneManager.cs / Scene.cs / SceneManager.bindings.cs，以及官方 [LoadScene](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/scenemanagement/scenemanager/loadscene)、[sceneLoaded](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/scenemanagement/scenemanager/sceneloaded) 文档。LoadScene 延后激活，SceneLoaded 位于 OnEnable 和 Start 之间。Ursa 的激活边界由可绘制宿主帧定义；单次 Run 保留脚本环境，发布列表采用 GUID 引用。

Editor 的脚本候选规范化保留尚未解析的 GameObject、Component 和 ScriptableObject 字段身份，包括数组与 List 的空元素；不能在构建完整图之前将这些字段的默认空值写回作者文档。通知回调中重载当前活动 Scene 会进入后续边界，已经开始激活的请求不吞掉新的重载。
