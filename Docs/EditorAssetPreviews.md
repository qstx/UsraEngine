# Inspector 资产预览

在 Project 中单击材质、纹理、FBX/OBJ 模型、Mesh 或 Prefab，Inspector 底部显示 **Preview**。单击标题可折叠；拖动标题上方带手柄的分隔条可调整高度，命中区域为 10 逻辑像素，悬停和拖动时高亮并显示上下调整光标。展开状态和高度保存在 `user:Settings/AssetPreviews/<project-key>.json`，拖动结束时保存，不写入资产。坏设置显示诊断并使用有界默认值。原有停靠布局继续保留。

高度使用逻辑像素，不随显示密度改变保存值，各项目独立保存。有限的高度限制在 120–1200；展开状态与高度全部有效才恢复。缺字段、错误类型、非有限高度或读取失败会整体回退到展开且高度 280，并显示设置诊断。写入失败保留当前会话的操作结果并提示错误。

实际显示高度还受 Inspector 可用空间限制，上方属性区至少保留 40 逻辑像素并独立滚动。缩小窗口不会覆盖保存的偏好高度，重新增大窗口时自动恢复；在受限窗口内拖动时从当前显示高度起算。

`UrsaEditor --project <项目目录> --view Inspector` 聚焦 Inspector。旧的 `--view "Material Preview"` 入口会展开 Preview 并聚焦 Inspector；不再创建独立材质预览窗口。旧布局中的 Material Preview 记录可忽略，无需重置其他面板。

`--view Inspector` 保留保存的折叠偏好；旧 Material Preview 入口会在读取偏好后主动展开。

## 选择与草稿

预览跟随 Inspector 当前资产的 GUID 和 local file ID。打开材质 A 后选中 B，会预览 B；查看 B 不会切换当前文档或触发保存确认。同一模型的两个 Mesh 子资产各有独立身份；重命名不改变 GUID。

已有同目标文档时，材质使用合法未保存参数，纹理使用未保存像素，Prefab 使用当前对象树的私有快照。属性编辑仍由原有 Inspector 和文档事务负责。预览的相机、形状、通道、Mip 和动画时间都是查看状态，不使文档变脏、不保存资产。

这五类预览属于 Editor 内置能力。C# `CustomEditor` 和 `SerializedObject` 负责组件 Inspector，本次不增加公共 C# Preview 回调。

## 材质和三维内容

材质可选择 Sphere、Cube 或 Plane。拖动画面旋转，滚轮缩放，**Reset view** 重新取景。预览使用材质本身的 Shader、参数与纹理；Shader 或活动管线失败会显示诊断。

模型预览保留完整导入层级、变换和材质。顶部可在 Whole model 与带稳定 local file ID 的 Mesh 子资产之间切换。Mesh 预览使用中性材质并显示顶点、三角形及 Submesh 数量。选择独立 `.ursamesh` 也可查看。

带骨骼权重的 Mesh 使用绑定姿态渲染，并按实际渲染几何的边界取景；不修改源网格、权重或骨骼数据。

Prefab 使用只读副本，保留禁用对象与 Renderer 状态。禁用子树不参与画面与取景；没有可绘制几何时显示空状态，缺失依赖时显示错误。初次取景按有效边界进行；退化边界使用有限最小距离。播放不会逐帧重新取景，超出初始范围时可缩放或重置。

查看模型不会将它实例化到当前 Scene；查看 Prefab 不替代打开和编辑 Prefab 的命令。

## 纹理

**Source / Imported** 切换原始解码图像和最近有效导入结果；导入设置窗口保留设置与诊断，图像展示统一在 Inspector 中。修改未应用的设置不会把旧产物当成新结果。

**RGBA / RGB / R / G / B / Alpha** 只创建显示视图，不改源像素或导入设置。RGBA 使用透明棋盘背景，RGB 强制不透明，单通道复制成灰度；Alpha 作为线性数据查看。颜色通道保留源数据的 sRGB/线性解释。

Mip 使用产物中实际保存的像素层；源图或没有额外 Mip 的纹理禁用额外层。**Fit** 保持比例适配；**100%** 对应一个纹素占一个 framebuffer 像素，支持在图像区拖动平移与裁剪。只读包没有源图时禁用 Source，仍可查看 Imported。

## 基础动画

支持现有导入器生成的骨骼动画片。新模型先显示绑定姿态；选片后从零时刻暂停。**Play / Pause** 控制独立预览时钟，**Loop** 只控制播放越界，**Speed** 接受有限正倍率。拖动时间轴会暂停；拖到精确片尾保留结束姿态，不因循环回到零。

选择 Bind pose 清除动画采样并恢复初始姿态。零时长或非法时长片不可播放。静态模型没有动画控件。成功重导入时，仍存在的片保留身份，暂停并把时间限制在新时长内；已删除的片回到静态状态。

Animator Controller、混合树、重定向、Root Motion、动画事件和 morph-target 不在此功能范围内。预览只调用显式姿态采样，不调用 Scene Update，不创建项目托管 Behaviour，也不执行 Awake、Start 或游戏 Update。

## 更新与资源寿命

三维画面使用活动渲染管线的 `CameraType::Preview`，拥有独立 Scene、相机和目标。Scene/Game 的输入、相机与渲染目标不被替换。静态内容仅在选择、草稿、依赖、管线、视角、选项或尺寸变化时更新；可见动画按帧更新。

折叠、隐藏、非活动停靠标签、最小化或没有有效画面区域时不渲染、不推进时钟；恢复时重新建立时间基准，不追赶隐藏时长。画面区域不包含工具栏，尺寸按显示密度换算，并受设备限制及像素预算约束。

版本变化先构造候选内容，成功渲染后替换。失败仅能保留同一项目会话、类型及 GUID/local file ID 的上次成功画面，并显示该画面的内容/管线版本及当前诊断；动画暂停，未变化的失败不逐帧重试，可点击 **Retry** 或等待有效内容变化。切换目标或项目立即清除旧结果，迟到结果不能覆盖当前请求。过期导入明确标记为最近成功导入。纹理导入窗口与 Inspector 共享解码内容，只有 Inspector 持有显示用 GPU 纹理。

替换或注销 GPU 目标前等待提交完成。双缓冲保留正在构建的 UI 引用的画面；旧 UI 纹理和内容在下一帧不再构建旧 draw data、且设备空闲等待成功后统一释放。等待失败返回应用的错误处理路径；退出时记录设备故障。预览在作者文档、脚本服务、UI 和设备销毁前释放，不建立跨项目全局缓存。

验证入口：`UrsaAssetPreviewStateTests` 覆盖身份、版本和时钟；`UrsaAssetPreviewPanelTests` 使用真实 ImGui 控件/鼠标事件、GPU 图像、动画 FBX 与生命周期探针；`UrsaAssetPreviewWorkspaceTests` 启动多个完整 Editor 实例，检查设置、旧入口和停靠布局恢复；`UrsaEditorSrpAcceptanceTests` 检查活动管线的 Scene/Game/Inspector Preview 路由与重载。集成证据见 [验收记录](InspectorAssetPreviewAcceptance.md)。
