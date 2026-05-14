# CAD Viewer Development Plan

## Goal

Build a native C++17 desktop CAD viewer that imports standard CAD exchange
formats, triangulates B-Rep geometry, and renders large industrial models with a
custom real-time renderer.

Primary scope:

- Import STEP and IGES first; keep room for STL, OBJ, PLY, glTF/GLB later.
- Use Open CASCADE Technology (OCCT) for CAD data exchange, B-Rep topology,
  shape healing, metadata, and triangulation.
- Use Qt Widgets for the desktop shell, file dialogs, docking panels, and input.
- Implement the renderer ourselves, starting with OpenGL and adding Vulkan after
  the render abstraction is stable.
- Use CMake, Ninja, and a manifest-based dependency manager.
- Render with a PBR metallic-roughness material model suitable for industrial CAD
  inspection.

## Non-Goals For The First Milestone

- No CAD editing, parametric modeling, or feature tree reconstruction.
- No CAM/CAE mesh quality guarantees; OCCT triangulation is used for
  visualization, not simulation.
- No full product lifecycle metadata editor.
- No renderer feature parity between OpenGL and Vulkan in the first release.
- No cloud collaboration, plugin marketplace, or scripting system in the MVP.

## Recommended Stack

Core language and build:

- C++17.
- CMake 3.24+ with CMakePresets.json.
- Ninja generator.
- vcpkg manifest mode as the default dependency manager.
- Conan 2 can be supported later if package availability becomes a blocker.

Core libraries:

- Qt 6 Widgets, Gui, OpenGLWidgets.
- OCCT via the package manager or a pinned source build.
- Vulkan SDK only when the Vulkan backend starts.
- glm or a small internal math layer for renderer math.
- fmt/spdlog for diagnostics.
- stb_image or KTX tooling for environment maps and material textures.

Initial vcpkg dependencies:

- opencascade
- qtbase with widgets/opengl/vulkan features as needed
- glm
- fmt
- spdlog
- stb

## Architecture

The codebase should be split by ownership boundary, not by UI screen.

Suggested modules:

- app: QApplication entry point, main window wiring, settings, recent files.
- cad: OCCT importers, XDE document handling, shape healing, tessellation.
- scene: renderer-independent scene graph, model hierarchy, materials, bounds.
- renderer: abstract GPU resource model and frame graph.
- renderer_gl: OpenGL implementation.
- renderer_vk: Vulkan implementation, added later.
- ui: Qt widgets, model tree, property panels, import progress, viewport host.
- platform: filesystem, threading, logging, asset paths.
- tests: importer and geometry conversion tests.

Dependency direction:

- ui depends on app, scene, renderer interfaces.
- renderer_gl and renderer_vk depend on renderer interfaces.
- cad depends on OCCT and emits scene data.
- scene must not depend on Qt, OCCT, OpenGL, or Vulkan.

## Data Flow

1. User opens a CAD file from Qt.
2. app dispatches an asynchronous import job.
3. cad chooses an importer by extension and content probe.
4. STEP/IGES is loaded through OCCT.
5. If metadata is needed, import into an XDE/XCAF document.
6. Shape healing and validation diagnostics run.
7. BRepMesh_IncrementalMesh creates per-face triangulation.
8. Faces are traversed and converted into renderer-independent mesh buffers.
9. Materials/colors/names/assembly hierarchy are copied into scene structures.
10. renderer uploads vertex/index/material buffers.
11. viewport renders with camera, environment lighting, selection overlay, and
    optional wireframe/edge overlays.

## CAD Import Design

Importer interface:

```cpp
class ICadImporter {
public:
  virtual ~ICadImporter() = default;
  virtual bool CanRead(const std::filesystem::path& path) const = 0;
  virtual ImportResult Import(const ImportRequest& request,
                              IProgressSink& progress) = 0;
};
```

ImportResult should contain:

- root scene node
- mesh assets
- materials
- source diagnostics
- unit metadata
- model bounding box
- import timing and triangle count

STEP strategy:

- Use STEPCAFControl_Reader for production import because it preserves assembly
  structure, colors, layers, names, and validation properties.
- Fall back to STEPControl_Reader only for a minimal geometry-only path or tests.

IGES strategy:

- Use IGESCAFControl_Reader when colors and names matter.
- Fall back to IGESControl_Reader for basic geometry.

Shape processing:

- Start with conservative shape healing and report failures instead of silently
  changing topology too aggressively.
- Make meshing parameters user-configurable:
  - linear deflection
  - angular deflection
  - relative deflection
  - parallel meshing
  - minimum face size threshold

Mesh extraction:

- Traverse TopoDS_Face.
- Read face triangulation plus TopLoc_Location.
- Apply transforms to positions and normals.
- Respect face orientation when emitting indices.
- Generate missing normals if OCCT data is incomplete.
- Deduplicate vertices only when it does not break face-level normals or material
  assignments.

## Scene Model

Scene data should stay stable across render backends.

Core structures:

- Scene
- Node
- Mesh
- Submesh
- Material
- Camera
- LightEnvironment
- SelectionId
- Axis-aligned bounds

Mesh vertex format for MVP:

- position: float3
- normal: float3
- color/material id: uint32 or normalized color
- optional: tangent float4
- optional: uv float2

CAD-specific metadata:

- source file path
- original shape label/id
- assembly path
- source face id
- name/layer/color
- transform

