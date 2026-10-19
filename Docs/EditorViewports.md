# 编辑器 Scene/Game 视口

Scene 的移动、旋转和缩放使用图标按钮，当前工具有按下背景；悬停可查看操作名。Game 的 Fit 和分辨率预设编辑等入口使用统一图标，分辨率名称、Scale 数值及保存/取消文字继续显示。窄面板按钮换行后，画面和 Gizmo 的实际区域仍按工具栏下方剩余空间计算；工具栏和弹出菜单不接收游戏输入。

Scene 与 Game 分别持有颜色目标、预览渲染器和 ImGui 纹理注册。Scene 使用编辑器相机；Game 使用场景活动相机。Edit 模式从 SceneDraft 收集画面，Run/Pause 从 PlaySession 的运行场景收集画面。

每帧先创建 ImGui 布局并测量当前视口区域，再根据当前 Game 图像的命中状态路由输入并更新运行场景，最后录制可见视口并提交 ImGui。这样游戏输入、相机比例、图像尺寸和画面坐标都使用同一帧的区域信息。

视口目标仅在区域宽高为正且目标尺寸合法时创建。尺寸变化先创建并注册候选纹理；成功后等待设备空闲，再注销和释放旧纹理。创建或注册失败时保留旧目标供最后一帧显示。隐藏、折叠、最小化和零大小区域暂停渲染；窗口恢复后按新区域重建目标。

Game 尺寸区分可用面板区域与目标像素。Free Aspect 跟随区域，固定比例在区域内适配，固定分辨率保持用户给定像素。Scale、Fit、裁剪和 pan 只改变 ImGui 显示矩形，不改变目标像素或相机视场角。用户自定义预设和当前项目显示参数写入 `user:Settings/GameViewPresets.json` 与 `user:Settings/GameViewProjects/<project-key>.json`。

ImGuizmo 必须在 `ImGui::NewFrame` 后执行 `ImGuizmo::BeginFrame`。Gizmo 绘制、命中和输入都应使用 Scene 当前实际显示矩形。

依赖跟踪：Engine 当前固定 ImGuizmo 子模块提交 `b796ac3b861afc6e91ca74e4611effd9c9527367`。截至 2026-10-11，官方 master 的 `SetRect` 已使用 `mY + mHeight` 计算纵向命中边界；当前固定提交仍使用 `mY + mXMax`。本项目作者已在 Demo 中手动验收，反馈现有行为没有实际影响，因此当前不带本地修正，等待官方发布新版后再升级并复核。[官方 master 源码](https://github.com/CedricGuillemet/ImGuizmo/blob/master/src/ImGuizmo.cpp#L955-L964)
