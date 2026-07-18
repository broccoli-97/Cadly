# OCCT 导入性能、商业内核差距与调用优化调研

**调研日期：** 2026-07-18  
**当前被测版本：** Cadly `6b85a4e`，OCCT 7.6.3，Debug preset  
**硬件/环境：** AMD Ryzen 5 7500F，6 核 12 线程，15 GiB RAM，WSL2/Linux x86-64

## 结论先行

1. **“OCCT 导入只能单线程”不准确。** 当前 Cadly 已把
   `BRepMesh_IncrementalMesh` 的并行参数设为 `true`，面网格化确实会使用多个线程。
2. **STEP/IGES 的 ReadFile 和 XCAF Transfer 在当前 API 路径没有每文件并行开关。** 实测总进程
   平均只有 147% CPU，原因是占大头的解析/转换阶段基本串行，只有网格阶段出现多核收益。
3. **当前最大 231 MB STEP 的 98.3 秒中，ReadFile 18.8 秒，XCAF Transfer 53.5 秒，Cadly
   网格/抽取 25.9 秒。** 即使把网格阶段变成零耗时，理论总加速也只有 `98.3 / 72.3 = 1.36x`。
4. **商业 SDK 没有可辩护的统一“快 X 倍”答案。** 原生格式访问、预生成 JT/PRC LOD、缓存和
   按需加载可能带来数量级的首屏优势；同一个 STEP、同一几何容差、同样保留 XCAF/PMI 时，
   必须用试用 SDK A/B。厂商宣传页不能替代基准。
5. **调用侧仍有高价值改进。** 当前 mesher 构造函数已经自动 `Perform`，代码又显式调用一次；
   整文档批量网格后每个 shape 又跑安全网格；三级解析边线、串行 mesh extraction、逐面
   submesh 和首帧同步 GPU 上传也消耗明显。先解决这些，再讨论换内核。

## 导入流水线不是一个阶段

```text
磁盘 I/O / 文本解码
        |
STEP/IGES entity graph (ReadFile)
        |
B-Rep + assembly + XCAF metadata (Transfer)
        |
whole-document triangulation [可并行]
        |
assembly walk + per-unique-shape mesh/edge extraction [当前串行]
        |
scene DTO + GPU upload [GPU upload 当前发生在首个 paintGL]
```

只观察任务管理器里“一个进程”无法判断内部是否并行；同样，只打开
`parallel_meshing` 也不会让前两段自动并行。

## API 证据

