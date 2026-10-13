# SRP 迁移基线（2026-10-13）

本报告对应 OpenSpec `scriptable-render-pipeline` 任务 1.1。画面仍由旧
`Renderer3D` 主入口生成，包含工作区已完成的 RHI/快照基础改动；它是本工作区继续
迁移的起点，不能称为完全未修改的旧版本。Engine HEAD 为
`9b2baf25dc0e5ecc52a9a1ea05e00b367e1158e8`，Demo HEAD 为
`d0a382277d218af45f0da4a8725cedf94f351930`。已有未提交改动均保留。

## 画面证据与复跑

从工作区根目录运行，使用已有的 Windows x64 / VS 2022 构建目录：

```powershell
cmake --build Engine/Build --config Debug --target UrsaEditor UrsaScriptPlayer UrsaAssetImport UrsaFrameCaptureTests --parallel 8
Engine/Build/Debug/UrsaAssetImport.exe --project Demo --configuration Debug --profile Both
powershell -NoProfile -ExecutionPolicy Bypass -File Engine/Tools/CaptureRenderingBaseline.ps1
```

脚本默认写入新的 `Engine/Build/SrpBaseline/<时间戳>/`，拒绝覆盖已有证据。
`-OutputDir <新目录>` 可固定输出位置，`-BuildDir`、`-ProjectDir` 和 `-Configuration`
可选择构建及项目。每次运行使用独立用户数据目录，不修改已有 Editor 用户布局。
源码场景、材质和元数据不保存；运行生成的对象随会话退出清理。

本次完整证据在 `Engine/Build/SrpBaseline/20261013-Verified/`。七张 PNG 已逐张检查：

| 文件 | 基线内容 |
| --- | --- |
| SnakePlayer.png | 1024×720 客户区，棋盘、绿色蛇身/蛇头与橙色食物 |
| MaterialsPlayer.png | 1024×720，带纹理、Ceramic、Gold 的三个立方体和地板 |
| SnakeScene.png | 1280×720 Editor，Run 场景的编辑相机视角 |
| SnakeGame.png | 同尺寸 Editor，Run 场景的游戏相机视角 |
| MaterialsScene.png | 同尺寸 Editor，编辑相机下的材质场景 |
| MaterialsGame.png | 同尺寸 Editor，游戏相机下的材质场景 |
| GoldPreview.png | 256×256 材质预览目标及 Gold 属性 Inspector |

截图取第 120 个有效帧。Editor 的启动导入进度不计入捕获；`--play` 等待 Run
激活后才计算捕获帧数和帧限制。Snake 和 MaterialShowcase 都在脚本生命周期中
创建几何体，因此 Stop 下源场景为空；Run 图片才用于有几何体的画面对比。
Scene 和 Game 的相机距离不同是当前行为。蛇的位置随实际时间推进，不要求跨运行
逐像素相同。材质预览保持当前 PBR 输出，没有调整光照或颜色来修改基线。

`Manifest.json` 保存实际启动参数、每张 PNG 的 SHA-256、二进制 SHA-256、构建
开关、版本及 Engine 状态，以及 33 个 Engine/Demo 源资产的 GUID、源文件和
元数据 SHA-256。清单和日志随图片留在忽略的 Build 目录。
只补录已有完整图片的清单时可用 `-ManifestOnly`，前提是二进制、源码和资产仍
对应这些图片；`-CaptureNames MaterialsPlayer` 可单独验证一个入口。

此前 `EditorRendered.png` 捕获了导入进度，`20261013-Resume2/` 的 Scene/Game
仍处于 Stop；它们保留作排查记录，**不属于有效画面基线**。

## 关键资源身份

完整清单在上述 Manifest，以下身份必须在后续迁移保留：

