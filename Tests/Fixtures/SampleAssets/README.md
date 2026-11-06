# 项目集成测试资源

此目录保存从旧 Demo 迁出的贪吃蛇、材质、模型、纹理、脚本 API 和渲染管线展示资源，保留原有有效资产及元数据身份。它们用于引擎自动回归，与新的 Demo 功能测试菜单分开维护。

`UrsaTestSamplePrepare` 仅复制本目录并显式导入到构建目录下的隔离项目，准备前核对构建绝对路径并清除旧 Assets/Library，不再读取 Demo。`Snake/SnakeSmoke.ursascene` 为自动玩法验收配置，复用贪吃蛇静态棋盘和场景对象。

迁移基线清单位于上级的 LegacyDemoMigration.json，按“旧 Demo 后叠加旧 SampleAssets”的有效输入记录路径、GUID 和内容哈希；本 README 的说明更新不改变运行资产身份。原 Demo 中未提交的 ManagedImportRules 配置及差量保存在 LegacyDemoSettings，可单独恢复，测试准备不会覆盖用户 Demo 设置。

新增测试展示资源应放在此目录；项目生成文件、程序集与导入产物仍放在测试项目的 `Library/`。
