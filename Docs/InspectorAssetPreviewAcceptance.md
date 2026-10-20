# Inspector 资产预览验收

执行日期：2026-10-19 至 2026-10-20。对应 OpenSpec 变更 `inspector-asset-previews`；环境信息采集于 2026-10-19。

## 版本与环境

- 工作区根仓库基线 `9243115`，Engine 子仓库基线 `5ad8eba`，包含当前未提交实现；不能用这两个提交单独复现本轮改动。
- Windows 11 家庭版中文版，版本 `10.0.26300`；检测到 NVIDIA GeForce RTX 4060 Laptop GPU，驱动 `32.0.15.9186`，以及 Oray 虚拟显示设备。
- Visual Studio 2022 / MSBuild `17.13.19`，CMake `3.28.0-rc4`，.NET SDK `10.0.100`，clang-format `19.1.1`；现有构建使用 Vulkan SDK `1.3.216.0`。
- `Engine/Build` 启用 Editor、测试与 .NET。配置中的 Demo 路径只供已有测试配置使用；预览、工作区和 SRP 验收使用生成的独立项目，不添加 Demo 资产。
- Debug 设备开启 Vulkan validation，设备空闲检查会将 validation 错误视为失败；Release 按正常产品配置运行。

## 执行方式与结果

从工作区根目录执行；每种配置单独保留构建、CTest 发现与回归日志。Debug 结果由首轮回归及后续针对失败/增强用例的补验共同构成；首轮日志中的旧失败保留，不将它改写为整轮通过。

```powershell
$previewTests = '^Ursa(AssetPreview(State|Panel|Workspace)|EditorPreview|ImGuiBridge|EditorMaterialInspector|ComponentEditors|MaterialDraft|TextureDraft|TextureImporter|AssetImportPipeline|AssetRefreshWindow|MeshAssetEditor|SceneDraft|EditorWorkspaceWindow|EditorDocuments|ProjectWorkspace|ProjectBrowserModel|ProjectModelImport|ProjectTextureImport|ProjectScenePrefabRoundTrip|EditorSrpAcceptance)Tests$'
foreach ($previewConfiguration in @('Debug', 'Release')) {
    cmake --build Engine/Build --config $previewConfiguration --parallel
    ctest --test-dir Engine/Build -C $previewConfiguration -R $previewTests --show-only=json-v1
    ctest --test-dir Engine/Build -C $previewConfiguration -R $previewTests --output-on-failure
}
./Engine/Tools/Format.ps1 -Check
```

CTest 发现结果应包含 22 个选定测试，加上自动纳入的两个内容准备 fixture。保留 fixture、超时和资源锁；不能把零匹配或未注册测试记为通过。

| 检查 | 最终状态 | 退出码 | 记录 |
| --- | --- | --- | --- |
| Debug 最终全量构建 | 通过 | 0 | [构建日志](../Build/Temp/InspectorPreviews-Debug-final-build.log) |
| Release 最终全量构建 | 通过 | 0 | [构建日志](../Build/Temp/InspectorPreviews-Release-final-build.log) |
| Debug 22 项回归及 fixture | 所选用例已全部取得通过证据，见补验说明 | 首轮 8；SRP 补验 0 | [发现结果](../Build/Temp/InspectorPreviews-Debug-discovery.json)、[首轮日志](../Build/Temp/InspectorPreviews-Debug-regression.log) |
| Release 22 项回归及 fixture | 24/24 通过 | 0 | [发现结果](../Build/Temp/InspectorPreviews-Release-discovery.json)、[回归日志](../Build/Temp/InspectorPreviews-Release-regression.log) |
| Engine 格式检查 | 通过，505 个 C++ 文件 | 0 | [格式日志](../Build/Temp/InspectorPreviews-format.log) |

首次受限环境构建停在 MSBuild 的文件检查，终止后使用可访问本机工具链的执行环境构建。最终日志中的编译/运行结果与这次环境限制分开记录。应用验收在发现忙状态的等待与 Pak 启动断言补齐后，另有 [Debug 验收构建](../Build/Temp/InspectorPreviews-Debug-acceptance-build.log) 和 [Release 验收构建](../Build/Temp/InspectorPreviews-Release-acceptance-build.log)。

Debug 首轮为 22/24，通过数包含两个 fixture；新工作区用例失败、SRP 用例超时。后续补验：

- 增强实际重导入压力后的 `UrsaAssetPreviewPanelTests` 通过，105.04 秒，见 [GPU 补验日志](../Build/Temp/InspectorPreviews-Debug-recheck.log) 的对应通过行；该文件还保留后续旧版应用用例的失败。
- `UrsaAssetPreviewWorkspaceTests` 的 Loose/Pak 共八个独立进程全部通过，287.92 秒，见 [应用补验日志](../Build/Temp/InspectorPreviews-Debug-application-recheck.log) 的通过行。其后的旧 SRP 运行被中止，不能把该日志整体记为退出码 0。
- 最终 `UrsaEditorSrpAcceptanceTests` 通过，321.35 秒；连同 fixture 为 2/2、退出码 0，见 [最终 SRP 补验日志](../Build/Temp/InspectorPreviews-Debug-srp-recheck.log)。

