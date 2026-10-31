这两份配置由提交 `b8f8616` 的原有 Preferences 实现写出，在 General Preferences 迁移前冻结。

- `default.ini` 使用原有默认值。
- `custom.ini` 包含修改过的 Schematic 值、颜色、3D 设置、文件选项、最近文件、路径分类及资源库；路径含 UTF-8 文本。

`PreferencesCommandTests` 比较默认配置的原始字节，并检查自定义配置读取后再次写出的字节完全相同。
测试中的 `future.setting` 仅检查修改时保留其他内存数据；旧配置格式仍只写原来已知的设置，不把未知字段持久化算作本轮完成。
