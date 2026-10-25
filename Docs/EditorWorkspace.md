# UrsaEditor 工作区

UrsaEditor 使用可停靠面板。首次打开项目时，Hierarchy 位于左上，Project 与 Console 位于底部，Scene 与 Game 共享中央区域，Inspector 位于右侧。拖动面板标签可以调整停靠关系；通过 Window 菜单可以显隐面板，Reset Layout 会恢复默认布局。

窗口布局按项目保存在当前用户设置中；面板显隐状态保存在项目 `Library/Editor/Workspace/PanelVisibility.json`。清理项目 Library 会重置面板显隐设置；布局文件独立保存在用户数据目录。

主窗口标题按 `项目 - 当前文档 - Ursa Editor (版本)` 显示；未打开文档时显示 `Untitled`，未保存文档的名称后带 `*`。切换、保存或重命名文档后会更新标题。Play 中的游戏脚本不会覆盖 Editor 标题。

默认窗口图标使用 `Editor/Resources/UrsaEditor.png`，保留原有熊头标志，采用紧凑的 256×256 深色圆角底板和透明外缘，构建时嵌入 Editor。窗口显示前会完成图标解码和设置，避免启动时短暂出现系统默认图标。可以通过 `UrsaEditor --project <项目目录> --icon <PNG路径>` 指定其他窗口图标；嵌入 Editor 的宿主也可以调用 `EditorApplication::SetWindowIcon`。

Windows 的 `UrsaEditor.exe` 同时嵌入 `Editor/Resources/UrsaEditor.ico`，包含 16、24、32、48、64、128 和 256 像素版本；GLFW 创建原生窗口时也会加载此资源。修改默认 PNG 后，运行 `Tools/GenerateEditorIcon.ps1` 同步生成 ICO，再重新构建 Editor。`--icon` 只覆盖运行中的窗口图标。

## 菜单与运行控制

- **File**：保存当前文档、关闭文档或退出 Editor。关闭未保存文档时会询问保存、丢弃或取消。
- **Edit**：Undo、Redo 目前保持禁用；External Tools 在打开时显示工具窗口。
- **Assets**：刷新资源、创建文件夹/场景/Prefab/材质/纹理，以及重命名单个可写源资源（F2）。
- **GameObject**：在当前 Scene 的选中节点下创建空对象；未选节点时创建 Scene 根对象。
- **Component**：按类别显示当前 GameObject 可添加的组件，包括项目注册的 C# 组件。
- **Window**：显隐 Project、Hierarchy、Inspector、Scene、Game 和 Console，并重置工作区。面板背景菜单也提供 Close Panel 和 Reset Layout。
- **Tools**、项目注册菜单和工具窗口：调用 Engine 或项目 C# 扩展。
- 顶部工具栏提供 Play、Pause/Resume、Step 和 Stop。Step 只在暂停时可用。

### 常用快捷键与上下文菜单

快捷键仅在对应编辑器面板拥有焦点且文本输入/搜索/模态控件未消费键盘输入时生效。命令执行时会重新检查当前选择和操作资格。

| 操作 | 快捷键 | 菜单入口与限制 |
|---|---|---|
| 保存当前文档 | Ctrl+S | File > Save；没有可保存文档时不会执行 |
| 刷新项目资源 | Ctrl+R | Assets > Refresh；只对已打开工作区可用 |
| 重命名 GameObject | F2 | Hierarchy 聚焦且单选可用；也可从选中行菜单选择 Rename |
| 重命名源资产 | F2 | Project 聚焦且单个可写源资产可用；导入子资产、多选和只读资产禁用 |
| 复制 GameObject | Ctrl+D | Hierarchy 聚焦且 Edit 模式下的有效对象选择可用；选中行菜单会复核资格 |
| 删除 GameObject | Delete | Hierarchy 聚焦、Edit 模式且未聚焦文本输入时可用；删除前检查图内引用 |

右键菜单按命中目标捕获上下文：Hierarchy 行保留当前选择组，空白区以根级创建为目标；Project 资源/目录/空白区分别提供适用的 Open、Rename、Move、Delete、Reimport 或 Create；组件标题提供 Remove Component；Scene 画面提供 Create Empty；Console 日志提供 Copy Message 和有上下文时的 Locate。引用字段旁的 `...` 提供 Select、Clear 和可用时的 Locate，Select 打开带搜索、候选预览和接受/取消的对象选择器。面板背景菜单的 Close Panel 只改变面板可见状态，Reset Layout 恢复工作区布局。

Undo/Redo 仍不可用并以禁用状态显示；文档/About 等尚未实现的帮助入口也显示为禁用。未实现操作不会作为可执行快捷键注册。

### 菜单交互验收