Release 整轮为 24/24、退出码 0，其中应用验收 71.25 秒，SRP 99.22 秒。随后画面抽查发现带权重 Mesh 缺少正确的绑定姿态矩阵，以及测试选择下拉首项时使用了错误的控件 ID。修复后新增骨骼矩阵、非空 GPU 像素和菜单关闭断言；[Debug 画面补验](../Build/Temp/InspectorPreviews-Debug-mesh-recheck.log) 为 2/2、退出码 0（面板 35.16 秒），[Release 画面补验](../Build/Temp/InspectorPreviews-Release-mesh-recheck.log) 为 2/2、退出码 0（面板 12.93 秒）。更新后的 200% Mesh 与 150% 透明纹理截图已人工抽查。

验收驱动修正了后台发现忙状态下的请求等待、过深的临时项目路径、同进程多次初始化 .NET、隐藏窗口恢复时机，以及 Run/Stop 请求在后台检查期间被拒绝的问题。独立进程验证真实持久化，SRP 按编译/渲染就绪状态推进，发送运行请求前先排空后台任务。冷启动编译预算调整为工作流 600 秒、CTest 900 秒，管线、图像、Dirty、最小化和多次运行断言均保留；`Progress.json` 记录阶段及诊断。

## 资源与显示密度矩阵

`UrsaAssetPreviewPanelTests` 在真实 Vulkan 目标上录制 ImGui 控件和鼠标事件，分别挂载松散 Engine 内容及只读 Engine Pak。每格均生成五类资产画面，包含完整模型和单独 Mesh 子资产、RGBA 透明纹理、动画精确片尾、隐藏/恢复与 210 逻辑像素宽 Inspector。

| 配置 | 资源 | 100% | 150% | 200% | 图像目录 |
| --- | --- | --- | --- | --- | --- |
| Debug | Loose | 通过 | 通过 | 通过 | [Loose](../Build/Temp/AssetPreviewEvidence/Debug/Loose/) |
| Debug | Pak | 通过 | 通过 | 通过 | [Pak](../Build/Temp/AssetPreviewEvidence/Debug/Pak/) |
| Release | Loose | 通过 | 通过 | 通过 | [Loose](../Build/Temp/AssetPreviewEvidence/Release/Loose/) |
| Release | Pak | 通过 | 通过 | 通过 | [Pak](../Build/Temp/AssetPreviewEvidence/Release/Pak/) |

每个目录中 `09-Texture-DPI-{100,150,200}.png`、`09-A.ursamat-DPI-*.png`、`09-Two.obj-DPI-*.png`、`09-Animated.fbx-DPI-*.png`、`09-Mesh-DPI-*.png` 和 `09-Probe.ursaprefab-DPI-*.png` 对应矩阵证据。`06-Animation-End.png`、`07-Transparent-Texture.png` 与通道、平移图像提供额外证据。每档 DPI 的模型时间轴验证精确片尾并检查暂停位置的隐藏/恢复；播放中的隐藏、折叠、非活动标签及零尺寸另有同一测试内的断言。

这些倍率通过逻辑尺寸与 framebuffer 尺寸之比提供，属于合成 DPI 的集成测试，并未切换用户 Windows 的全局缩放设置。完整 Editor 测试额外记录实际窗口的 framebuffer scale；系统级切换多台不同 DPI 显示器不在本次自动化证据内。

## 完整 Editor 与旧入口

`UrsaAssetPreviewWorkspaceTests.ps1` 在每种资源形态下启动四个独立 Editor 进程，共享该项目的用户设置和停靠布局：

1. 使用控件折叠 Preview、保存自定义 Inspector/Hierarchy 停靠布局，并准备逻辑高度 335 的用户设置。高度拖动本身由面板鼠标回归验证，应用级测试独立检查非默认设置恢复。
2. 重新启动并聚焦 Inspector，检查仍然折叠、非默认高度可恢复；注入旧 Material Preview 窗口记录，其他停靠关系保持。
3. 使用旧 Material Preview 启动入口，检查展开、焦点及实际画面，不生成第二个窗口。画面已进入当前 UI draw data 后切换到另一项目，再运行一帧，验证旧纹理仍可安全提交、新会话没有旧目标。
4. 重新启动原项目，读取部分损坏的设置，检查整体回退到展开/280 且有诊断。

项目中不创建 Scene；旧入口的画面检查证明预览不需要有效 Game 相机或打开作者 Scene。截取通过断言后的帧，每个生成项目保留 `Workspace-0.png` 至 `Workspace-3.png`。