仓库当前调用在
[OcctShapeToMesh.cpp](../../src/cad/src/OcctShapeToMesh.cpp#L62)：

```cpp
BRepMesh_IncrementalMesh mesher(shape,
                                opts.linear_deflection,
                                opts.relative_deflection,
                                opts.angular_deflection,
                                opts.parallel_meshing);
mesher.Perform();
```

本机 OCCT 7.6.3 头文件对该构造函数的说明是：`isInParallel` 为真时 shape 将被并行网格化；
同时也明确写了构造函数 **Automatically calls method Perform**。所以：

- `opts.parallel_meshing=true` 有效，不是 UI 假开关；
- 紧接着的 `mesher.Perform()` 是第二次调用，通常会复用已有 triangulation，但仍有遍历和状态
  检查成本，应先做 A/B 后移除；
- `STEPCAFControl_Reader::ReadFile` 和 `Transfer` 只接收文件名/`Message_ProgressRange`，没有
  `SetRunParallel` 或线程数参数；不能通过一个调用开关把 STEP Transfer 变成多线程；
- `TransferOneRoot`/`TransferRoots` 共享 work session 和 transfer cache。不能在同一个 reader 上
  粗暴地从多个线程调用 `TransferOneRoot`；这不是 API 保证的并行方式。

OCCT 官方文档入口：

- [OCCT Data Exchange User Guide](https://dev.opencascade.org/doc/overview/html/occt_user_guides__data_exchange.html)
- [OCCT Mesh User Guide](https://dev.opencascade.org/doc/overview/html/occt_user_guides__mesh.html)
- [BRepMesh_IncrementalMesh class reference](https://dev.opencascade.org/doc/refman/html/class_b_rep_mesh___incremental_mesh.html)
- [STEPCAFControl_Reader class reference](https://dev.opencascade.org/doc/refman/html/class_s_t_e_p_c_a_f_control___reader.html)

## Cadly 实测

### 测试方法

```bash
cmake --build --preset linux-debug
build/linux-debug/bin/cad_import_cli test_files --profile
taskset -c 0 build/linux-debug/bin/cad_import_cli \
  test_files/RC_Buggy_2_front_suspension.stp --profile
build/linux-debug/bin/cad_import_cli \
  test_files/RC_Buggy_2_front_suspension.stp --profile
```

所有模型使用同一 visual-relative tessellation 默认值。数据是本机工程诊断，不是跨软件排名；
Debug build 会放大 C++ 抽取成本，但不会改变 XCAF Transfer 占比很高这一结论。

### 全模型结果

| 文件 | 大小 | ReadFile | XCAF Transfer | mesh/scene | 总计 | faces | triangles |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `3864470050F1.stp` | 231 MB | 18.797 s | 53.533 s | 25.924 s | 98.256 s | 60,185 | 3,264,211 |
| `KR600_R2830-4.stp` | 12.6 MB | 1.353 s | 2.906 s | 1.424 s | 5.708 s | 4,113 | 189,095 |
| `RC_Buggy_2_front_suspension.stp` | 15.3 MB | 1.642 s | 7.381 s | 2.000 s | 11.040 s | 5,742 | 414,758 |
| `Ventilator.stp` | 2.3 MB | 0.266 s | 0.487 s | 0.138 s | 0.910 s | 305 | 41,238 |
| `as1-ug-214.stp` | 73 KB | 0.008 s | 0.020 s | 0.020 s | 0.064 s | 39 | 2,096 |
| `bearing.iges` | 1.3 MB | 0.022 s | 0.097 s | 0.050 s | 0.172 s | 213 | 5,730 |
| `hammer.iges` | 1.0 MB | 0.009 s | 0.037 s | 0.039 s | 0.088 s | 45 | 11,899 |
| `linkrods.step` | 1.8 MB | 0.142 s | 0.212 s | 0.020 s | 0.376 s | 37 | 5,522 |

整个批次 wall time 118.76 秒、user time 168.20 秒、CPU 147%、峰值 RSS 3.45 GB。
这直接证明进程使用过多核，但大部分时间并没有占满 12 线程。

### 单核/全核对照

在文件缓存热身后的同一 RC Buggy 模型：

| 模式 | ReadFile + Transfer | mesh/scene | 总计 | CPU |
| --- | ---: | ---: | ---: | ---: |
| 固定单核 `taskset -c 0` | 7.796 s | 4.354 s | 12.151 s | 99% |
| 正常 12 逻辑线程 | 7.762 s | 2.036 s | 9.799 s | 155% |

网格/scene 段加速约 `2.14x`，整段导入只加速约 `1.24x`。解析时间几乎不变，符合 API
结构。并行效率没有接近 12x，因为 mesh/scene 段还包含串行装配遍历、边线采样、buffer
组装和每 shape 的安全 meshing。

### 关闭颜色和名称不是快速路径

同一模型、全核热缓存：

- 默认：9.799 秒；
- `--no-colors --no-names`：9.868 秒。

差异在噪声内。原因是代码仍然使用 `STEPCAFControl_Reader` 和 XCAF document；只是关闭了
部分属性读取，并没有切换到 `STEPControl_Reader` geometry-only path。若产品要提供“快速
几何预览”，必须把它定义成明确的有损模式，并说明会丢装配/颜色/PMI，而不是把两个复选框
包装成性能选项。

### 大模型内部阶段

231 MB STEP 的 `document_to_scene=25.924 s` 中：

| 子阶段 | 时间 | 说明 |
| --- | ---: | --- |
| batch document tessellation | 11.112 s | 并行价值最高 |
| assembly walk total | 14.810 s | 串行递归和 mesh 构建，部分与下项重叠 |
| shape conversion total | 7.575 s | 每 unique shape 的重复检查/抽取 |
| shape safety tessellation | 4.454 s | 整文档 mesh 后仍逐 shape 调 mesher |
| analytical edge LOD total | 1.816 s | 每条 BRep edge 采 3 档 LOD |
| face topology walk | 1.284 s | 三角形、normal、submesh、edge strips |

这说明调用侧仍有几秒到十几秒的优化空间，不能把 25.9 秒全部归咎于 OCCT parser。

## 商业内核/翻译 SDK 为什么可能更快

“商业内核”需拆成三类：

1. **原生几何内核：** Siemens Parasolid、Dassault Systèmes CGM、Spatial ACIS。读取自身原生
   B-Rep 时可避免 STEP entity 到另一套拓扑的完整重建；但读取 STEP 仍然需要 translator。
2. **多 CAD 翻译 SDK：** HOOPS Exchange、Spatial 3D InterOp、Datakit、CAD Exchanger SDK、
   C3D Converter。优势通常是原生格式覆盖、持续维护的格式版本、PMI/语义、修复策略和
   工程化缓存，不等于每个 STEP 都更快。
3. **轻量可视化格式：** JT、PRC、3DXML、glTF 等可能已经包含 tessellation/LOD。查看器可以
   跳过 B-Rep 重建或只在需要测量时加载精确几何，TTFP 差距可能远大于内核算法差距。

商业方案常见优势来源：

- 二进制/原生格式解析，不需要解析巨大的 STEP 文本实体图；
- 按产品结构、可见性或 bbox 先返回装配树，几何按需加载；
- 预生成多档 tessellation、持久缓存和相同零件去重；
- 针对厂商格式的容错、PMI/颜色/图层直接映射，减少二次修复；
- 更成熟的进度、取消、内存预算、进程隔离和批量转换服务；
- 与目标内核相同的数据表示，减少 topology rebuild/healing。

但以下说法不能在没有 A/B 时成立：

- “Parasolid 导入任何 STEP 都比 OCCT 快 N 倍”；
- “付费 SDK 一定会多核解析单个 STEP”；
- “文件加载完成”就是同一完成定义；有些产品只完成了预览 LOD；
- “三角形数量相同”就是几何精度相同；还需对 chord/angle、seams、normals、PMI、颜色和
  healing 结果。

官方产品入口：

- [Siemens Parasolid](https://plm.sw.siemens.com/en-US/parasolid/)
- [HOOPS Exchange](https://www.techsoft3d.com/products/hoops/exchange)
- [Spatial 3D InterOp](https://www.spatial.com/products/3d-interop)
- [Datakit CrossCad/Ware](https://www.datakit.com/)
- [CAD Exchanger SDK](https://cadexchanger.com/products/sdk/)
- [C3D Toolkit](https://c3dlabs.com/en/products/c3d-toolkit/)

## 调用侧改进清单

以下是建议，不是本次代码修改；优先级同时考虑收益、风险和可验证性。

### P0：低风险、先量化

1. **消除重复 `Perform`。** 使用自动执行的构造函数时不再显式调用；或改用参数对象和一次
   `Perform(progressRange)`。逐模型对比 mesh checksum、失败面和时间。
2. **关闭 OCAF document。** `NewDocument` 后必须在成功、失败、取消所有路径调用
   `TDocStd_Application::Close(doc)`，最好 RAII。当前同进程连续 1 次 RC 模型峰值约 426 MB，
   3 次约 633 MB；allocator cache 会影响数字，但缺失 `Close` 本身已由 API 生命周期证实。
3. **把 OCCT progress 接入取消。** `STEPCAFControl_Reader::Transfer` 和 mesher 都接受
   `Message_ProgressRange`；用 `Message_ProgressIndicator` 适配 atomic cancel。当前 231 MB
   Transfer 单段 53.5 秒，点击取消可能长时间没有响应。
4. **修正进度权重。** Read 0-20%、Transfer 20-70%、batch mesh 70-85%、extract/upload
   85-100%，使用阶段历史 EMA；assembly loop 不要把 45% 进度重置到 `i/N`。
5. **Release 基准和可重复结果。** 同一 commit、冷/热缓存各 5 次，固定 CPU governor，记录
   P50/P95、峰值 RSS、输出 mesh hash 和失败诊断。

### P1：减少重复工作和首屏时间

1. **整文档 mesh 后只处理缺失/精度不足的 face。** 不再对每个 shape 无条件调用安全 mesher；
   先检查 triangulation 和 deflection。大模型的 `shape safety tessellation` 当前为 4.45 秒。
2. **边线渐进生成。** 首屏只生成 mesh-coupled visible edges 或一档 BRep edge；fine/ultra LOD
   后台生成。当前三级解析边在大模型上 1.82 秒，并增加 CPU/GPU 内存。
3. **并行 unique-shape extraction。** XCAF assembly walk 先生成稳定 job list；每个 job 只读已经
   tessellate 的独立 shape，输出局部 Mesh/Stats；主线程按 label 顺序合并。不要让 worker 修改
   共享 OCCT document、material cache 或 scene vectors。
4. **预估并 `reserve`。** 对 triangles/nodes/edge points 做一次轻量计数，减少几百万 Vertex 的
   vector 重分配；评估计数遍历本身是否抵消收益。
5. **首屏与完整完成分离。** 低精度 surface 可交互后就发布 scene；边、PMI、高精度 LOD 和
   GPU 上传继续后台完成。产品状态必须写清“Preview”而不是假报完成。
6. **GPU upload 迁出首个可见 paint。** 使用有预算的每帧 upload queue 或共享 context worker；
   每帧限制毫秒/字节并优先上传可见节点。

### P1：避免渲染端放大导入数据成本

当前每个 CAD face 生成一个 `Submesh`，PBR pass 对每个 submesh 调一次 `glDrawElements`。
231 MB 样本 60,185 faces，最坏接近 60k surface draw calls，尚未计算 edge passes。

建议：

- 按 `(mesh, material)` 重排/合并 index range；颜色已可作为 vertex attribute；
- face ID 放独立整数 attribute/SSBO 或 selection pass，避免为了 future picking 保留每面 draw；
- 重复 prototype 使用 instanced draw；
- 加 frustum/size culling 和 surface LOD；
- 在 benchmark 中独立记录 `draw calls / uploaded bytes / first-frame CPU/GPU time`。

### P2：缓存和多进程

- 缓存 key 至少包含 `file content hash + importer version + OCCT version + tessellation options +
  metadata mode`，原子写入并校验 checksum；不能只看 mtime。
- 缓存 scene tree、materials、mesh/edge LOD 和 diagnostics；支持先映射目录，再按需读取 buffer。
- 多文件批处理可用独立进程并发，每进程一个 OCCT/XCAF session。OCCT data-exchange 使用
  `Interface_Static` 等进程级状态；在同一进程并发多个 reader 前必须针对所用版本做线程安全
  审计。进程池也能隔离崩溃和限制单任务 RSS。
- 客户长期重复查看同一模型时，缓存通常比替换 parser 更能改善 TTFP。

## 商业 SDK 采购基准协议

要求每个供应商用完全相同的 20-50 个客户样本和输出契约，至少包含：

| 维度 | 要求 |
| --- | --- |
| 文件 | STEP AP203/214/242、IGES、目标原生格式；小零件、重复装配、坏面、Unicode、外部引用 |
| 输出 | hierarchy/name/color/layer/PMI/units、相同 chord+angle、相同 double-sided 规则 |
| 时间 | process cold start、TTFP、full B-Rep、full tessellation、GPU-ready；P50/P95 |
| 资源 | wall/user/system、线程曲线、峰值 RSS、输出 CPU/GPU bytes |
| 可信 | shape/face count、bbox、volume（适用时）、颜色/实例/PMI diff、失败诊断 |
| 取消 | Read/Transfer/tessellate 各阶段取消延迟和资源回收 |
| 集成 | Linux/Windows、静态/动态链接、部署体积、C++ ABI、升级周期、技术支持 |
| 商务 | 年费/版税、离线许可、容器/CI、云服务限制、客户文件上传条款 |

先做两周 trial spike，通过 adapter 输出同一个 `scene::Scene`。不要为了 demo 把商业 SDK
直接耦合到 UI 或 renderer。

## 推荐决策

- **短期不因“单线程”直接换内核。** 当前数据证明的是 XCAF Transfer 占比高，而不是 OCCT
  全部单线程；调用侧还有重复 meshing、取消、抽取、draw-call 和缓存问题。
- **把升级 OCCT 与商业 SDK 放进同一 benchmark。** 先用最新可支持的 OCCT release build
  重跑，再评估 1-2 个 SDK；输出契约必须一致。
- **若客户核心是原生 CATIA/NX/Creo/SolidWorks、PMI 和坏文件容错，商业 translator 的价值
  主要是兼容性/语义/支持，不只速度。**
- **若核心是 STEP/IGES 离线检查，先完成上述 P0/P1 和持久缓存，性价比更高。**

