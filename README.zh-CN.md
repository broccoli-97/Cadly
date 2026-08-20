# Cadly

> 一个简单的 CAD 模型导入查看器 —— 打开 STEP/IGES 文件并查看模型。

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

[English](README.md)

Cadly 是一个用 C++17 编写的原生桌面 CAD **查看器**（不是编辑器）。它通过 Open
CASCADE（OCCT）导入 STEP/IGES，对 B-Rep 做三角化，再交给手写的实时渲染器绘制
（当前为 OpenGL 4.1 core，接口预留了后续接入 Vulkan 后端的空间）。界面外壳基于
Qt 6 Widgets。材质采用 PBR 金属度-粗糙度模型配合基于图像的光照（IBL），针对工业
检视场景做了调校。

## 功能

- **导入** STEP 与 IGES：使用 OCCT CAF 读取器保留颜色、名称与装配层级，并提供
  仅几何的回退读取器。
- **非模态导入**：导入在工作线程中进行，进度与取消位于文档标签页内，导入期间
  原有场景仍可交互。
- **显示模式**：着色、着色+边线、消隐线（可选暗显被遮挡线）、带解析式 LOD 边线
  层级的线框，以及三角网格调试叠加。线框/消隐模式的视点相关轮廓线由 GPU 逐帧
  生成。
- **CAD 式导航**：右键拖拽旋转、中键拖拽平移、滚轮以光标为锚点缩放，默认正交
  投影，`1`–`7` 切换标准视图。
- **装配树**：单击高亮部件，双击隔离（其余部件半透明虚化或直接隐藏）。
- **多标签文档**、带持久化导入选项（网格偏差、修复、焊接）的属性检查器、
  深色/浅色主题以及无边框专注模式。
- **多语言界面**：英文与简体中文。
- **命令行工具** `cad_import_cli`：无需显示设备即可验证导入结果。

## 构建与运行

使用 CMake preset（Ninja）。Linux 下依赖系统的 Qt 6 与 OCCT；Windows/便携构建
使用 vcpkg 清单（`vcpkg.json`）。

```bash
cmake --preset linux-release          # 配置（RelWithDebInfo）
cmake --build --preset linux-release  # 构建 -> build/linux-release/bin/
ctest   --preset linux-release        # 冒烟测试

build/linux-release/bin/cadly [file.step]        # 图形界面，可选在启动时打开文件
build/linux-release/bin/cad_import_cli file.step # 无界面导入，打印几何统计
```

其他 preset：`linux-debug`、`linux-qt68-{debug,release}`（Qt 6.8，启用 qlementine
样式）、`linux-vcpkg-debug`、`windows-msvc-{debug,release}`（VS 解决方案）、
`windows-ninja-{debug,release}`（单配置 Ninja，需要环境中已有 MSVC）。CI 在
Linux 与 Windows 上完成构建、测试与打包。

### 依赖

- 支持 C++17 的编译器、CMake ≥ 3.24、Ninja
- Qt 6（Widgets、OpenGL、Concurrent、Svg；LinguistTools 可选）
- Open CASCADE 7.6 及以上
- glm、spdlog、fmt
- 支持 OpenGL 4.1 core 的显卡与驱动

## 架构

`src/` 下每个模块对应一个 CMake target，导出为 `Cadly::<Name>`：

| 模块 | Target | 职责 |
|---|---|---|
| `platform` | `Cadly::Platform` | 日志、资源与路径查找 |
| `scene` | `Cadly::Scene` | 规范场景模型 —— 仅依赖 glm，不含 Qt/OCCT/GL |
| `renderer` | `Cadly::Renderer` | `IRenderer` / `RenderTypes` 接口 |
| `renderer_gl` | `Cadly::RendererGL` | OpenGL 4.1 后端，不依赖 Qt |
| `cad` | `Cadly::Cad` | 导入器 —— 唯一链接 OCCT 的 target |
| `ui` | `Cadly::Ui` | 控件与视口宿主 |
| `app` | `cadly` | 程序入口、设置、最近文件 |

数据流：文件 → `ImporterRegistry::select()` → OCCT STEP/IGES 导入器 →
`BRepMesh_IncrementalMesh` → `scene::Mesh` → `scene::Scene` →
`IRenderer::attach_scene()` → `render()`。

`docs/cad-viewer-plan.md` 是最初的设计方案与里程碑规划，`docs/ui-redesign/`
记录界面外壳的布局设计，`CLAUDE.md` 说明修改代码时需要保持的架构约束。

## 许可证

本项目基于 Apache License 2.0 发布，详见 [LICENSE](LICENSE)。

Cadly 链接了 Open CASCADE（LGPL-2.1 及其例外条款）与 Qt 6（LGPL-3.0），这些
组件仍遵循各自的许可证。