| 配置 | Loose 应用验证 | Pak 应用验证 | 项目及截图 |
| --- | --- | --- | --- |
| Debug | 通过 | 通过 | [应用证据](../Build/Temp/AssetPreviewWorkspaceEvidence/Debug/) |
| Release | 通过 | 通过 | [应用证据](../Build/Temp/AssetPreviewWorkspaceEvidence/Release/) |

`UrsaEditorSrpAcceptanceTests` 使用实际项目管线，验证 Scene/Game/Inspector Preview 分支、脚本重载后的管线版本、最小化暂停以及多次 Run/Stop。它的生成项目与图像位于 `Engine/Build/TestProjects/SrpAcceptance/<配置>/<运行 GUID>/`，确切运行目录由回归日志记录。

### 实际命令行入口

最终构建后，在隔离的 `URSA_USER_DATA_ROOT` 中复制生成项目的停靠布局，并把其 Preview 用户设置准备为高度 335、折叠状态，分别执行：

```powershell
UrsaEditor.exe --project <生成项目> --asset Preview.ursamat --view Inspector --hidden --frames 12 --capture <Inspector.png> --capture-frame 10
UrsaEditor.exe --project <生成项目> --asset Preview.ursamat --view "Material Preview" --hidden --frames 12 --capture <MaterialPreview.png> --capture-frame 10
```

Debug 和 Release 的四次执行退出码均为 0。Inspector 入口保持全部准备好的 Preview 设置为折叠；旧 Material Preview 入口只展开当前项目对应的一份设置。该检查验证 Launcher 参数解析及持久化展开语义；实际三维画面和焦点由上面的完整应用验收检查。

结果及确切生成项目路径见 [CLI 结果 JSON](../Build/Temp/InspectorPreviews-cli-results.json) 与 [CLI 日志](../Build/Temp/InspectorPreviews-cli.log)，截图分别在 [Debug CLI](../Build/Temp/AssetPreviewCliEvidence/Debug/) 和 [Release CLI](../Build/Temp/AssetPreviewCliEvidence/Release/)。

## 状态、故障与寿命断言

- 状态测试区分请求与成功结果的内容、导入和管线版本；过期成功/失败和相同 GUID 的另一项目会话不能发布结果。
- 面板测试注入管线失败、无效 Shader、未应用导入设置、源图解码失败、缺失依赖与目标删除，检查诊断归属、同目标恢复、无跨目标旧图、无持续逐帧重试，以及源字节和作者 Dirty/对象数保持。
- 导入版本使用已发布 Library 产物的版本地址；未应用设置不会将查询时计算出的新请求指纹标为旧产物的版本。
- 设置回归覆盖坏 JSON、部分有效字段、非有限/越界高度、读取失败、写入失败、项目隔离与跨 DPI 恢复。
- UI 桥测试在退休图像后仍录制当前 draw data 并核对实际颜色，再于下一帧验证注册被释放。面板压力测试在已有 UI 提交时重复切换/重新加载，检查注册数回到基线及私有对象释放。
- 真实游戏生命周期探针保持零，joint palette 和实际 GPU 图像随动画采样变化；不是仅验证播放布尔值。

本记录中的 Build 目录是本机生成证据，不进入源控制；复核时保留该目录或按上述命令重新生成。要求的构建、回归、修复补验、入口及格式检查均已有通过证据；OpenSpec 严格验证同时执行。

## 2026-10-20 分隔条交互补验

Preview 分隔条增加可见手柄、10 逻辑像素命中区域和悬停/拖动高亮。实际布局为属性区保留至少 40 逻辑像素；窗口缩小保留原偏好，受限区域内拖动从实际显示高度起算，结束时保存。

```powershell
cmake --build Engine/Build --config Debug --target UrsaEditor UrsaAssetPreviewPanelTests UrsaAssetPreviewWorkspaceTests UrsaEngineFormatCheck -j 8
ctest --test-dir Engine/Build -C Debug -R '^UrsaAssetPreview(Panel|Workspace)Tests$' --output-on-failure
```

构建及格式检查退出码 0，检查 505 个 C++ 文件，见 [构建日志](../Build/Temp/PreviewResize-build.log)。CTest 连同内容 fixture 为 3/3 通过、退出码 0；面板 78.14 秒，工作区 117.46 秒，见 [测试日志](../Build/Temp/PreviewResize-tests.log)。新增真实鼠标断言覆盖扩大的命中区域、图像尺寸随拖动变化、矮窗口裁剪、受限布局内即时拖动、上下限及高度持久化。截图见 [Loose](../Build/Temp/AssetPreviewEvidence/Debug/Loose/09-Preview-Resize.png) 与 [Pak](../Build/Temp/AssetPreviewEvidence/Debug/Pak/09-Preview-Resize.png)。此次分隔条调整未补跑 Release；前述 Release 记录对应调整前的功能验收。
