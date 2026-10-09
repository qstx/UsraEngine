# C# Script Player 发布

## 构建与编译

从工作区根目录配置 Engine，并指定 C# 项目目录：

```powershell
cmake -S Engine -B Engine/Build -DURSA_ENABLE_DOTNET=ON -DURSA_PROJECT_DIR=Demo
cmake --build Engine/Build --parallel --config Debug --target UrsaEditor UrsaScriptPlayer UrsaScriptCompile UrsaPak
Engine/Build/Debug/UrsaScriptCompile.exe Demo Debug
```

脚本程序集和 Editor 扩展清单位于项目 `Library/ScriptBuild/` 下。脚本发生变化后必须重新成功编译；`UrsaPak` 会拒绝过期或失败的脚本输入。Release 发布需对构建、脚本编译和打包都使用 Release 配置。

## 创建单包

```powershell
Engine/Build/Debug/UrsaPak.exe `
  --engine Engine/Assets `
  --engine-generated Engine/Build/Resources/Debug `
  --project Demo/Assets --library Demo/Library `
  --configuration Debug `
  --output Engine/Build/Package/Debug/Content.pak
```

包内包含 Engine 与项目资源、导入后的模型/纹理，以及当前 Game 程序集。包内 `ursa:` 和 `prj:` 只读；日志、设置和存档通过 `user:` 挂载写入。

## 运行

从工作区根目录使用 loose project 或 pak：

```powershell
Engine/Build/Debug/UrsaScriptPlayer.exe --project Demo --scene Snake/Snake.ursascene
Engine/Build/Debug/UrsaScriptPlayer.exe --project Demo --scene MaterialShowcase.ursascene
Engine/Build/Debug/UrsaScriptPlayer.exe --pak Engine/Build/Package/Debug/Content.pak --scene Snake/Snake.ursascene
```

Player 也接受 `--content <目录>` 读取已部署的 loose `Content/Engine` 与 `Content/Project` 结构。`--scene` 路径相对于项目 `Assets/`，pak 模式下相对于 `prj:`。

## 内容与路径边界

项目源码、`.cs`、`.meta` 和 `Library/` 不会原样放进发行包。`prj:__UrsaManaged/` 保存游戏程序集和清单，`ursa:Managed/` 保存稳定宿主运行文件；`__UrsaManaged` 与 `__UrsaCooked` 是保留目录。运行时程序集通过 VFS 加载并验证内容指纹。

发行机器需要 Windows x64、兼容的 .NET 10 Runtime 和 Vulkan 驱动，不需要 SDK 或项目源码。Player 启动时从 `user:` 缓存宿主文件，不会自动安装运行时。
