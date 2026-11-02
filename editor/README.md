# 编辑器模块

`Editor.h` 和 `Identity.h` 是 C/C++ 都可使用的公开头，只依赖 `stdint.h`。
`ImGuiDrawing.h` 是需要自绘控件或视图的模块使用的绘制适配，单独依赖宿主使用的 ImGui。

模块导出 `NeMoDevModule(const EditorHost *, EditorModuleDesc *)`。内置功能也提交同一种
`EditorModuleDesc`，没有另一条注册路径。命令、查询、编辑器、视图的用途标识与具体实现标识分开；
同一用途有多个实现时，必须在配置中明确选择，登记顺序不会覆盖已有实现。当前 `EDITOR_VERSION` 为 22，模块与宿主使用相同版本重新编译。
模块描述、参数和文字在注册时复制。注册失败不会留下部分项目。依赖缺失、依赖环、参数不一致、
重复标识、绘制版本或布局不匹配会使启用失败。

生产启动会扫描程序目录的 `modules`，macOS 应用包使用 `Contents/PlugIns/EditorModules`；
用户目录为既有偏好目录下的 `modules`，与原生运行时插件分开。
`--editor-module-dir PATH` 可重复指定目录，`--editor-module FILE` 继续指定单个文件。
默认目录不存在时忽略；显式目录或文件缺失、无法读取、坏库、入口或依赖错误会使本次启动失败，并报告来源。
只扫描目录第一层当前平台的 `.dll`、`.dylib` 或 `.so`，按目录参数和文件名字节顺序收集；
私有依赖可放在子目录并设置模块自己的库查找路径，不会被当作编辑器模块递归扫描。
不搜索当前工作目录、系统库目录或原生插件目录。同一文件的符号链接、硬链接和重复参数只加载一次；
不同文件声明相同模块标识仍报错，不按安装位置覆盖。

内置登记函数和外部入口交给同一 `EditorApplication::Assemble` 批次，最终均通过相同的
`EditorModuleDesc` 复制与校验。全部来源收集后才启用；失败停止并移除本批已接受模块，保留此前登记，
允许清理后重试。有效依赖按相反顺序停止，所有停止回调结束后才卸载动态库；
无有效依赖排序时按收集的相反顺序清理。模块入口失败或抛出 C++ 异常时也调用其已提供的 `stop`。
入口返回的描述数组必须保留到宿主复制完成，使用静态存储或模块拥有的数据，不能指向已经退出的栈。
注册和加载仅限编辑器线程与启动阶段，不热加载或热卸载代码。初始化只能准备描述与模块自身资源，不能执行文档命令。

`cmake/EditorModules.cmake` 提供 `nemodev_add_module(Name SOURCES ...)`；使用 ImGui 时增加
`HOST VirtoolsDev`，导入匹配宿主的绘制实现，不链接另一份 ImGui。只有显式调用
`nemodev_install_module(Name)` 的模块才安装到 `Bin/modules`，测试夹具不随产品交付。
公开接口示例见 `examples/DocumentInfo`。默认构建不安装示例；启用
`VIRTOOLSDEV_BUILD_EXAMPLES=ON` 后构建示例目标并安装，macOS 打包会将模块移至上述 PlugIns 位置。
隔离的 UI 回归不扫描安装和用户目录，仍使用同样的显式目录发现与装配流程。

命令和查询都接收带长度的 UTF-8 文字、对象引用、无符号整数、有限实数、各类数组和递归记录。查询声明返回字段；宿主通过 `EditorSink`
立即复制结果。命令通过同一个 sink 返回 `message` 文字。文字可以来自回调局部变量，sink 返回后即可释放。
对象数组和整数数组同样在提交时复制；命令真正执行前重新检查数组内每一个对象引用。每次请求与结果中的文字总量上限为 1 MiB，数组总量上限为 64 MiB，批量选区不受文字长度限制。
模块负责自己的分配和销毁；视图的私有数据由对应的 `create` 和 `destroy` 配对处理。

