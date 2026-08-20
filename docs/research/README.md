# Cadly 产品、导入、渲染与代码调研

本目录是 2026-07-18 针对当时 Cadly 工作树的调研交付物。报告将仓库实测、API 事实、
产品能力声明和建议明确分开；没有修改产品代码。第 4 份已于 2026-08-13 按 commit
`fb56f40` 重写并扩展到 UI 与功能，其余四份仍是 2026-07-18 的原始交付。

## 文档

1. [CAD 模型查看器竞品与用户诉求](01-market-and-user-needs.md)
2. [OCCT 导入性能、商业内核差距与调用优化](02-import-performance-occt-vs-commercial.md)
3. [渲染、材质、光照与 KeyShot 风格](03-rendering-material-lighting-keyshot.md)
4. [架构、UI 与功能改进评审](04-code-architecture-review.md)（2026-08-13 复审，
   含对上一版 finding 的逐条对账）
5. [交互式产品与视觉原型](cadly-inspection-studio.html)

## 最重要的跨报告结论

- 产品核心不是支持更多 toolbar mode，而是“可信导入 -> 选择 -> 隐藏/隔离 -> 测量/剖切
  -> 保存审阅结果”的闭环。
- OCCT 不是全部单线程；当前并行 mesh 有效，但 XCAF Transfer 是主要瓶颈。231 MB STEP
  实测 98.3 秒，其中 ReadFile + Transfer 为 72.3 秒。
- 商业 SDK 的价值常在原生格式、PMI、容错、LOD/缓存和技术支持；没有统一的可信速度倍数，
  应以同一 scene 输出 contract 做试用 A/B。
- 当前 PBR 框架已经存在；视觉差距主要是默认全金属、缺地面/接触阴影/AO、程序化低信息
  环境、无 HDR 后处理和重复暗边。
- 模块名和大方向合理，`scene`/`cad`/`renderer_gl` 的外部依赖边界应保留。XCAF document
  生命周期、取消、颜色/实例样式已在 2026-08 前修复；逐面 draw call、首帧上传（外加
  标签页切换时的全量显存驱逐）、单位元数据、三个大文件的职责聚集仍未处理，且拾取
  链路至今缺席——详见第 4 份的复审。

## 验证记录

调研期间执行：

```bash
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
build/linux-debug/bin/cad_import_cli test_files --profile
taskset -c 0 build/linux-debug/bin/cad_import_cli \
  test_files/RC_Buggy_2_front_suspension.stp --profile
```

详细模型结果、硬件和限制见第二份报告。HTML 是独立设计资产，直接用浏览器打开，不依赖
网络或构建产物。

