# Cadly 架构、UI 与功能改进评审

**评审基线：** commit `fb56f40`（2026-08-13）。上一版基线 `6b85a4e`（2026-07-18），
其间落地 30 个 commit。
**规模：** 约 14,800 行 C++/GLSL（含 shader 与测试）。最大的三个文件是
`GLRenderer.cpp` 2,077 行、`MainWindow.cpp` 1,859 行、`OcctShapeToMesh.cpp` 995 行
——比上一版分别增长 22% / 28% / 13%。
**方法：** 通读全部公共头、核心实现、shader、CMake、CI 与测试；`linux-release`
构建、`ctest` 全绿（8/8，1.8 秒）；用 `cad_import_cli` 对仓库内样件做了导入与
tessellation 对照实验。未修改产品代码。验证记录见文末附录。

> 本文件取代同路径下 2026-07-18 的《代码、模块和架构详细审查》。范围从"代码/模块/
> 架构"扩展到 **架构 + UI 设计 + 功能**三条线，因为当前阻碍产品前进的问题已经不
> 只在代码结构里。

---

## 0. 上一版评审的处理结果

先对账。上一版 24 条 finding 中已解决 9 条，其中包括全部 6 条 P0 里的 4 条：

| 上版编号 | 内容 | 状态 | 证据 |
| --- | --- | --- | --- |
| P0-1 | OCAF document 泄漏 | **已修** | RAII [`XcafDocumentLease`](../../src/cad/src/XcafDocumentLease.h)，并有 [`open_xcaf_document_count()`](../../src/cad/include/cadly/cad/XcafSession.h) 作为可断言的诊断缝 |
| P0-2 | 取消在最耗时阶段不可达 | **已修** | [`OcctProgressBridge`](../../src/cad/src/OcctProgressBridge.h) 把 sink 桥到 `Message_ProgressIndicator`，Transfer 与批量 mesh 都可中断；`ImportResult::cancelled` 明确区分取消与失败 |
| P0-4 | 颜色二次 tint / face 错位 | **已修** | 顶点色恒为白，材质是唯一来源（[OcctShapeToMesh.h:35](../../src/cad/src/OcctShapeToMesh.h#L35)）；材质按 `source_face_id` 配对而非位置（[OcctShapeToMesh.cpp:883](../../src/cad/src/OcctShapeToMesh.cpp#L883)） |
| P0-5 | mesh cache 丢实例颜色 | **已修** | prototype 几何与 occurrence 外观分离，实例色走 `Node::material_override`（[OcctShapeToMesh.cpp:957](../../src/cad/src/OcctShapeToMesh.cpp#L957)） |
| P1-1 | `load_hierarchy` 是 no-op | **已修** | 选项已删除 |
| P1-2 | mesher 重复执行 | **已修** | 参数构造即 Perform，批量后用 `fully_triangulated()` 跳过安全网 |
| P1-7 | IBL 失败静默 | **已修** | 每个 bake target 都过 `glCheckFramebufferStatus`；bake 改为分帧增量（`IblBakePlan`），不再阻塞首帧 |
| — | GPU mesh 泄漏 | **已修** | `attach_scene` 增加驱逐 pass，`MeshGpu::source` 消除 ABA |
| — | 大文件二次解析 | **已修** | geometry-only fallback 改为惰性，只在 XDE walk 空场景时才跑 |

仍然成立的：**P0-3**（逐 face draw call）、**P0-6**（首帧同步上传）、**P1-3**（单位
元数据）、**P1-4**（非 ASCII 名称）、**P1-5**（MainWindow 职责）、**P1-6**（renderer
单体 + 接口太薄）、**P1-8**（picking 未实现）、**P1-9**（source identity 不唯一）、
**P1-10**（镜像分解）、**P1-11**（float 世界坐标）、**P1-12**（进度倒退）、以及全部
P2。下文只重述仍然成立的部分，并补充这一个月新出现的问题。

---

## 1. 总体判断

**三条架构不变量依然守得很好**，这是这个代码库最值钱的东西，任何重构都不要动它：
`scene` 不含 Qt/OCCT/GL、`cad` 是唯一链接 OCCT 的 target、`renderer_gl` 无 Qt 依赖
（overlay/scale bar/坐标轴都在 GL 里画，没有退回 QPainter）。

**问题的重心变了。** 上一版说"运行时职责在向三个 God Object 聚集"——这一个月里这
三个文件各自又长了 13–28%，没有一个被拆过。同时，这个月新增的功能（selection
highlight、isolate、hide others、i18n）全部是 **UI 状态直接写进 `scene::Node` 标志位**
的模式，于是"UI 拥有场景可变状态"从一个小便利变成了架构事实：现在有三处独立代码
（sidebar 写、MainWindow 记忆并重放、renderer 读）共同维护同一份视图状态，且每次
tab 切换都要"擦掉再重放"（[MainWindow.cpp:1399](../../src/ui/src/MainWindow.cpp#L1399)）。
测量、剖切、批注一旦进来，这个模式会立刻崩掉。

**产品层面，Cadly 现在是一个非常漂亮的"看"，还不是"查"。** 三种显示模式、silhouette、
hidden line、isolate、highlight 的完成度已经高于很多开源查看器；但左键至今不能拾取
（[IRenderer.h:47](../../src/renderer/include/cadly/renderer/IRenderer.h#L47) 仍是
stub），因此选择只能从树里发起，测量/剖切/爆炸/PMI 全部无从谈起。上一版把 picking
列为"第一优先、是多个功能的共同基础"，这一个月的投入却都花在了它的**上层表现**
（高亮、遮挡幽灵线、隔离样式）上——这些工作本身质量很高，但它们都在为一个还没有
入口的交互做视觉打磨。

**性能架构仍然是缺席的，而不是不够好。** 没有 draw call 批处理、没有视锥剔除、没有
LOD、没有上传预算、没有任何 GPU 侧计数器。样件规模下（39–213 个面）完全看不出来，
所以这件事很容易一直不做，直到用户打开第一个真实装配。

---

## 2. 架构

按风险排序。`P0` 会在真实规模模型上直接破坏可用性；`P1` 是在扩功能之前应当解决的
结构问题；`P2` 是长期债务。

### A1（P0）渲染按 CAD face 逐 draw call，且没有任何剔除或批处理

**证据：** 每个成功的 face 产生一个 `Submesh`；PBR pass 对每个 node 的每个 submesh
发一次 `glDrawElements`，并在同一循环里更新 6 个 uniform
（[GLRenderer.cpp:1925-1954](../../src/renderer_gl/src/GLRenderer.cpp#L1925)）。node
循环没有视锥剔除、没有距离 LOD、没有实例化——`scene->nodes` 有多少就画多少
（[:1960](../../src/renderer_gl/src/GLRenderer.cpp#L1960)）。

线模式更重：`draw_silhouettes` 每个 node 把**整个三角形索引缓冲**再送一次几何着色器
（[GLRenderer.cpp:1658](../../src/renderer_gl/src/GLRenderer.cpp#L1658)）。hidden-line +
dimmed hidden + 有选中部件时，一帧里 edge 与 silhouette 各跑 4 趟（遮挡趟、可见趟、
选中遮挡趟、选中可见趟），合计 8 次整场景 node 遍历，其中 4 趟 silhouette 每趟都要
把全部三角形重新过一遍几何着色器
（[:1975-2022](../../src/renderer_gl/src/GLRenderer.cpp#L1975)）。

**影响：** draw call 数量 = CAD 面数，与三角形数无关。上一版实测的 231 MB 样件有
60,185 个面——单帧约 6 万次 surface draw call，加上线模式的多趟遍历。仓库内样件
（as1 39 面 / bearing 213 面）完全掩盖了这一点。

**建议：** 导入后按材质合并 index batch，face 边界改为存 range 表供 picking 用（保留
`source_face_id` 不等于每帧必须逐面 draw）；重复 prototype 走实例化；per-draw uniform
换成 UBO/SSBO 数组 + `gl_DrawID`；先把 draw-call / 三角形 / GPU 时间做成 renderer 自
己的 counter 并显示在诊断条上——**没有计数器就没有优化闭环**，这一条应当最先做。

### A2（P0，新增）首帧同步上传，且切换标签页会整批驱逐并重传

**证据：** GPU 上传只发生在绘制循环内部的 `ensure_mesh_upload`（三处调用：
[:1499](../../src/renderer_gl/src/GLRenderer.cpp#L1499)、
[:1636](../../src/renderer_gl/src/GLRenderer.cpp#L1636)、
[:1913](../../src/renderer_gl/src/GLRenderer.cpp#L1913)），每个 mesh 一次性
`glBufferData` 顶点 + 索引 + edge strip + 三级 edge LOD，没有任何每帧预算。

更严重的是 `attach_scene` 的驱逐 pass：它以**新场景引用的 mesh 集合**为白名单，把不
在其中的 GPU buffer 全部删除（[:618-640](../../src/renderer_gl/src/GLRenderer.cpp#L618)）。
而 `activate_document` 在每次标签页切换时都会 `viewport_->set_scene(...)`
（[MainWindow.cpp:1403](../../src/ui/src/MainWindow.cpp#L1403)），下一帧即触发一次
attach——**于是 A/B 两个文档来回切，每次都把对方的全部几何从显存删掉再重传**。

**影响：** 进度条到 100% 之后仍有一次"假死"首帧；多文档工作流（这是本月刚做的核心
UI 能力）在大模型上每次切页都要付一次全量上传。

**建议：** 明确 CPU-ready / GPU-ready 两个阶段；`attach_scene` 改为"标记非活跃"而不是
删除，配合一个按字节数的 LRU 驱逐；上传做成每帧预算 + 可见节点优先，用已经存在的
`needs_redraw()` 机制驱动续帧（IBL 已经是这个模式，照抄即可）。首帧可交互时间应当
进测试。

### A3（P1）`IRenderer` contract 太薄，`GLRendererImpl` 是 2,077 行单体

**证据：** 接口只有 initialize/resize/attach_scene/render/needs_redraw/pick/shutdown，
全部返回 `void`，没有 capabilities、没有错误结果、没有上传/剖切/选择缓冲的概念，
`pick()` 是默认 stub。实现类一个人承担 GL 资源缓存、shader 构建、IBL bake、PBR、
edges、silhouette、triangle mesh、background、MSAA、矢量字体、scale bar、坐标轴、
isolate ghost pass 与 selection pass。

**影响：** 想加剖切就得改 `DisplayMode`（已经是 20 个字段的杂货铺）+ 改单体内部若干
pass 的隐式状态顺序；Vulkan 后端只能照抄 OpenGL 宿主的假设；错误只能通过日志出口，
UI 无法告诉用户"你的 GPU 不支持 X，已降级"。

**建议：** 先做 OpenGL 内部拆分（`GpuMeshCache` / `FrameTargets` / `PbrPass` /
`LinePass` / `OverlayPass`），再把 backend-neutral 的 `RenderSceneView`、
`RenderSettings`、`RenderCapabilities`、`RenderStats` 提到 `renderer`；
initialize/upload 返回 result。Vulkan 是否真的要做，等拆完再判断。

### A4（P1）`MainWindow` 仍是 application service，且测试代码进入产品二进制

**证据：** 1,859 行里包含 `DocumentState` 模型、import worker 与 sink、进度轮询、
recents 菜单、QSettings schema、全部 QAction、面板构建、显示状态机、拖放、isolate
状态记忆、关闭时的取消等待。

新问题：`run_demo()` 是一段 170 行的截图驱动逻辑
（[MainWindow.cpp:1139-1308](../../src/ui/src/MainWindow.cpp#L1139)），列举了 20 多个
demo 名字，**编译进发布二进制**并通过 `--demo` 暴露给最终用户。它是很好的验证手段
（这也是这个项目能无人值守验证 UI 的原因），但它应当在 test-only 编译开关后面，或
拆成独立的 `DemoController`。

**建议（与上一版一致，优先级上调）：** 抽出 `app::DocumentSession`（文档集合、活动
文档、每文档的相机/显示/视图状态）、`app::ImportCoordinator`（队列、取消、进度、
结果替换）、`app::SettingsStore`（schema + 版本 + 迁移），`ui::MainWindow` 只负责把
QAction/widget 绑到这些 signal 上。`app` 应当变成可测试的 library，`cadly` 可执行文件
只做 composition root。

### A5（P1）视图状态寄生在 `scene` 里，没有 selection/visibility service

**证据：** `Node::visible / selected / ghosted` 是场景数据，由 sidebar 直接递归写入
（[SidebarWidget.cpp:374-396](../../src/ui/src/SidebarWidget.cpp#L374)），场景交接时
再全部清零（[:324](../../src/ui/src/SidebarWidget.cpp#L324)），然后由 shell 从
`DocumentState::isolate_node` 重放（[MainWindow.cpp:1402-1409](../../src/ui/src/MainWindow.cpp#L1402)）。
renderer 只检查当前 node 的 `visible`，不检查祖先；`world_bounds` 也包含隐藏节点。

**影响：**
- 语义分散：有效可见性（含祖先）、可见包围盒、选择包围盒三个概念没有任何一处集中
  定义，于是 `Fit` 会把隐藏和 ghosted 的部件也框进去（见 U2）。
- 每次 solo/isolate 都要 O(n) 递归写场景 + O(n) 递归同步 item roles
  （[:398](../../src/ui/src/SidebarWidget.cpp#L398)）。
- 一旦支持并发导入或后台重算，"UI 线程正在改 renderer 正在读的 scene"就是数据竞争。

**建议：** 把视图状态从 `scene` 移到每文档的 `ViewState`（可见/选择/隔离/未来的剖切
与批注），以稳定 id 为键；renderer 每帧收一个只读的 `RenderSceneView{scene, view_state}`。
`scene` 回到纯粹的导入产物——这恰好是它当初被设计成"稳定契约"的原因。

### A6（P1）身份与元数据仍然不可用于持久化

- **`source_label` 不唯一：** `path + "/" + name`
  （[OcctShapeToMesh.cpp:911](../../src/cad/src/OcctShapeToMesh.cpp#L911)），同名兄弟
  节点得到同一字符串，`find_node_by_label` 线性返回第一个
  （[Scene.cpp:74](../../src/scene/src/Scene.cpp#L74)）。
- **非 ASCII 名称变 `?`：** `read_label_name` 逐字符丢弃 >127 的码位
  （[OcctShapeToMesh.cpp:512](../../src/cad/src/OcctShapeToMesh.cpp#L512)）。产品刚刚
  加了简体中文 UI，却读不出中文零件名——这个反差用户会立刻注意到。
- **元数据未落库：** `Node::layer` 全仓库无人写入（reader 却开了 `SetLayerMode(true)`），
  `XCAFDoc_VisMaterial`、透明度、PMI/validation properties 一概不提取。
- **镜像分解丢符号：** `Transform::from_matrix` 用三列长度当作全正 scale，不检查
  determinant（[Transform.h:21](../../src/scene/include/cadly/scene/Transform.h#L21)），
  镜像 occurrence 会朝向错误。

**建议：** 保存 XCAF label entry + prototype id + occurrence index + 文件 hash 作为
identity，显示名只用于 UI；scene 字符串统一 UTF-8；node 保留原始 affine 矩阵，需要
时再做经过校验的分解；为每类元数据输出 imported/dropped 诊断，避免 UI 上写着
"Load names/colours" 却悄悄丢东西。

### A7（P1）单位是"假的"，并且已经在破坏 tessellation 质量

这一条上一版列为 P1-3，但当时没有量化。本次实测把它坐实了：

**证据：** IGES 的 `unit_to_meters` 硬编码 `0.001f`
（[OcctIgesImporter.cpp:114](../../src/cad/src/OcctIgesImporter.cpp#L114)）；STEP 从
进程级全局 `Interface_Static::CVal("xstep.cascade.unit")` 猜，且无论结果如何
`source_unit` 一律写死 `"mm"`（[OcctStepImporter.cpp:132](../../src/cad/src/OcctStepImporter.cpp#L132)）。

`test_files/bearing.iges` 的 IGES 全局段声明单位 MM、最大坐标 1000，导入后 scene
的包围盒却只有 `0.101 × 0.122 × 0.031`。于是：

1. scale bar 用 `world × unit_to_meters` 计算，会把这个轴承标成约 0.12 mm 宽——差
   1000 倍；未来的测量功能会继承同一个错误。
2. 更隐蔽的是 tessellation：visual-relative 算出的弦高被 `min_linear_deflection`
   （默认 0.01，**单位是模型单位**）夹上去，最终 deflection = 0.01 = 整个模型尺寸的
   8.2%。实测同一文件把下限调到 1e-5 后三角形数从 **5,730 涨到 27,306（4.8 倍）**，
   代价只是 mesh 时间 66 ms → 100 ms。也就是说，任何非 mm 尺度的模型都会被默认参数
   静默地欠细分。
3. `min_face_area`（默认 1e-8）同样是模型单位量纲，同样会随尺度失真。

**建议：** 分开存 `file_unit` / `working_unit` / `file_to_working` / `working_to_meter`，
从 reader model 与 XCAF document 的长度单位读取而不是猜；把 tessellation 的绝对量纲
参数（min deflection、min face area）改为相对于模型 extent 表达，或在解析出真实单位
后归一化；诊断里显示单位来源。用 mm/inch/m 三个 fixture 做验收。

### A8（P2）依赖与构建细节

- `Cadly::Ui` 把 `Cadly::RendererGL` 和 `Cadly::Cad` 列为 **PUBLIC** 链接依赖
  （[src/ui/CMakeLists.txt:48](../../src/ui/CMakeLists.txt#L48)），但 RendererGL 只在
  `ViewportWidget.cpp` 用到，应当是 private implementation detail。
- `cadly_scene` private link `Cadly::Platform`，而 scene 的三个源文件不使用 platform
  的任何东西——依赖噪声，且会让"scene 只依赖 glm"的不变量在 CMake 层面读起来不成立。
- OCCT 头以普通 `target_include_directories(... PRIVATE ...)` 引入
  （[src/cad/CMakeLists.txt:68](../../src/cad/CMakeLists.txt#L68)），不是 `SYSTEM`，
  第三方警告会淹没自家的 `-Wall -Wextra -Wpedantic` 基线。
- `CADLY_WARNINGS_AS_ERRORS` 默认 OFF，且没有任何 preset 打开它——警告基线目前没有
  强制力。建议 CI 的 Linux debug 配置打开。
- `ImporterRegistry` 是单例（`instance()`），无法注入 fake importer，导入生命周期只能
  用真实 OCCT 跑，直接影响 A4 拆分后的可测试性。

### A9（P2）设置没有 schema 与迁移，且有两个所有者

`app::Settings`、`app::RecentFiles` 各自 `QSettings(IniFormat, ...)`，`MainWindow`
又自建一个同名 handle（[MainWindow.cpp:187](../../src/ui/src/MainWindow.cpp#L187)），
键分散在三处、没有版本号、控件删改后旧键永久残留。`Settings` 头注释写着"把直接
QSettings 调用挡在 MainWindow 之外"——这个约定实际上已经被绕过了。

---

## 3. UI 与交互设计

Graphite 外壳本身的完成度很高（token 表、自绘控件、暗/亮双主题、zero-chrome、
非模态导入、popover），下面这些不是"不好看"，是**交互闭环上的缺口**。

### U1（P0）左键仍然不能拾取，整条选择链路只有单向入口

viewport 的每一次左键都当作"点到空白"上报
（[ViewportWidget.cpp:168-171](../../src/ui/src/ViewportWidget.cpp#L168)），唯一作用是
取消高亮。用户在三维视图里看到一个零件，无法点它、无法知道它是谁、无法从它跳到树。
所有已经做好的 highlight/isolate 视觉效果都只能从左侧树发起。

**建议：** renderer 侧做一个 integer ID pass（node id + face id）+ 异步 readback，
定义 ID 稳定性、隐藏/半透明部件的命中规则、DPI 坐标契约；UI 侧建立 selection
service 做树 ↔ 视口双向同步。这是测量、剖切、批注、Fit Selection 的共同前置。

### U2（P1）Fit 不理会可见性、隔离与选择

`fit_view()` 直接用 `scene_->world_bounds`
（[ViewportWidget.cpp:151](../../src/ui/src/ViewportWidget.cpp#L151)），而 world_bounds
包含隐藏与 ghosted 节点。**用户 solo 或 isolate 一个零件后按 F，取景框回到整个装配**
——这与该模式的全部意图相反。也没有"缩放到选中"（Fit Selection），而这是 CAD 查看器
里使用频率最高的操作之一。

### U3（P1）模型树在真实装配规模下会成为瓶颈，且交互面太窄

- 每个 scene node 建一个 `QStandardItem`（[SidebarWidget.cpp:336](../../src/ui/src/SidebarWidget.cpp#L336)），
  没有虚拟化；几万节点的装配意味着几万个堆对象与一次全量构建。
- 每次眼睛/隔离操作都递归遍历整棵 item 树同步 role
  （[:398](../../src/ui/src/SidebarWidget.cpp#L398)），`select_node` 也是递归线性查找。
- 单选、无右键上下文菜单、无键盘可达的隐藏/隔离/展开全部、无"只显示有几何的节点"
  过滤、隐藏状态不跨重导入保留。

**建议：** 换成直接读 `scene::nodes` 的自定义 `QAbstractItemModel`（0 拷贝、O(1) 内存），
按 QModelIndex 精确发 `dataChanged` 而不是全树同步；补多选与上下文菜单。

### U4（P1）Inspector 的信息与控制密度远低于工具定位

- Display 面板只有 4 个旋钮：edge intensity、MSAA、scale bar、坐标轴
  （[InspectorWidget.cpp:161](../../src/ui/src/InspectorWidget.cpp#L161)）。没有材质
  预设/覆盖、没有环境与曝光、没有背景、没有剖切、没有边线颜色。
- Properties 面板只有 7 行（源文件/节点/label/mesh/材质/三角形/包围盒）
  （[PropertiesPanel.cpp:70](../../src/ui/src/PropertiesPanel.cpp#L70)）：**没有单位、
  没有体积/表面积/质心等质量属性、没有变换、没有 layer/颜色来源**。检查场景里
  这些恰恰是最常被问的问题。
- 诊断条的 "Log" 标签其实只有导入摘要；应用日志只走 stdout，没有文件 sink 也没有
  内存 ring buffer（[Log.cpp:22](../../src/platform/src/Log.cpp#L22)）——从桌面图标或
  打包版启动时，用户与支持人员都拿不到日志。

### U5（P1）导入体验的边界情况

- **一次只能导一个文件**：第二次 Open 只会在状态栏闪一句"An import is already running"
  （[MainWindow.cpp:1525](../../src/ui/src/MainWindow.cpp#L1525)）。多文档 tab 已经在
  了，导入队列却没有。（注意：并发导入还被 `cad` 里的进程级全局 `Interface_Static`
  单位读取挡着，属于 A7 的连带问题。）
- **拖放只取第一个 URL**（[:1766](../../src/ui/src/MainWindow.cpp#L1766)），多选拖入
  会静默丢弃其余文件。
- **`.p21` 进不来**：importer 支持 `.p21`（[OcctStepImporter.cpp:53](../../src/cad/src/OcctStepImporter.cpp#L53)），
  但文件对话框过滤器（[:1519](../../src/ui/src/MainWindow.cpp#L1519)）和拖放白名单
  （[:201](../../src/ui/src/MainWindow.cpp#L201)）都没有它；`ImporterRegistry` 也仍是
  纯扩展名匹配，注释里承诺的 content probe 不存在。
- **进度条会跳到 100% 然后停住（或倒退）**：批量 tessellation 映射到 [0.45, 0.70]
  （[OcctShapeToMesh.cpp:799](../../src/cad/src/OcctShapeToMesh.cpp#L799)），紧接着的
  装配遍历直接用 `i / labels.Length()` 上报
  （[:977](../../src/cad/src/OcctShapeToMesh.cpp#L977)）。绝大多数 STEP 只有一个 free
  shape，于是进度立刻变成 100% 并卡在那里走完整个遍历（顶点提取 + 三级 edge LOD
  采样，是第二重的阶段）；多 root 文件则从 0.70 掉回 1/N。
- 已打开的文件在磁盘上更新后没有重载提示，也没有文件监视。

### U6（P2）状态与反馈的可信度

状态栏的 `frame X ms` 是 `paintGL` 的 CPU 提交耗时，不是 GPU 帧时间
（[ViewportWidget.cpp:112](../../src/ui/src/ViewportWidget.cpp#L112)，注释里说清楚
了，但 UI 上没有）。没有 draw call 数、没有显存占用、没有 GPU timer query。诊断条
只在导入后有内容，运行期是空的。

### U7（P2）输入与可达性

- 只处理 `angleDelta`，不处理触控板的 `pixelDelta`，也没有捏合手势
  （[ViewportWidget.cpp:191](../../src/ui/src/ViewportWidget.cpp#L191)）——笔记本用户
  的缩放会是跳变的。
- 右键被 orbit 占用且禁用了系统菜单，视口里因此没有任何上下文菜单入口（未来
  "隐藏/隔离/缩放到此"最自然的位置）。
- 快捷键全部硬编码，不可自定义；无视口键盘导航；自绘控件没有 accessible name。

---

## 4. 功能缺口

不是 bug，但它们决定 Cadly 能不能进入"检查"这个使用场景。按依赖排序：

| 功能 | 现状证据 | 依赖 |
| --- | --- | --- |
| 3D 拾取 / 视口选择 | `IRenderer::pick` 为 stub | **第一优先**，下面几项的共同前置 |
| Fit Selection / 缩放到零件 | 只有整场景 `world_bounds` | 选择 + 可见包围盒服务 |
| 测量（点/边/面/距离/角度） | 无任何几何查询接口 | 拾取 → double 精度 B-Rep 查询 → 结果持久化 |
| 剖切 / 盒裁剪 | `DisplayMode` 与 shader 里没有 clipping plane | `RenderSettings` → cap/stencil → 视口 manipulator |
| 质量属性（体积/面积/质心） | Properties 面板无此项 | `cad` 侧 `BRepGProp` + 单位正确性（A7） |
| 爆炸视图 | 无 occurrence 偏移状态 | 稳定 occurrence id（A6） |
| 透明材质 | 单一不透明 pass，XCAF alpha 未读取 | 材质语义 + 排序/OIT pass |
| PMI / GD&T | 导入不保存 | importer 元数据模型 → renderer overlay |
| 版本对比 | 无 stable provenance/diff | 稳定 id + 缓存 |
| 导出审阅结果（截图/批注/报告） | 无 annotation/review 模型 | 选择/测量持久化 |
| 大装配 LOD / 流式加载 | 无剔除无 LOD | A1 + A2 |
| 视觉质量：地面/接触阴影/AO、HDRI、曝光与色调控制 | 程序化环境 + 固定 tonemap，无后处理 | renderer pass 拆分（A3） |

---

## 5. 测试与工程基线

`ctest` 目前 8 个用例全绿，且比上一版实质性变强：新增了 XCAF document 归零断言、
取消语义断言、颜色单一来源断言、周期面法线断言、tessellation policy 的无穷大回退
断言。但有一个必须立刻处理的问题：

### T1（P0）CI 里几乎没有真正跑到的导入断言

`.gitignore` 只放行了一个样件：

```
test_files/*
!test_files/as1-ug-214.stp
```

`git ls-files test_files/` 确认仓库里只有 `as1-ug-214.stp`。而 smoke test 里所有导入
相关的用例都写成"fixture 不存在就 return"：hammer.iges（visual-relative 导入）、
screw.step（周期面法线）、hammer.iges（XCAF 归零的成功路径、取消语义）、
`KR600_R2830-4.stp`（颜色单一来源）——**在 CI 上全部静默跳过**，只剩下"垃圾文件导入
失败"那半个用例。本机因为有未跟踪的样件才会真的执行（这也是本地全套只跑 1.8 秒的
原因之一）。

也就是说：本月修掉的 P0-4/P0-5 颜色回归、P0-1 文档泄漏的成功路径，**在 CI 上没有任何
防线**。CI 现在真正的导入闸门只有一句 `cad_import_cli test_files/as1-ug-214.stp` 的
退出码。

**建议：** 建一组小体积、许可证明确的自制 fixture（一个带实例颜色的装配、一个带
面颜色的实体、一个镜像 occurrence、一个非 mm 单位模型、一个中文零件名模型、一个
IGES 曲面 quilt），几十 KB 级别、随仓库跟踪；把"fixture 缺失就跳过"改成"缺失即
失败"，需要大模型的用例单独标 label 并允许本地跑。

### T2–T4（P1）其余空白

- **渲染层零测试**：没有 offscreen/llvmpipe golden image、没有 GL 状态断言、没有
  draw-call 预算测试、没有 context-loss 测试。silhouette 这类视角相关效果目前只靠
  人眼看截图。
- **无导入生命周期测试**：取消、导入中关闭标签页、多 tab 重导入、失败文档状态，
  全部只能手工验证——这正是 A4 拆分之后可以自动化的部分。
- **无性能与内存回归**：没有 benchmark dashboard、没有 20 次循环导入的 RSS soak、
  没有 ASan/UBSan 任务。
- **警告基线无强制**：见 A8。

---

## 6. 建议顺序

不建议大重写。按"先让现有东西可信，再让它可扩展，最后加功能"排：

1. **止血（几天量级）**：CI fixture 落库并去掉静默跳过（T1）；进度阶段修正（U5）；
   `.p21` 与多文件拖放（U5）；Fit 尊重可见性/隔离（U2）；OCCT 头改 SYSTEM + 打开
   warnings-as-errors（A8）。
2. **单位与身份的正确性**：真实单位读取 + 量纲相关参数归一化（A7）；UTF-8 名称与
   稳定 occurrence id（A6）。这两条是测量、审阅回放、版本对比的地基，越晚改代价越大。
3. **性能闭环**：先加 renderer counters（draw call / 三角形 / 上传字节 / GPU 时间）
   并显示在诊断条；再做材质批处理与视锥剔除（A1）；再做上传预算与 LRU 显存缓存
   （A2）。有了计数器，后两步才有验收标准。
4. **应用层职责拆分**：`DocumentSession` + `ImportCoordinator` + `SettingsStore`，
   demo hook 移出产品二进制（A4、A9）；视图状态移出 `scene`（A5）。
5. **拾取基础设施**：ID pass + 异步 readback + selection service + 树/视口双向同步
   （U1）。完成后立刻能兑现 Fit Selection、视口上下文菜单、"点谁看谁"。
6. **renderer 内部分 pass 并加 capabilities**（A3），为剖切与透明留出位置。
7. **用户功能**：测量 → 剖切 → 质量属性 → 批注/审阅导出；视觉侧的地面/AO/接触阴影
   与曝光控制可以并行推进（依赖第 6 步）。

全程保持三条不变量：`scene` 不依赖 Qt/OCCT/GL、`cad` 是唯一 OCCT owner、renderer
overlay 不退回 QPainter。

---

## 附录：本次验证记录

```bash
cmake --build --preset linux-release          # ninja: no work to do（树是干净的）
ctest --preset linux-release                  # 8/8 passed, 1.79s

build/linux-release/bin/cad_import_cli test_files/as1-ug-214.stp
#   28 nodes / 39 faces / 2,096 tris / 58 ms / extent 200 / deflection 0.075
build/linux-release/bin/cad_import_cli test_files/linkrods.step
#   1 node / 37 faces / 5,522 tris / 590 ms（其中 parse 563 ms）
build/linux-release/bin/cad_import_cli test_files/bearing.iges
#   1 node / 213 faces / 5,730 tris / extent 0.121977 / deflection 0.01（= 8.2% extent）
build/linux-release/bin/cad_import_cli test_files/bearing.iges --min-deflection 0.00001
#   27,306 tris / deflection 4.57e-05 / mesh 66→100 ms   ← A7 的量化证据

git ls-files test_files/                      # 仅 as1-ug-214.stp        ← T1 的证据
head -c 1400 test_files/bearing.iges          # IGES 全局段：单位 MM、最大坐标 1000
```

硬件与环境：WSL2（Linux 6.18）、系统 Qt 6.4 + OCCT 7.6.3、`linux-release`
(RelWithDebInfo)。所有结论要么来自源码引用，要么来自上述命令输出；引用上一版报告
的数字（231 MB 样件 60,185 面、98.3 秒导入）在本次未复测，已在正文标注来源。