工作区保存窗口实例、角色、开关、焦点、停靠和位置。视图用 `saveState`、`restoreState` 和
`stateVersion` 处理自身状态；`settings` / `settingCount` 列明其中可以跨启动保存的字段。
这些字段复用 `EditorParameter`，只允许独立于文档的值，不得含嵌套对象引用、对象编号或会话编号。
同一版本号同时约束运行中替换和启动恢复；改变字段含义时必须递增，版本不匹配会保留原配置并报告失败。
没有声明字段的窗口只保存位置和开关，不会把完整的临时状态写入文件。
恢复时先创建新实例，读取其初始状态，再覆盖已保存的字段，最后调用同一个 `restoreState`。
单实例在同一次启动中关闭再打开仍恢复完整临时状态；`documentBound` 窗口和 overlay 不写入启动状态。
外部模块和内置模块使用同一流程。提供者缺失时保留占位窗口和原始字段，重新启用后可恢复。

`workspace.save` 通过现有配置发布流程保存工作区；`workspace.reopen-instance(instance)` 重开保留的窗口。
这里的持久窗口编号只标识窗口，不是运行中的 `EditorView` 作用域，也不能作为对象引用使用。
`workspace.view` 和 `workspace.instances` 的 `available` 表示当前提供者是否可用。
工作区恢复失败时禁止覆盖原保存内容，显式执行 `workspace.discard-saved-state` 后才允许保存替代工作区。

`ObjectRef` 包含文档、原生对象槽位及对象生命周期。它不持有对象，也不写入 NMO/CMO/NMS。
执行命令前宿主重新查找目标，因此原生 ID 或内存地址被复用后，旧输入不能修改新对象。
对象查询和实际修改位于 `features/objects/ObjectCommands.cpp`，视图只接收复制后的值。

名字属性将 `object.details` 的 `name` 字段、`object.rename` 的 `name` 参数及
`text.single-line` 编辑器连在一起。Level Manager、Hierarchy Manager、Setup、属性视图、
快捷键和菜单使用同一个属性草稿。草稿保留开始时的对象引用；选择另一个对象不会把它改为新的目标。
取消、关闭所属视图、关闭文档和目标失效都会结束草稿。确认后在帧末提交；原生任务占用文档时拒绝提交，
草稿继续保留。一次外层命令及其嵌套修改共用现有 `Edit`，只产生一个撤销步骤，失败会回滚全部修改。

命令声明文档、工作区或应用作用域，并区分可撤销编辑、不可撤销文档修改、会话变化、历史、文件和运行控制。
只有可撤销编辑自动加入 `Edit`；不可撤销修改、历史和文件命令复用已有处理方式，不能在未结束的文档编辑中执行。
状态查询与执行使用同一准入检查，实际执行前再次检查。可选参数省略时使用对应类型的空值。
`EditorParameter::nullable` 只用于对象参数：明确传入空引用可以清空值，但必需参数省略时仍拒绝。
结构值编辑器声明自己的选项查询字段；标量编辑器保持成对的名称和值列表。

Setup 属性通过 `setup.*` 登记，同一属性的 `*-value` 查询和 `*-set` 命令负责读取与修改。
索引、批量目标和文档版本随草稿固定。窗口所属对象与实际编辑目标分别记录，因此曲线点、相机目标和 Level 的场景环境也能完成连续拖动。

可撤销命令可选填 `EditorCommandDesc.preview`。`EDITOR_PREVIEW_UPDATE` 保存第一次修改前的数据并显示预览；
`EDITOR_PREVIEW_CANCEL` 恢复原值，释放回调状态并将指针置空，恢复报错时也必须完成释放。
宿主在取消、关闭视图、目标失效、文档切换和停止时清理预览；确认前恢复，再由已选中的修改命令提交一次撤销。
预览不修改 Dirty、文档版本或历史。恢复失败会清空已不可靠的历史并标记文档。
回调在编辑器线程执行，期间可读取查询，不能重入修改命令。运行时任务的准入继续拒绝未结束的编辑。
提供预览的回调数据必须保留到宿主停止；模块停止前，宿主先清理草稿。

