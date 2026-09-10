# 剖切旋转闪烁的定位与修复

2026-09-10，使用公共样例 `test_files/as1-ug-214.stp` 复现。

## 问题来源

截图中的主要冲突发生在底板与两个支架的接触面。`PLATE` 的 Z 范围为
0–20，两个 `L_BRACKET` 的底面也位于 Z=20。剖切穿过底板后，底板的背面
开始可见，与支架的正面处在同一深度。两侧三角化不同，尤其孔周围有细长
三角形，深度插值和 polygon offset 的舍入误差会随观察角度变化，导致两种
材质交替覆盖，表现为交错条纹和旋转闪烁。半透明剖切填充让这种冲突显现出来。

这不是 `05f013f` STEP 性能优化提交引入的。该提交没有修改 renderer、
renderer_gl 或 shaders；使用其父提交 `b59544c` 的渲染器也能复现相同条纹。
`b59544c` 开始显示被剖切打开的实体背面，从而暴露了这对接触面的冲突。

另外发现一处独立问题：`f4e8a8b` 将半透明填充移到边线之后绘制，但没有
恢复模型表面使用的深度偏移。当剖切面与已有表面重合时，旋转可能让填充
错误地覆盖该表面。这一问题也早于性能优化提交。

## 修复方式

- 被剖切打开的零件分别绘制背面和正面。背面额外使用 `factor=0.25`、
  `units=4` 的 polygon offset，让贴合处的正面材质稳定优先。
  随斜率变化的偏移用于覆盖细长三角形的舍入差异；只增加固定 units
  在真实样例的斜视角度下仍会留下条纹。
- 半透明填充恢复与模型表面一致的 polygon offset，绘制结束后关闭。
- 空腔、背面遮挡、镜像实例、选择和隔离半透明逻辑继续保留。

修改仅位于 Cadly 的 OpenGL 绘制阶段，没有修改 OCCT、模型顶点或拓扑。
剖切打开的零件会增加一次每子网格的绘制调用；分开剔除正反面的方式保留
GPU 的提前深度测试。普通未剖切浏览和文件导入没有新增绘制或转换步骤。

## 验证

[`section_render_test.cpp`](../tests/section_render_test.cpp) 增加不经过 OCCT
的像素回归测试：已有表面遮住重合填充、相接实体的正面稳定优先，以及
细长三角形和偏离原点的实例。覆盖正交／透视、MSAA 0／4、边线开关、
镜像、绘制顺序和多个旋转角度，同时保留原有空腔和遮挡测试。

```sh
cmake --build --preset linux-debug -j 2
build/linux-debug/tests/cadly_section_render_test -platform xcb
ctest --preset linux-debug
cmake --build --preset linux-release -j 2
build/linux-release/tests/cadly_section_render_test -platform xcb
ctest --preset linux-release
git diff --check
```

定位阶段在包含性能提交的工作区完成了 Debug 17 项、Release 18 项 CTest，
全部通过；两个构建的显式桌面 OpenGL 像素测试也通过。将修复移至以
`b59544c` 为基点的独立 worktree 后，又分别配置 Debug／Release、构建
`cadly_section_render_test` 并用 `-platform xcb` 运行，均通过，没有新增
编译警告。图像验证使用 Mesa 25.2.8。

真实样例固定 Z=2 剖切、保留 Z≥2 一侧，分别在 0°、10°、35°、60°
观察角度检查每帧 48 个接触面采样点：修复后，改变背面材质不会污染这些
正面采样点。下图固定同一模型、剖切参数和相机，左侧使用性能优化之前的
渲染器，右侧使用修复后的渲染器。

![剖切接触面的修复前后对照](images/section-depth-stability.png)
