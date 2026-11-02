# Document Info 模块

本示例通过公开 `Editor.h` 和 `ImGuiDrawing.h` 登记命令、菜单和工作区窗口，
读取已有 `document.info` 的复制数据。它不访问 CKContext、Document 或编辑器私有头。
打开、绘制和关闭窗口不修改文档、选区或撤销历史。

在当前源码树中构建匹配宿主的模块：

```sh
cmake --preset macos-arm64-release -DVIRTOOLSDEV_BUILD_EXAMPLES=ON
cmake --build --preset macos-arm64-release --target NeMoDevDocumentInfo
```

其他平台使用相应 preset。普通模块采用 `nemodev_add_module`；自绘模块指定匹配的 `HOST`，
并使用该宿主的 ImGui 头与编译设置。示例默认不启用、不安装。
打开示例后，Editors 菜单中的 Document Info 使用宿主的窗口创建、绘制、关闭和查询流程。

执行常规安装会将显式启用的示例放入 `Bin/modules`；macOS 分发包使用
`NeMoDev.app/Contents/PlugIns/EditorModules`。该目录随程序搬移，用户目录和显式模块路径也可使用。
不需要修改 SDK，也不需要在宿主增加专用绘制分支。

`--editor-module` 和 `--editor-module-dir` 的输入会写入当前 JSON 配置，下次启动无需重复传入。
可用 `--editor-profile` 选择独立配置；Modules (Next Start) 菜单统一控制内置与外部模块。
配置以实际模块标识 `example.document-info` 保存开关，禁用在下次启动生效。
命令、查询、编辑器和视图的提供者选择也保存在该文件，沿用相同的登记与校验流程。
