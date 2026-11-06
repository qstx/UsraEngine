# 旧 Demo 配置保留

`ManagedImportRules.pre-suite.json` 是重建功能测试套件前 Demo 的完整配置快照，`ManagedImportRules.pre-suite.patch` 是当时相对于 Demo Git HEAD 的未提交差量。二者用于恢复用户已有修改，不作为新 Demo 或 Engine Sample 的导入缓存。

恢复旧配置时先核对当前工作树，按需将快照复制回 Demo/ProjectSettings/ManagedImportRules.json，或对相同基线应用 patch；不要自动覆盖后续用户编辑。迁移资源清单见上级 LegacyDemoMigration.json。