Release UI 验收以 ImGui 多帧输入验证真实右键和菜单项点击：`UrsaReferencePickerTests` 覆盖引用字段 Clear、Select、Locate，并检查 Locate 不遗留选择弹窗；`UrsaEditorIconsTests` 覆盖 Console 日志右键复制/定位、导航上下文传递，以及关闭面板不改变文档 Dirty/Revision。该组在 2026-10-25 Release 配置通过。Undo/Redo 与未实现帮助入口通过菜单禁用状态和命令注册点检查；编辑操作不创建撤销历史。

## Project 面板

Project 左侧显示 `Assets` 文件夹树，右侧显示所选文件夹中的直接内容。展开箭头展开子目录；选择文件夹会更新内容视图。顶部路径按钮可以返回 Assets 或跳到上级目录。搜索会按资源名称和相对路径查找项目资产。

单击资源会选中它并在 Inspector 显示路径、GUID、资源类型和导入器信息。双击资源或在右键菜单选择 Open，会打开对应的 Scene、Prefab、材质、纹理或导入器视图。右键文件夹可在该目录下创建资源或子文件夹。选中单个可写源资源后按 F2，或从右键菜单选择 Rename Selected，可打开重命名对话框；多选、导入子资源和只读资源不可用。`.meta` 文件由资产数据库管理，不单独显示。

创建资源时，留空 Asset path 会在当前文件夹使用默认名称。创建文件夹时，留空 New folder name 会创建 `New Folder`。重命名时在 Asset path 中输入目标虚拟相对路径；资产 GUID 在移动或改名后保持不变。

可写 Scene 中的 Prefab/Model 主资产可拖入 Hierarchy 或 Scene 画面实例化；Scene 画面按当前像素射线选择最近可见几何落点，没有命中时落到编辑相机目标平面，画面上的标记显示即将使用的位置。工具栏、画面留边和 Game 视图不接受该操作。拖动 Hierarchy 中一个或多个最外层 Scene 子树到 Project 内容区，会为每个根创建独立 Prefab；父子同时选中时仅导出外层根。导出的对象、组件身份和可表达的本地资源引用重新映射，跨子树对象引用无法保留时整批拒绝并显示原因。导出不会替换、删除或改成 Prefab 实例化原 Scene 对象；新 Prefab 写入失败时会清理本批新文件和 sidecar。

## Inspector 与 Hierarchy

Hierarchy 只显示 Scene 或 Prefab 中的 GameObject 层级。单击一行选择对象；Ctrl/Cmd 单击可增减选择，Shift 单击可按当前可见行范围选择，Ctrl/Cmd+Shift 会将范围加入已有选择。过滤和折叠后的范围只包含当前可见行。主选以行高亮和焦点显示；点击另一面板的对象或资产会切换选择域。Inspector 显示多选数量、公共名称/Active/Transform 和公共组件；混合字段在显式编辑后才会写入，分量编辑保留每个对象未编辑的坐标。Scene Gizmo 仍只操作主选。

Project 面板支持同样的 Ctrl/Cmd 多选和 Shift 可见范围选择。对象与资源选择互相替换，不会混在同一组。Hierarchy 行按住后拖动不会在拖拽开始时改变 Inspector 目标；松开且未形成拖拽才发布点击选择。右键未选中的行会先单独选中它，右键已选行会保留选择组供菜单操作。资源的稳定身份使用 GUID 与子资源 local ID；Hierarchy 对象使用文档及对象身份。资源移动后选择跟随 GUID，删除或目标失效后会保留可诊断的缺失条目并从可操作目标中排除，同时选择有效主选。切换 Play 时 Hierarchy 选择绑定到运行副本，Stop 后恢复 Edit 选择。脏 importer 草稿阻止切换到另一目标；取消保留原选择及高亮。

拖放按来源和落点执行完整性校验：资产或对象拖入引用字段时必须恰好一项且类型兼容；组件引用字段可在所有者对象下消歧组件。支持的引用集合先校验全组再按来源顺序追加。Hierarchy 可将对象组拖到行上方/下方插入、行中央作为子级，或拖到根节点空白区；父子重叠来源只移动最外层对象，拖放保留世界姿态。跨 Scene/Play 文档、移动到自身后代、失效对象、奇异父矩阵或无法表示为 TRS 的剪切会拒绝整组，并在 Hierarchy 显示失败原因。拖动期间只使用稳定身份和会话 token；Escape、焦点/拖放上下文结束、无效释放及来源失效都会结束会话，释放后的帧不会重复执行命令。Hierarchy 的 Scene Gizmo 仍只操作主选，不参与层级拖放。

将面板标签拖到主窗口外可创建独立平台窗口；把标签拖回工作区可重新停靠。浮动面板继续使用同一 Editor 文档、选择状态和 ImGui 上下文。各 viewport 使用所属窗口的 Swapchain 与 DPI，窗口布局保存在用户设置中，不会标记 Scene 或资产草稿为 Dirty。