连续命令通过 `beginCommand` 固定参数，随后使用相同的 `editValue`、`finishEdit`。
它须声明非空对象 `target`、可编辑的 `value`、`preview` 和 `commitPreview`，并使用可撤销编辑类别。
`beginCommand` 只建立草稿；首次 `editValue` 建立原生预览。`commitPreview` 将已经预览的结果加入
宿主现有的外层 `Edit`，不恢复后重新执行手势。成功后 `EDITOR_PREVIEW_RELEASE` 只释放状态并清空指针，
宿主再提交外层编辑；回调失败、抛异常或释放失败均取消该编辑。取消和释放均须完成自己的清理。
没有 `commitPreview` 的属性保持原有的“恢复后执行命令”方式。
活动或等待提交的连续命令阻止配置切换。关闭作用域、切换文档和停止时，同一宿主清理活动及排队状态。
`previewCommand` 仅供回调委托给指定实现，支持 UPDATE、CANCEL、RELEASE 和 COMMIT；
回调之外、错误线程或超出递归上限的调用会被拒绝。内置实现与外部实现使用相同入口。

Setup 的场景环境保留专用恢复方式；图像保存格式及其选项只恢复自身数据，保留旧文件中的格式字节。
没有图像槽位的位图属性使用值记录，避免整对象加载改变无关的 Current Slot。其他对象沿用现有状态记录及完整的引用生命期。
材质状态单独保存效果参数的数据；参数值撤销记录以材质、效果和成员路径定位，效果重建参数后仍能连续撤销。
格式选项共用读写实现，校验读写器的参数类型、成员和数据长度，失败保留原格式。

Schematic 的注释和脚本颜色通过 `schematic.comment`、`schematic.script-color` 属性编辑，
移除颜色使用 `schematic.reset-color` 命令。注释所在图使用完整对象引用，槽位只在开始时的 Script 版本有效；
批量脚本颜色同时固定全部目标及版本。无变化的确认保留 Redo，任一目标失效时不写入其他目标。
优先级和连线延迟复用 `setup.behavior.priority`、`setup.behavior-link.delay`，多选确认只形成一步撤销，
批次中有目标不能修改时回滚整批。对话框只保留草稿编号，确认后保留作用域至帧末执行；关闭视图取消其草稿。
共享控件保留多行注释和嵌入式 RGB 颜色选择器，查询、编辑器和修改命令仍可分别替换。

Schematic 连续工具统一在 Escape 时取消，包括框选建图；隐藏或失去焦点时结束未完成的拖动。
Layout 的相机、三维变换和二维句柄共用取消入口，在 Escape、右键、隐藏、失焦和停止前恢复原值。
正交视图的轨道拖动取消后恢复原来的相机选择；重复 Begin 不丢弃已有拖动。
系统文件对话框导致失焦时只取消连续工具，参数工作副本和属性对话框继续保留。
原有属性取消流程保留；连续图编辑与 Layout 手势使用版本 16 的命令入口。

DataArray 的行列操作通过 `array.*` 命令执行，表格只读取 `array.table`、单元格和列的值查询。
`beginEdit` 和 `drawProperty` 接收 `EditorRequest*`，其中除 `target` 外可传入位置参数。
`array.cells` 使用 `rows` 与 `column`；`array.column` 与 `array.new-column` 使用 `column`。
查询将这些位置和文档版本返回给草稿；确认时使用同名字段调用修改命令。修改期间排序、增删行列、
撤销或文档切换都会使旧版本失效。表格行选择、焦点和弹窗状态由窗口持有。
单元格继续使用原生文本表示和已登记的 `value.string`；列描述是名字、类型和参数类型组成的记录，
由 `value.array-column` 绘制。批量单元格全部成功才记录一步撤销，任何一个失败都会恢复整批原值。
列对话框中的类型选择只更新草稿，OK 或输入框回车才提交；取消、关闭和滚出正在编辑的行会丢弃草稿。
行列撤销记录保留原有参数的完整对象引用，恢复时不重新创建单元格参数；记录淘汰后由现有历史回收。
内置和外部实现使用同一登记方式；已有草稿期间仍沿用现有的配置切换检查。