| 资源 | GUID |
| --- | --- |
| Snake/Snake.ursascene | 75727361-0000-0000-0300-000000000002 |
| MaterialShowcase.ursascene | d3000000-0000-4000-8000-000000000100 |
| Materials/Ceramic.ursamat | 8b8e4e91-58a2-4a26-a2d5-0a64365d3401 |
| Materials/Gold.ursamat | a3f83e80-98a7-4d9e-a606-1c4a572b376b |
| ScriptSample/Checker.ursatex | 55555555-5555-4555-8555-555555555555 |
| ScriptSample/Checker.ursamat | 66666666-6666-4666-8666-666666666666 |
| Scripts/Snake/SnakeGame.cs | 1c59a5b2-89f6-406c-9d8d-d99229aa3956 |
| Scripts/MaterialShowcaseBehaviour.cs | b1b44da0-89d5-4cc9-bc51-7f93140228c7 |
| 内置 Unlit / PBR Shader | 10000000-0000-4000-8000-000000000001 / 10000000-0000-4000-8000-000000000002 |

## 固定性能采样口径

本次图片来自 Debug、Vulkan validation 开启、Tracy/Memory/Diagnostics 开启、
调用栈深度 0 的现有构建；没有连接 Tracy 采样器。图片读回会增加等待，不作为
性能样本。任务 10.4 的正式对比使用独立构建目录，避免改变现有构建开关：

```powershell
cmake -S Engine -B Engine/Build/SrpPerformance -G "Visual Studio 17 2022" -A x64 -DURSA_ENABLE_DOTNET=ON -DURSA_BUILD_EDITOR=ON -DURSA_ENABLE_TRACY=ON -DURSA_ENABLE_TRACY_MEMORY=OFF -DURSA_ENABLE_TRACY_DIAGNOSTICS=OFF -DURSA_TRACY_MEMORY_CALLSTACK_DEPTH=0
cmake --build Engine/Build/SrpPerformance --config Release --target UrsaScriptPlayer UrsaEditor UrsaAssetImport --parallel 8
Engine/Build/SrpPerformance/Release/UrsaAssetImport.exe --project Demo --configuration Release --profile Both
Engine/Build/SrpPerformance/Release/UrsaScriptPlayer.exe --project Demo --scene Snake/Snake.ursascene --hidden --frames 1320
Engine/Build/SrpPerformance/Release/UrsaScriptPlayer.exe --project Demo --scene MaterialShowcase.ursascene --hidden --frames 1320
```

两场景分别启动独立进程，各重复三次，禁止同时运行两轮或并行构建。
Player 固定 1024×720，Editor 固定 1280×720、默认布局和 Free Aspect；材质预览
256×256。交换链 VSync 保持开启，Vulkan 使用 FIFO，不用窗口缩放或 DPI 改变目标。
使用同一 GPU/驱动、电源模式和显示环境；本机 CPU 为 i9-13900H，GPU 枚举包含
RTX 4060 Laptop，NVIDIA 驱动版本 32.0.15.9186，另有 Oray 虚拟显示设备。
正式采样须记录实际使用的物理设备及显示/电源状态。

先启动 Tracy 0.14.1 capture 接收端，再启动目标。每份 capture 丢弃最初 120 个
完整 Engine 帧，随后取连续 900 个完整帧；1320 帧运行上限留出连接和边界余量。
不足 900 个有效样本、发生窗口恢复/资源重导入或包含截图读回的轮次判为无效。
预热和长度按完整帧计算，不能用固定秒数冒充固定样本量。保存原始 `.tracy` 与
区间索引、每帧数据、CPU 区域、GPU 区域及中位数/P95到
`Engine/Build/SrpBaseline/Performance/<轮次>/`，新管线使用相同口径。
详细内存/Diagnostics 的验证另采，不能混入基础时间对比。

当前 GPU 标记主要覆盖 DrawIndexed；帧槽或栅栏等待不等于 GPU 绘制耗时。
设计文档中的旧 36 秒/33.46 ms 数据是历史热点线索，因配置和采样边界不同，
不作为这组固定条件的数值基线。任务 1.1 固定了复跑条件和画面/身份记录；
正式 CPU/GPU 数值对比及内存诊断仍由任务 10.4 验收。

## 验证记录

Debug Editor/Player 构建通过。`UrsaFrameCaptureTests` 通过独立 PNG 解码器验证
RGBA/BGRA、尺寸、像素、透明度和未完成捕获诊断。七个实际入口均正常退出，
日志未出现 Vulkan validation 错误。Editor 客户区截图支持 `--asset`、`--view`
及 `--play`，不代替任务 10.2 的真实交互验收。