选择多个源 Material 或同一精确脚本类型的 ScriptableObject 时，Inspector 显示共同可编辑字段；Material 字段按 Shader 属性身份求交集，ScriptableObject 仅绘制当前所有目标都支持的基础类型。单次字段编辑以全组事务提交，**Save Selected** 保存全部脏目标。导入子资产只读，导入器和混合类型多选显示限制说明。资产 Inspector 底部预览始终显示主选资源，不改变属性编辑目标组。未实现聚合绘制的集合或对象引用会明确标注，不会被写入。

材质、纹理、模型、Mesh 和 Prefab 共用 Inspector 底部可折叠、可调整高度的 Preview。查看资产不会切换作者文档或要求保存；匹配的材质、纹理和 Prefab 草稿可预览未保存值。模型包含选片、播放/暂停及时间轴。展开偏好和逻辑高度按项目保存到用户设置，坏设置整组回退到展开/280，写入失败保留本次内存状态并提示。操作与隔离规则见 [资产预览](EditorAssetPreviews.md)。`--view Inspector` 保留折叠偏好；`--view "Material Preview"` 兼容入口在读取偏好后展开并聚焦 Inspector，不创建独立窗口或重置其他停靠面板。

Scene 面板提供 Transform Gizmo。Game 面板在 Play 模式显示运行画面；编辑模式下显示提示。Play 中的 Hierarchy 内容是临时运行副本，Stop 后回到编辑文档。

## Console

Console 实时显示 Engine、Editor 和脚本日志的日期时间、等级、来源分类与消息。Trace、Debug、Info、Warning 和 Error 复选框只筛选显示内容；Clear 清空 Editor 的内存历史，不会删除日志文件。若日志记录携带资源路径或 GameObject ID，选择 Locate 会跳到对应资源或对象。

## Editor 字体

Editor 默认使用 Engine 自带的 `Noto Sans SC` 字体，不依赖项目 `Assets` 中的字体。`ursa:Fonts/Editor/NotoSansSC-VF.ttf` 通过 ImGui 动态字体图集按需加载拉丁和简体中文字符；ImGui 内置字体合并为缺字回退。若 Engine 字体无法读取，Editor 会记录警告并继续使用内置字体；仍未覆盖的字符会显示替代字形。

字体来源、SIL Open Font License 1.1 和 SHA-256 见 [Editor 字体资源说明](../Assets/Fonts/Editor/README.md)。替换字体时，将兼容的新字体放在同一 Engine 资源路径，并保留对应许可与归属文件；松散资源和 `.pak` 均使用该 VFS 路径。

项目可通过 `Ursa.Editor` 注册自定义 Inspector、菜单、工具窗口和 Editor 工具，见 [C# 编辑器扩展](ManagedEditorExtensions.md)。

## 常用图标

Editor 的运行、场景工具、Project、Hierarchy、Inspector、Console、菜单和辅助面板使用同一套 Engine 内置图标。纯图标按钮悬停后显示操作说明；当前工具和运行状态以按下背景区分，不可用操作变暗并禁止执行。Pause 和 Step 保留位置，Step 只在暂停时有效。保存、删除、应用和确认等操作保留文字。

Project 的文件夹、场景、Prefab、材质、纹理、Shader、网格、模型、脚本和 ScriptableObject 使用类型图标，名称与搜索路径保持可读。未知文件显示通用文件图标。Hierarchy 优先显示可确认的 Prefab、Camera、Light、Renderer 类型，其余显示 GameObject；禁用对象和父级禁用的对象变暗。图标不增加组件子行，也不改变资产 GUID 或对象选择。

Console 保留 Trace、Debug、Info、Warning、Error 五个独立筛选按钮；按钮数量表示当前内存历史中该等级的总条数，不受显示筛选影响。Critical 归入 Error，保留条目仍显示 Critical 等级。历史淘汰后数量同步更新；Clear 清空内存历史及计数，不删除日志文件；带上下文的日志可用定位图标跳转。

导入器、网格编辑、Graphics Settings、External Tools 和保存确认共用图标样式；扩展菜单和自定义类型没有专用图标时使用通用标识，现有 C# API 不需要增加图标参数。图标资源通过 `ursa:Icons/Editor` 加载，支持松散和打包内容；加载失败时显示文字回退并记录一次初始化警告。资源维护与生成方法见 [图标说明](../Assets/Icons/Editor/README.md)。

图标与文字使用逻辑 UI 单位，支持高密度显示；窄面板中的按钮可换行。原有面板停靠名称不变，现有用户布局继续适用。Project 当前保留列表浏览，本次不提供缩略图网格。
