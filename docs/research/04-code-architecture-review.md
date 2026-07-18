# Cadly 代码、模块和架构详细审查

**审查基线：** commit `6b85a4e`（2026-07-18）  
**规模：** 约 10,800 行 C++，核心大文件为 `GLRenderer.cpp` 1,704 行、`MainWindow.cpp`
1,452 行、`OcctShapeToMesh.cpp` 879 行。  
**方法：** 阅读全部公共头、核心实现、CMake、shader 和测试；Debug 构建、CTest、8 个 CAD
样本导入；未修改产品代码。

## 总体判断

模块名和最初的依赖方向是合理的，尤其值得保留的是：

- `scene` 没有 Qt/OCCT/OpenGL 头，作为 importer 和 renderer 之间的 DTO 边界是正确的；
- 只有 `cad` 链接 OCCT toolkit；
- `renderer_gl` 通过宿主提供函数地址，代码没有 Qt 依赖；
- overlay、scale bar、axes 和 edge 都在 renderer 中，不用 QPainter 覆盖 viewport；
- import 在 QtConcurrent worker 运行，UI 没把 98 秒的导入放到 GUI thread；
- STEP/IGES 都使用 XCAF，能保留基础 assembly/name/color，而不是只读一个 compound；
- 诊断、分阶段计时、tessellation policy 和公共 CLI 是后续优化的重要基础。

但当前“模块边界正确”更多体现在 CMake 目标名，运行时职责已经向三个 God Object 聚集：

```text
MainWindow (UI + document model + import coordinator + persistence + workflow)
       |                         |
       v                         v
OcctShapeToMesh             GLRendererImpl
(XCAF traversal + mesh +    (resources + IBL bake + PBR + every pass + HUD font)
 colors + edge LOD)
```

这三个文件分别同时拥有策略、数据转换、资源生命周期和呈现。当前体量仍能维护，但选择、
测量、截面、缓存、透明材质或 Vulkan 任意一项都会继续放大耦合。建议保留模块，重划模块内
的职责，而不是再增加一层“Manager”名字。

## 依赖审查

实际依赖图：

```text
app executable
  -> ui -> cad -> scene -> glm
       |     -> platform
       -> renderer (interface) -> scene
       -> renderer_gl -> renderer + scene + platform + OpenGL
  -> platform
```

### 合理处

- `cad -> scene` 和 `renderer_gl -> renderer -> scene` 没有反向依赖。
- `app` 是 composition root，创建 QApplication、theme、settings、recents 和 MainWindow。
- `platform` 只承担 log/path；没有把 GUI 或系统窗口塞进去。

### 边界偏移

- `ui::MainWindow` 的公共头直接包含 `cad/ICadImporter.h`，实现中创建 importer job、持有
  `ImportResult/ImportSummary`、写 QSettings。UI 已成为 application service。
- `Cadly::Ui` 将 `Cadly::RendererGL` 和 `Cadly::Cad` 作为 `PUBLIC` link dependency；
  RendererGL 只在 `ViewportWidget.cpp` 用，应是 private implementation detail。
- `scene` 在 CMake 中 private link `Cadly::Platform`，但 scene 源没有使用 platform；这是小的
  依赖噪声。
- `IRenderer` 的注释和生命周期仍以 GL context 描述，接口只有 initialize/resize/attach/render/
  shutdown，缺少 capabilities、错误结果、异步资源上传、selection/section render data；未来
  Vulkan 实现会被迫模仿 OpenGL host，而不是实现真正后端中立的 contract。

## Findings

以下按风险排序。`P0` 是会破坏正确性、可取消性、内存或大模型可用性的项；`P1` 是在扩展
核心功能前应解决的结构/行为问题；`P2` 是长期质量债务。

### P0-1：OCCT document 没有 Close，导入会在应用 session 中滞留

