# 资源准备与项目缓存

资源准备已迁移到项目导入管线。程序构建只编译代码；Editor 打开/刷新项目，或显式执行 UrsaAssetImport 时，在项目 Library 生成资源。

完整命令、缓存布局、运行验证与旧配置迁移说明见 [项目资源导入管线](AssetImportPipeline.md)。

旧的 UrsaEngineResources 目标、Engine/cache、Build/Resources 镜像和构建期 Shader 编译流程已移除。不要删除源 Assets 或 .meta；旧生成目录可在关闭使用它们的程序后清理。重新构建程序不会重新创建这些目录。