时间、按键、脚本、动画、消息、属性类型及同步对象都复用参数草稿和修改命令。
`parameter.selection-choices` 接收目标和可选的 `filters` 记录，记录内可指定 `class`、`owner` 和 `category`。
空记录沿用当前值的所有者和类别；显式空所有者表示全部脚本，显式空类别表示全部属性。
消息记录保存 `id` 和 `name`，`id=-1` 配合新名字只在确认时登记消息类型；登记后的原生编号在撤销时保留。
同步对象记录保存 `object`、尚未创建的 `name` 和可选的 `users`；修改人数时另带 `set-users=1`。
显示人数不会修改被引用对象。新建、人数修改和参数引用修改在同一次确认中完成，并共同撤销。

每个已接受命令返回整数 `operation`，包括尚待帧末执行的请求。通过 `application.operation`
查询 `status`、`path`、`written`、`message` 和结果字段数 `count`；最近 256 个完成结果保留在应用中。
`finishEdit` 的可选 `operation` 输出返回排队编号，取消或失败将其清零；
`application.operation` 的 `document-revision` 是外层编辑结束后的文档版本。
`application.operation-output` 按操作编号和索引读取字段的键、类型及对应值，包含批量创建的对象引用；
结果中的对象引用仍不持有对象。等待中的操作拒绝读取结果字段，已淘汰的编号返回失效。
异步提供者返回 `EDITOR_QUEUED`，结束后在编辑器线程调用 `completeOperation`；文档编辑由宿主提交后结束。
关闭视图会取消尚未执行的请求。已经开始的文件任务仍完成清理并登记实际写出的文件，不能依赖原视图继续存在。

所有宿主调用都在编辑器线程执行。绘制、查询和变更通知中的修改请求在帧末执行，提交时固定目标和所选实现。
文档查询结果按版本缓存；声明为每次读取的工作区查询直接读取工作区状态。订阅绑定到视图，关闭视图后
不再回调。停止模块前先销毁视图并清除等待执行的命令；模块按依赖的相反顺序停止，随后卸载动态库。
模块回调不得抛出异常；宿主也会捕获 C++ 异常，文档编辑失败时回滚修改。

文件与撤销入口也通过普通命令、菜单和键位贡献登记。菜单和键位可以引用无需必填参数的命令，
或引用已有属性；同一动作可有多个默认键位。配置覆盖某个动作时替换它的全部默认键位，空键位表示禁用。
文件流程由 `DocumentCommands` 持有，窗口只呈现输入并回复。`document.input` 提供当前输入编号、
种类和显示数据，`document.file-filter` 按编号和索引提供过滤器，`document.respond` 提交选择或取消。
自定义文件对话视图使用这些相同入口，无需文档或 SDK 访问。

对象创建与删除也使用普通注册命令。`object.create-*`、`script.create`、`script.delete`、
`object.remove-from-scene`、`object.remove-from-group` 复用现有领域修改和撤销记录。
`object.destroy` 保留直接销毁并清空历史的原有语义，使用不可撤销文档修改类别。
`object.delete-options` 返回当前依赖选项；确认时随删除请求传入完整草稿，原生依赖数据由对象命令拥有。自定义数组须覆盖当前 SDK 的全部类别，未知 flags 和超出 32 位的选项均拒绝。

`selection.replace/invert/clear/lock/restore` 修改同一份 `ObjectSelection`；`selection.objects`
返回只读的对象引用、锁定与版本，无需读取 SDK。锁定中的普通替换、清除和反选会被拒绝；
恢复命令用于取消选择交互并还原原来的对象引用和锁定。选择不产生创作撤销步骤。

视图可声明普通窗口或覆盖式绘制，以及单实例策略。默认文件对话视图是可替换的单实例覆盖视图；
配置切换先创建新视图，成功后销毁旧实例，创建失败保留旧实例和配置。

