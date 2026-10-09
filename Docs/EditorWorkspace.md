# UrsaEditor 工作区

UrsaEditor 使用可停靠面板。首次打开项目时，Hierarchy 位于左上，Project 与 Console 位于底部，Scene 与 Game 共享中央区域，Inspector 位于右侧。拖动面板标签可以调整停靠关系；通过 Window 菜单可以显隐面板，Reset Layout 会恢复默认布局。

窗口布局按项目保存在当前用户设置中；面板显隐状态保存在项目 `Library/Editor/Workspace/PanelVisibility.json`。清理项目 Library 会重置面板显隐设置；布局文件独立保存在用户数据目录。

## 菜单与运行控制

- **File**：保存当前文档、关闭文档或退出 Editor。关闭未保存文档时会询问保存、丢弃或取消。
- **Assets**：刷新资源、创建文件夹/场景/Prefab/材质/纹理，以及重命名选中的 Project 资源。
- **GameObject**：在当前 Scene 的选中节点下创建空对象；未选节点时创建 Scene 根对象。
- **Component**：按类别显示当前 GameObject 可添加的组件，包括项目注册的 C# 组件。
- **Window**：显隐 Project、Hierarchy、Inspector、Scene、Game 和 Console，并重置工作区。
- **Tools**、项目注册菜单和工具窗口：调用 Engine 或项目 C# 扩展。
- 顶部工具栏提供 Play、Pause/Resume、Step 和 Stop。Step 只在暂停时可用。

Edit 中的 Undo/Redo 当前不可用，Ursa 尚未提供持久化编辑操作的撤销栈。

## Project 面板

Project 左侧显示 `Assets` 文件夹树，右侧显示所选文件夹中的直接内容。展开箭头展开子目录；选择文件夹会更新内容视图。顶部路径按钮可以返回 Assets 或跳到上级目录。搜索会按资源名称和相对路径查找项目资产。

单击资源会选中它并在 Inspector 显示路径、GUID、资源类型和导入器信息。双击资源或在右键菜单选择 Open，会打开对应的 Scene、Prefab、材质、纹理或导入器视图。右键文件夹可在该目录下创建资源或子文件夹。`.meta` 文件由资产数据库管理，不单独显示。

创建资源时，留空 Asset path 会在当前文件夹使用默认名称。创建文件夹时，留空 New folder name 会创建 `New Folder`。重命名时在 Asset path 中输入目标虚拟相对路径；资产 GUID 在移动或改名后保持不变。

## Inspector 与 Hierarchy

Hierarchy 只显示 Scene 或 Prefab 中的 GameObject 层级。选择对象会让单一 Inspector 显示该对象、Transform 和组件。选中 Project 资产时 Inspector 切换为该资产信息；打开资产后由对应的资产文档 Inspector 接管。

Scene 面板提供 Transform Gizmo。Game 面板在 Play 模式显示运行画面；编辑模式下显示提示。Play 中的 Hierarchy 内容是临时运行副本，Stop 后回到编辑文档。

## Console

Console 实时显示 Engine、Editor 和脚本日志的日期时间、等级、来源分类与消息。Trace、Debug、Info、Warning 和 Error 复选框只筛选显示内容；Clear 清空 Editor 的内存历史，不会删除日志文件。若日志记录携带资源路径或 GameObject ID，选择 Locate 会跳到对应资源或对象。

项目可通过 `Ursa.Editor` 注册自定义 Inspector、菜单、工具窗口和 Editor 工具，见 [C# 编辑器扩展](ManagedEditorExtensions.md)。