## Renderer Plan

Start with OpenGL:

- Qt QOpenGLWidget viewport.
- Core profile OpenGL 4.1 or 4.5 depending on target platforms.
- Persistent or dynamic buffer upload path for large CAD files.
- Instanced drawing for repeated assembly parts.
- PBR forward renderer first.
- Deferred renderer only if many lights or heavy screen-space effects become
  necessary.

Render passes:

- depth prepass for large models
- opaque PBR pass
- outline/selection pass
- edge/wire overlay pass
- grid/axis gizmo pass
- tonemapping/gamma pass

PBR MVP:

- metallic-roughness workflow
- base color from CAD color or default industrial palette
- image-based lighting from prefiltered environment
- roughness/metalness presets for steel, aluminum, plastic, rubber, painted metal
- ambient occlusion approximation for visual grounding

Industrial visual style:

- neutral studio HDRI
- matte dark gray background gradient
- cool fill light and warm rim light
- subtle ground plane/shadow
- thin silhouette and selected-edge highlight
- optional exploded-view-ready scene graph

Vulkan phase:

- Do not start Vulkan until the scene model, importer, camera, and OpenGL renderer
  are stable.
- Add a renderer interface first, then implement Vulkan with QVulkanWindow.
- Compile Vulkan shaders to SPIR-V at build time.
- Keep shader source shared where possible, but expect backend-specific binding
  layouts.

## UI Plan

Main window:

- central 3D viewport
- model tree dock
- properties dock
- import diagnostics dock
- toolbar for view modes, clipping, fit, isolate, section, wireframe
- status bar showing triangles, faces, import time, GPU memory estimate

Viewport interactions:

- orbit/pan/zoom
- fit all/fit selection
- selection by part and face
- isolate/hide/show
- section clipping plane
- exploded view after hierarchy is preserved

Import workflow:

- file dialog
- import options dialog
- progress dialog with cancel
- diagnostics report after import
- recent files

## Build And Dependency Management

Repository layout:

```text
Cadly/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  src/
    app/
    cad/
    scene/
    renderer/
    renderer_gl/
    renderer_vk/
    ui/
  shaders/
    glsl/
    spirv/
  assets/
    env/
  tests/
  docs/
```

CMake rules:

- One target per module.
- No global include_directories or link_directories.
- Use target_compile_features(... cxx_std_17).
- Use Qt imported targets.
- Keep OCCT toolkit links isolated in the cad target.
- Add install rules early so packaging is not an afterthought.
- Use CTest for importer tests.

Preset strategy:

- linux-debug
- linux-release
- windows-msvc-debug
- windows-msvc-release
- optional: vulkan-debug

## Milestones

Milestone 0: project skeleton

- CMakePresets.json, vcpkg.json, root CMakeLists.txt.
- Qt main window with empty viewport.
- Logging and settings.
- CI build command documented.

Milestone 1: OCCT import spike

- Load STEP and IGES from command line or minimal UI.
- Convert to TopoDS_Shape or XDE document.
- Mesh with BRepMesh_IncrementalMesh.
- Print counts: shapes, faces, triangles, bounds, import time.
- Add tests using small sample files.

Milestone 2: CPU scene conversion

- Convert OCCT triangulation to scene Mesh/Submesh.
- Preserve transform, material color, and hierarchy where available.
- Build model tree data.
- Add import diagnostics.

Milestone 3: OpenGL viewport MVP

- QOpenGLWidget viewport.
- Camera controls.
- Upload and draw imported meshes.
- Depth test, culling, basic material color.
- Fit-to-model and model bounds.

Milestone 4: industrial PBR

- Metallic-roughness shader.
- Environment lighting.
- Tonemapping and gamma correction.
- Material presets.
- Edge/selection overlay.

Milestone 5: UX and performance

- Async import with cancel.
- Large model progress and memory reporting.
- Mesh cache and lazy GPU upload.
- Instancing repeated parts.
- Picking and isolate/hide/show.

Milestone 6: Vulkan backend

- Renderer interface freeze.
- QVulkanWindow host.
- Swapchain, pipelines, descriptor layout, buffers.
- SPIR-V shader compilation.
- Feature parity with OpenGL MVP, not full PBR parity initially.

Milestone 7: packaging

- Install layout for app, plugins, shaders, assets, Qt runtime, OCCT runtime.
- Windows and Linux packages.
- Smoke tests on clean machines.

## Key Risks

- CAD import quality varies by source system; diagnostics and sample corpus are
  required early.
- OCCT triangulation is good for visualization but not a simulation-quality mesh.
- STEP/IGES metadata preservation requires XDE; basic readers are not enough for
  real assembly UX.
- Large assemblies need instancing and async loading, otherwise UI latency will be
  unacceptable.
- Vulkan is a schedule risk; OpenGL should be the MVP renderer.
- Runtime deployment of Qt and OCCT shared libraries must be handled early.
- CAD file licensing/privacy means test assets need a curated public sample set.

## First Implementation Batch

1. Create the CMake/vcpkg/Qt skeleton.
2. Add a minimal Qt main window and OpenGL viewport.
3. Add cad_import_cli to validate OCCT import and triangulation without UI.
4. Add scene data structures and conversion from OCCT faces.
5. Render imported mesh with flat color.
6. Add PBR shader path once geometry and camera are stable.