**证据：** STEP 在
[OcctStepImporter.cpp](../../src/cad/src/OcctStepImporter.cpp#L75) 调 `NewDocument`，IGES 在
[OcctIgesImporter.cpp](../../src/cad/src/OcctIgesImporter.cpp#L54) 做相同操作；所有返回路径都
没有 `app->Close(doc)`。OCCT `TDocStd_Application::Close` 的 contract 是让 document 不再由
application session 管理。

**影响：** re-import 或多文档会保留 OCAF graph 和 triangulation，内存峰值逐次上升；失败/
取消路径同样泄漏 session ownership。本机 RC 模型单次峰值约 426 MB，同进程顺序导入三次
峰值约 633 MB；allocator cache 会干扰数值，但 API 生命周期问题本身确定。

**建议：** 建一个仅存在于 `cad` 内部的 RAII `XcafDocumentLease`，析构时 Close；在返回 scene
前 scene 必须已经复制完所需数据，不持有 TDF/TopoDS 引用。用同进程循环导入 20 次的 RSS
平台测试验收。

### P0-2：取消在最耗时阶段不可达

**证据：** STEP 只在 `ReadFile` 后检查一次
[progress.cancelled](../../src/cad/src/OcctStepImporter.cpp#L101)，随后 `Transfer(doc)` 可单段
执行 53.5 秒；批量 `tessellate(all)` 也不接 cancel。assembly walk 才再次轮询。IGES 同样。

**影响：** 用户点击取消或关闭窗口时，`MainWindow::closeEvent` 会
`waitForFinished()`；窗口可能冻结数十秒。API caller 取消后还可能收到一个 `success=true` 的
部分 scene，因为 importer 只检查 nodes 是否为空。

**建议：** 将 atomic sink 适配为 `Message_ProgressIndicator`，传给 Transfer 和 mesher；每个
阶段结束再检查 cancel，并返回明确 `ImportStatus::Cancelled`，不要用 `success=false` 猜测。

### P0-3：逐 CAD face 的 draw call 在大模型上不可扩展

**证据：** `append_face` 每个成功 face push 一个
[Submesh](../../src/cad/src/OcctShapeToMesh.cpp#L228)；PBR pass 在
[GLRenderer.cpp](../../src/renderer_gl/src/GLRenderer.cpp#L1621) 遍历 submeshes 并逐个
`glDrawElements`。231 MB 样本有 60,185 faces。

**影响：** 单帧可能产生约 60k surface draw calls，edge/mesh passes 另算。GPU buffer 虽复用，
driver submission 和 uniform update 会让旋转性能崩溃。为 future picking 保留 face range，
不等于每帧必须逐 face draw。

**建议：** 导入后按 material 合并 index batch；face id 放 integer vertex attribute/SSBO 或
selection pass；重复零件 instancing；renderer 记录 draw-call telemetry 和 GPU time。

### P0-4：颜色合成存在二次 tint 和 face/submesh 错位

**证据：** `shape_to_mesh` 把 shape/instance fallback color 烘进每个 vertex；随后
`build_mesh_for` 又给每个 submesh 分配同色或 face color material。PBR shader 使用
`u_base_color.rgb * v_vertex_color.rgb`。同一个颜色会平方，face override 也会被 fallback
再次相乘。

另外 `append_face` 在 triangulation 为空或面积太小时不 push submesh，但
[material assignment loop](../../src/cad/src/OcctShapeToMesh.cpp#L773) 对每个源 face 都增加
`sub_index`。任何被跳过的前序 face 都会让后续材料错位。

**影响：** STEP/IGES 颜色变暗、偏色或落到错误面，直接破坏查看器可信度和“默认材质不好看”
问题的判断。

**建议：** 只保留一个 base-color source；`append_face` 返回 emitted face id/submesh index
映射，材料按映射赋值。增加“shape color + face override + skipped tiny face + instance color”
fixture 和像素/scene assertions。

### P0-5：mesh cache 会丢实例级颜色

**证据：** mesh cache 只以 `shape.TShape().get()` 为 key。第一次 component 调
`build_mesh_for(shape, proto, instanceColor)` 后，后续同 prototype 不论实例颜色如何都直接
返回缓存 mesh；`Node::material_override` 没有被设置。OCCT 还提供专门的
`GetInstanceColor`，当前没有调用。

**影响：** 同一个螺栓/零件原型在不同 occurrence 使用不同颜色时，所有实例显示为第一个
实例的颜色。

**建议：** geometry cache 与 appearance 分离；Mesh 只缓存 prototype geometry/face style，
instance style 写 Node override 或 per-instance material table。缓存 key 不能混入 occurrence
颜色来复制几何。

### P0-6：第一帧同步上传全部 mesh，导入完成后仍可能卡 UI

**证据：** `ViewportWidget::paintGL` attach scene 后，GL renderer 在 node loop 内
`ensure_mesh_upload`，内部多次同步 `glBufferData`，还上传三级 edge LOD。

**影响：** 进度条到 100% 后首帧停顿；3.45 GB 峰值样本可能在 GUI thread 复制数百 MB 数据。
用户将其感知为“导入假完成”或窗口无响应。

**建议：** 明确 CPU-ready/GPU-ready 两阶段；每帧 upload budget、可见节点优先、共享 context
worker 或持久映射 staging。把 first interactive frame 纳入测试。

### P1-1：`load_hierarchy` 是用户可见的 no-op

**证据：** 字段在 `ImportOptions`、Import UI 和 settings 中存在，`src/cad` 没有任何读取。

**影响：** 用户取消勾选后得到完全相同结果，破坏设置可信度。此前 healing/welding 的 no-op
已经在 `fd9cafc` 删除，这个选项存在相同问题。

**建议：** 要么暂时删除，要么定义 geometry-only flatten contract，并用 assembly fixture
测试 node/mesh count 和 metadata loss。

### P1-2：mesher 被重复执行和重复检查

**证据：** 当前版本 OCCT 的参数构造函数自动 Perform，代码随后又 `mesher.Perform()`；整文档
批量 mesh 后，每个 unique shape 的 `shape_to_mesh` 又调用 `tessellate`。大模型 profile 中
`shape safety tessellation=4.45 s`。

**影响：** 导入尾段浪费 CPU，且让“parallel mesh”收益被串行小任务抵消。

**建议：** 只调用一次 Perform；批量后检查 missing/outdated triangulation 再局部补 mesh。

### P1-3：单位与来源元数据不可靠

**证据：** STEP 从全局 `Interface_Static::CVal("xstep.cascade.unit")` 推断比例，却始终把
`source_unit` 写成 `mm`；IGES `unit_to_m` 硬编码 `0.001f`，不读 `IGESData_GlobalSection` 或
XCAF document length unit。

**影响：** scale bar 和未来测量可能错误；“文件单位”和“OCCT transfer 后系统单位”被混成
一个字段。当前 `bearing.iges` header 明确是 MM，但代码无法验证其他 unit flag。

**建议：** 分开存 `file_unit`, `working_unit`, `file_to_working`, `working_to_meter`；从 reader
model/document API 读取并在 diagnostics 显示。使用 mm/in/m 三个 fixture 验收 bbox 和测量。

### P1-4：名称和路径不支持完整 Unicode

**证据：** `read_label_name` 逐个 `ExtendedCharacter` 转 char，非 ASCII 直接替换 `?`；UI
worker 把 QString path 转 `toStdString()` 再构造 filesystem path，Windows narrow path 和 OCCT
7.6 filename API 对非 ASCII 路径存在风险。

**影响：** 中文/日文零件名冲突、树不可读、source_label 不唯一；中文 Windows 路径可能无法
打开。

**建议：** scene string 统一 UTF-8，使用 OCCT/Qt 可支持的宽路径或 UTF-8 bridge；新增中文
文件名、装配名和 emoji/补充平面的明确支持策略（不支持也要诊断）。

### P1-5：`MainWindow` 同时承担四种职责

**证据：** 1,452 行文件包含 DocumentState、import worker/sink、recent menu、QSettings schema、
所有 QAction、panel 构建、display state machine、drag/drop、demo hook 和 close/cancel。

**影响：** 测量/截面/标注会继续堆 action/state；import lifecycle 只能通过完整 QWidget 测试，
多文件队列或 headless service 很难复用。

**建议拆分：**

- `app::DocumentSession`：documents、active id、camera/display/review state；
- `app::ImportCoordinator`：queue、cancel、progress、cache、result replacement；
- `app::SettingsStore`：schema/version/migration；
- `ui::MainWindow`：只 bind QAction/widget 和上述 signals；
- dev screenshot/demo 放独立 `DemoController` 或 test-only 编译开关。

这里的 `app` 应成为可测试 library，再由 `cadly` executable 做 composition root。

### P1-6：`GLRendererImpl` 是渲染单体，`IRenderer` contract 太薄

**证据：** 1,704 行实现包含 GL resource cache、shader build、IBL bake、PBR、edges、wireframe、
background、MSAA、scale-bar vector font、axes 和 selection stub。接口 `pick()` 默认返回 invalid，
没有 capabilities/error/upload/section/selection contract。

**影响：** Vulkan 或测试 backend 无法复用 render graph/feature policy；任何 pass 状态修改都在
一个类里隐式依赖前序 GL state，增加状态泄漏风险。

**建议拆分：**

- backend-neutral `RenderSceneView`, `RenderSettings`, `RenderCapabilities`；
- GL `GpuMeshCache`, `FrameTargets`, `PbrPass`, `EdgePass`, `OverlayPass`, `PostProcessPass`；
- renderer-owned `SelectionBuffer`/ID pass；
- initialize/upload/render 返回 result/error，不用日志作为唯一错误通道；
- 先完成 OpenGL 内部拆分，再判断 Vulkan 是否真的有产品需求。

### P1-7：IBL 初始化失败检测不完整

**证据：** `bake_env_cube/irradiance/prefilter/brdf_lut` 创建和 attach FBO 后不调用
`glCheckFramebufferStatus`；`bake_ibl()` 在 shader 有效时总返回 true。只有 MSAA FBO 做了
complete check。

**影响：** 某 GPU/format 不支持时 IBL 静默变黑，日志仍说初始化成功；用户看到的就是“材质
很差”。启动时数亿次 shader sample 也没有缓存或 GPU tier。

**建议：** 每个 bake target 校验并返回错误；将 BRDF LUT/HDRI prefilter 离线资产化或缓存；
记录 GPU/driver/format 和 fallback 状态到 diagnostics。

### P1-8：选择数据结构存在但完整链路未实现

**证据：** `SelectionId`、`Submesh::source_face_id`、`Node::selected` 都存在；renderer `pick`
仍是 stub，Viewport left click 只交给 base widget，tree selection 只更新 PropertiesPanel。

**影响：** 用户无法从 3D 选择、同步树、highlight、Fit Selection、测量和截面。它是市场 P0，
也是多个后续功能的共同基础。

**建议：** 先做 node/face integer ID pass 和 async readback，定义 ID 稳定性、hidden/transparent
规则和 DPI coordinate contract；tree/viewport 通过 selection service 双向同步。

### P1-9：source identity 以名称路径构造，不唯一

**证据：** `source_label = path + "/" + node.name`；同名 siblings 会得到相同字符串，
`find_node_by_label` 线性返回第一个。

**影响：** 标注/测量/review state 无法可靠回放；非 ASCII 名称都变 `?` 后更严重。

**建议：** 保存 XCAF label entry、prototype id、occurrence path/index 和 file hash；显示名称只用于
UI，不作为 identity。Scene 内增加 id->index map。

### P1-10：变换 TRS 分解会丢镜像符号并可能除零

**证据：** `Transform::from_matrix` 用三列长度作为全为正的 scale，再 `quat_cast`；没有检查
determinant、负 scale 或零 scale。

**影响：** mirrored occurrence 可能方向错误；normal matrix/culling 也可能不一致。gp_Trsf 能表达
mirror/uniform scale，导入器不能假设全是纯旋转平移。

**建议：** scene node 保留原始 affine matrix，动画需要时再使用经过验证的 decomposition；对
negative determinant 调整 winding/cull，新增 mirror fixture。

### P1-11：float 世界坐标对大偏置模型不稳

**证据：** OCCT double point 在导入时直接 cast 为 float；scene transform/camera 也为 float。

**影响：** 很大坐标或离原点很远的工厂/船舶/地理模型出现抖动、Z fighting、测量误差。

**建议：** importer 保留 double bounds/provenance；renderer 采用 model-origin rebasing（每个
mesh local origin + camera-relative world），测量在 double B-Rep/scene 空间完成。

### P1-12：进度值可能回退且阶段不真实

**证据：** outer importer 先 update 0.40，document conversion update 0.45，随后 free-shape loop
直接使用 `i / labels.Length()`，第一个 shape 可能回到很小百分比；XCAF Transfer 内没有更新。

**影响：** 进度条倒退、长时间卡在固定百分比、用户误判死锁。

**建议：** 阶段 progress scopes 映射到固定区间，并使用历史阶段权重；indeterminate 用 busy
状态，不伪造精确百分比。

### P2-1：importer 声称 extension/content probe，实际只有扩展名

`ImporterRegistry` 注释写 extension/probe，但 CanRead 只比较 suffix。GUI 过滤没有 `.p21`，
虽然 STEP importer 支持。错误扩展名或 extensionless 文件不会识别。

### P2-2：metadata 读取与 scene 能力不对称

STEP reader 打开 LayerMode/MatMode，scene 目前不填 `Node::layer`，也不提取
`XCAFDoc_VisMaterial`、alpha、PMI/validation properties。应避免 UI 显示“load metadata”让用户
误以为全部保留；为每类 metadata 输出 imported/dropped 诊断。

### P2-3：可见性、bounds 和 selection semantics 未集中定义

Sidebar 递归修改 visible；renderer 只检查当前 node 的 visible，不检查 ancestor；world_bounds
仍包含 hidden nodes。Solo 后 Fit 会把隐藏模型也算进去。将 effective visibility、visible bounds、
selection bounds 放进 scene query/service，避免每个 UI 操作自己递归。

### P2-4：构建 warning baseline 被第三方 OCCT 头污染

Debug build通过，但出现 OCCT header 的 `-Woverloaded-virtual/-Wpedantic` warning。first-party
warning 可能被噪声淹没。将 OCCT include 作为 SYSTEM 或 target-local 抑制第三方 warning，
保持 Cadly 自己的 `Cadly::Warnings` 严格。

### P2-5：设置没有 schema version/migration

UI 和 app 分别直接构造同一个 QSettings handle，key 分散在 MainWindow、Settings、RecentFiles。
控件删除/重命名后旧 key 永久存在。集中 settings schema、版本和 migration，并测试旧版本
配置升级。

## 功能缺口

这些不是代码 bug，但直接阻断“CAD 查看器”核心任务：

| 功能 | 当前证据 | 建议依赖顺序 |
| --- | --- | --- |
| 3D picking/高亮 | `IRenderer::pick` stub | 第一优先；是测量/截面/标注基础 |
| 测量 | 无 edge/face query 或 UI | picking -> double geometry query -> result persistence |
| 剖切/盒裁剪 | shader/RenderTypes 无 clipping plane | RenderSettings -> cap/stencil -> UI manipulator |
| Fit selection/isolate restore | 只有 whole scene Fit、可见眼睛 | selection/bounds service 后实现 |
| 爆炸视图 | 无 occurrence offset state | 稳定 occurrence id 后实现 |
| PMI/GD&T | STEP XCAF scene 不存 | importer metadata model -> renderer overlay/tree |
| Revision compare | 无 stable provenance/diff | stable id + cache 后实现 |
| 导出审阅结果 | 无 annotation/review model | selection/measurement persistence 后实现 |
| 透明材质 | 单一 opaque PBR pass | material semantics + sorted/OIT pass |
| 大装配 LOD/culling | 每 node/submesh 全画 | draw batching -> culling -> LOD/streaming |

## 测试审查

当前 CTest 包含 scene/cad smoke、popover、toolbar 和 inspector reset。2026-07-18 的
`linux-debug` 构建通过，测试在当时全部通过。覆盖有价值但范围很窄：

- 没有 STEP fixture 的端到端 import assertions；现有 smoke 只对 hammer IGES 做可选实测；
- 没有 hierarchy/instance transform/instance color/face color/layer/unit/Unicode/cancel fixture；
- 没有 malformed/partial/unbounded/tiny-face 后材料映射测试；
- renderer 没有 offscreen image/golden、GL state、draw-call budget、pick 或 context-loss 测试；
- MainWindow 没有 import cancel、close-during-import、多 tab/reimport/failed document 状态测试；
- 没有 release benchmark gate、ASan/UBSan、long-running memory regression。

推荐测试金字塔：

1. 纯 scene/cad unit：tessellation policy、matrix/mirror、unit conversion、identity map。
2. 小型公开 CAD fixtures：每个 fixture 只验证一个语义，二进制/文本来源和许可证记录在清单。
3. importer contract：success/cancel/error、diagnostics、stable IDs、scene checksum。
4. offscreen renderer：固定 Mesa/llvmpipe 或 CI GPU，验证 selection ID、depth、material reference
   pixels 和 draw-call counters；视觉 golden 允许小容差。
5. benchmark 非阻塞 dashboard：8 个当前模型的 P50、RSS、TTFP、draw calls，不以一次波动让 CI
   失败，但对显著回归报警。
6. 20 次循环 import/reimport + tab switch + close 的 memory/cancellation soak。

## 推荐重构顺序

不建议一开始“大重写”。按风险和功能依赖：

1. **正确性修复：** document RAII、cancel/status、单位、颜色/instance style、face mapping、
   `load_hierarchy` no-op。
2. **性能闭环：** 去重复 mesh、material batch/draw calls、GPU upload budget、profile counters、
   release benchmark。
3. **应用职责：** 提取 DocumentSession + ImportCoordinator + SettingsStore，MainWindow 只绑定 UI。
4. **选择基础：** stable occurrence/face ID、ID pass、selection service、树/viewport 同步。
5. **renderer 内部分 pass：** mesh cache/targets/PBR/edge/overlay/postprocess，增加 capabilities。
6. **用户功能：** Fit Selection -> measure -> section -> annotation/review pack。
7. **材质/视觉：** HDR post、studio environment、ground/AO/shadow、XCAF material、transparency。

每一步保持 `scene` 不依赖 Qt/OCCT/GL、`cad` 是唯一 OCCT owner、renderer overlay 不回到
QPainter。这三条现有边界是架构中最有价值的部分。

