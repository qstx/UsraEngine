# 项目 Scriptable Render Pipeline 示例

`ShowcasePipeline.ursaasset` 是可编辑的项目 C# 管线资产，引用 `Composite.ursamat` 和项目 Shader。这些示例由引擎测试夹具叠加到 `Build/TestProjects/Sample/<配置>`，测试项目默认使用框架 Forward；打开 `Window > Project Graphics Settings`，选择此资产并 Apply，即可让 Scene、Game、Material Preview 和 Player 使用同一离屏合成流程。选择 None 并 Apply 恢复默认 Forward。

每个相机先 `SetupCameraProperties`、剔除一次，再由同一 `CullingResults` 创建不透明和透明 RendererList。两个阶段依次绘制到独立的带深度 RenderTexture，最后通过 `DrawFullscreen` 采样场景颜色。绿色边框和可编辑的 `tint` 是合成阶段的可见证据。相机视口限制最终合成区域；RenderTexture 随尺寸变化替换，管线释放时统一回收。

在 Project 面板的 Create 菜单中选择 `Rendering/Demo Showcase Pipeline` 可以创建新实例。在 Inspector 中选择 `Composite.ursamat`、修改 `tint` 并 Save，再通过 Graphics 设置应用。尚未指定合成材质的实例会在工厂阶段给出诊断，保留上一有效选择。

`MaterialShowcase` 的方块在 Play 后由 Behaviour 创建；Stop 状态只渲染源场景。材质单击后可打开独立 Material Preview。Shader 的 Unlit 与 PBR 阶段分别使用 `SRPDefaultUnlit`、`UniversalForward`，旧 GUID 和材质引用保持有效。

`ComputeShowcasePipeline.ursaasset` 使用同一管线实现。它用 Ceramic 替换不透明 RendererList 的材质，并调用 `Pattern.compute` 的 `GeneratePattern` 内核生成 64×64 纹理，再在全屏阶段按 `patternStrength` 混入图案。内核线程组从导入反射读取，C# 按该尺寸计算 Dispatch 数量。计算纹理、替换材质、合成 Shader 及管线资产都随松散内容和 Pak 发布。

多相机示例：在编辑器中选中 `Showcase Camera`，通过 Add Component 添加 `MultiCameraShowcase`，进入 Play。脚本将原相机设为左半视口，并创建右半视口相机，使用不同位置和深度顺序。两个相机独立剔除、维护离屏目标并在自己的区域合成。Stop 后临时相机和运行时设置随运行图回收；源场景只有显式保存的脚本组件变化。