绘制模块必须使用宿主的同一份 ImGui 实现和编译配置。启用时校验 ImGui 版本与公开结构的布局尺寸，
模块不得另行静态链接第二份 ImGui。测试模块分别针对普通宿主和开启测试引擎的宿主独立编译。

名字、基本值、组合和专用值、DataArray 表格及 Setup 属性使用共享草稿和修改入口。
Schematic、Setup 和 Layout 的完整窗口已登记到工作区，查询使用只读数据，修改走统一命令。
生产模块发现、提供者配置和工作区状态保存恢复已接入；各平台发布包仍需分别验证。
GPU 视口继续使用现有纹理导入和 backbuffer 路径，模块接口不增加图像传输或逐帧资源操作。

Schematic 的连续编辑由 `schematic.geometry` 准备参数，`schematic.edit-geometry` 执行。
`mode` 为 0 移动、1 Script 高度、2 图尺寸、3 注释尺寸；`value` 包含二维 `delta` 或 `size`，
提交时可另带 `route` 和二维 `drop`。目标、元素位置、Script 与拓扑版本在开始时固定。
ScriptSession 只为绘制保存显示副本，查询与保存仍读取原值；预览和取消均不改 Dirty、历史及内容版本。
元素几何提交后，视图仅在 `document-revision` 仍相同时通过 `schematic.positions` 更新自己的选区位置；
发生后续编辑、Undo 或对象重建后不得重用旧索引。

| 用途 | 查询与命令 | 参数要点 |
| --- | --- | --- |
| 分组、解组、框选建图 | `schematic.group`、`schematic.ungroup`、`schematic.draw-graph` | 完整对象引用或二维 `rect`，批次预先校验 |
| 连接、移动连接、类型转换与插入运算 | `schematic.connection`、`schematic.connect` | 保留 `from`、`to` 与可选 `link` 位置；运算由复制的 GUID 列表选择 |
| 路由、控制点、导出引脚 | `schematic.route-links`、`schematic.insert-control-point`、`schematic.export-pins` | 位置记录、控制点序号及二维坐标；失效位置拒绝修改 |
| 剪贴板与拖动复制 | `schematic.clipboard`、`schematic.copy-selection`、`schematic.copy/cut/paste/paste-shortcuts/duplicate/copy-elements` | 共享同一原生剪贴板；粘贴固定剪贴板版本，跨 Script 拖动另带目标版本 |
| 3D 输入与连续手势 | `layout.input`、`layout.edit-gesture` | `kind` 为 0 相机、1 三维变换、2 二维变换、3 Fly；完整对象引用、起点、选项和鼠标状态 |
| 曲线工具与离散输入 | `layout.curve-tool`、`layout.set-curve-tool`、`layout.curve-click`、`layout.wheel-zoom`、`layout.zoom-on`、`layout.player-click` | 工具状态属于 Layout 原生实现；视图只保存输入、草稿与完成编号 |

3D 变换继续复用已有相机、变换、二维和对象状态记录算法。连续命令保存开始时的相机、曲线、
参照对象和全部被修改对象的完整引用；停止或取消恢复原值，确认只形成一步 Undo。
点击未选中的对象时先完成选择命令，再以该对象开始变换。视口资源、GPU 快通道、fallback
和显式截图读回维持现有流程。阶段五仍在迁移，正式性能验收暂缓且原门槛不变。


工具栏和视图工具使用 `EditorModuleDesc.tools`、`toolbarItems`、`toolbars` 登记。工具的 ID 同时是普通会话命令的 ID；菜单、工具栏和按键都执行这个动作。工具实现由已有 `command` 提供者配置选择，工具栏控件由 `toolbar` 提供者配置选择。各工具必须声明所属视图类型；相同工具的不同提供者必须保持所属视图和命令参数一致。

`EditorViewDesc.defaultTool` 指定默认工具。每个工作区实例分别保存当前工具，缺少外部模块时保留其标识并使用默认工具。视图内容区调用 `toolInput`，输入坐标相对此内容区，按钮掩码依次为左、右、中键；修饰键依次为 Control、Alt、Shift、Super。`ImGuiDrawing.h` 的 `EditorImGuiToolInput` 提供相同转换。只有内容区内的按下和滚轮会发给工具；捕获后继续接收移动和松开，失焦、隐藏、Escape、关闭和停止取消未完成预览。

工具接收 ENTER、FRAME、CANCEL、LEAVE。FRAME 返回 `EDITOR_TOOL_CAPTURED` 表示正在交互，返回 `EDITOR_TOOL_DEFAULT` 回到视图默认工具。文档编辑继续使用 `beginCommand` 或 `beginEdit`、`editValue`、`finishEdit`；这些草稿归工具来源视图。一次确认只记一步撤销；取消、异常或未清理就退出工具时，宿主恢复工具尚未确认的草稿，不取消同视图的普通 Setup 草稿。正在捕获时禁止切换配置。切换工具先发 LEAVE，工具可以接受当前交互；主动取消发 CANCEL，恢复原值。

`EditorBindingDesc.view` 指定输入归属，空值表示应用级动作；`phase` 区分按下、按住、松开。配置中的 chord 可包含 Control/Alt/Shift/Super 修饰键、字母、数字、F1–F24、命名导航键、Equal、Minus、KeypadEnter、KeypadAdd、KeypadSubtract 和 MouseLeft/MouseRight/MouseMiddle。格式沿用 `Ctrl+K`、`Shift+F3`。按下要求修饰键完全匹配；按住和松开允许同时按住其他修饰键，保留原有空格平移行为。同一 chord 可以在不同视图使用，同一视图内冲突会拒绝配置。可用的视图动作先处理输入，不能执行的动作不吞键，再由应用级动作处理。文字输入、模态和弹出菜单不触发这些动作。`inputBinding` 可检查已配置动作的按住/松开状态，Schematic 的平移与松开显示切换由同一绑定驱动。

工具栏位置由视图或应用绘制区声明，项目引用普通命令，并可携带复制后的固定参数。项目默认绘制为按钮；原有图标、下拉框和工具菜单使用 `EditorToolbarDraw`，仍经同一登记与配置。绘制区域和项目位置使用设备无关像素，位置乘以区域的 scale；RIGHT 从右边计算，STRETCH 使用剩余宽度。自绘控件保留自身 ImGui ID，返回非零即执行已配置动作。换成另一个动作或移到不同类型视图时，宿主改用普通按钮，避免调用原视图的私有控件。自绘模块必须使用匹配宿主的 ImGui 版本。

`Views` 中的 Tools、Tool Providers、Toolbar Entries、Shortcuts 管理启用的项目。配置仍写入同一个 JSON，工具栏覆盖只记录指定字段；`visible: false` 隐藏项目，删除覆盖恢复默认项目。自建项目需指定位置、动作、尺寸；需要参数的命令还可指定无对象引用的 `arguments` 字段记录。完整修改先校验再保存，保存失败恢复原配置和窗口。

模块登记的控件可引用另一个可选模块的动作。该动作模块缺失或停用时，控件保留但不可用；模块恢复后重新校验参数并启用。新增控件或替换动作必须能在当前命令中找到目标，错误名称不会静默生效。内置工具和控件随所属视图模块一起启停，外部模块遵循相同登记流程。

```json
{
  "toolbar-items": [
    {"id": "layout.pan", "visible": false},
    {"id": "custom.pan", "location": "layout.top", "action": "layout.pan",
     "title": "Pan", "position": [375, 1], "size": [80, 19]}
  ],
  "bindings": [{"action": "layout.pan", "chord": "Ctrl+K"}]
}
```

普通会话动作调用文档命令时，该文档命令使用自己的完成编号；只有嵌套的文档编辑共用外层编号和撤销步骤。立即或延后执行的修改均可通过 `application.operation` 读取最终文档版本，界面据此恢复对象位置和选区。
