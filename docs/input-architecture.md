# Input and navigation boundaries

## Why this split

Upstream `fa354d7` introduced five mouse-binding presets, `173a32c` added
free/turntable orbit, and `889d93d` exposed them in Preferences. Their behavior
is retained, but their original dependency direction made each preference
travel through the editor, MainWindow, ViewportWidget, and CameraController.
Bindings and serialization also depended on types nested inside the camera
controller, while late-modifier promotion shared a mouse handler with section
manipulation.

The new boundary is a resolved **navigation command**, not another layer of
per-setting forwarding methods. A camera executor never sees a preset, mouse
button, modifier, sensitivity, or settings key.

## Ownership and build targets

| Target | Owns | Dependencies |
| --- | --- | --- |
| `Cadly::Input` | Preset registry, stable tokens, typed preferences, pointer ownership, motion conversion, command types | C++17 standard library only |
| `Cadly::InputQt` | Qt event normalization, native/translated labels, observable runtime preferences | Input, Qt Core/Gui; no widgets or camera |
| `Cadly::AppSettings` | INI read/write and compatibility of persisted preferences | Input, Qt Core; no UI or renderer |
| `Cadly::CameraController` | Pivot resolution, execution of camera-space motion, zoom/clip policy | Command header, Scene, Qt Core; no GUI/input-event APIs |
| `Cadly::Ui` | Widget lifetime, tool hit testing/manipulation, selection, input dispatch | The above adapters and the existing rendering/CAD modules |

The camera controller keeps its existing public include path, but builds as a
separate library so its tests cannot acquire an accidental widget/renderer
dependency. `scene` and `renderer_gl` retain their original dependency rules.

```text
Qt pointer/wheel events
  → QtInputAdapter
  → PointerRouter
       → NavigationCommand → CameraController → scene::Camera
       → Tool / Selection  → viewport tool geometry / selection

app::Settings ── load ── InputPreferences ── changed ── viewport's PointerRouter
      ▲                        ▲
      └── persist changes ─────┤
                               └── PreferencesDialog edits / observes
```

`main.cpp` owns the configuration and persistence connection. MainWindow only
subscribes the viewport to a complete snapshot and gives the editor that same
configuration. It neither enumerates navigation fields nor saves them when
saving display/layout settings. There is no process-wide settings singleton;
tests and embedded hosts can supply isolated instances.

`NavigationCommand.h` deliberately does not include the preferences or preset
registry. Orbit commands carry radians, pan commands carry fractions of camera
distance, and zoom commands carry a distance multiplier plus an optional cursor
anchor (a distinct command type). The old motion coefficients and camera math
are unchanged. Preset bindings and the help legend use the same registry; it
is constructed once, including the shared trackpad rows.
Wheel conversion also passes through the router's configuration boundary;
the Qt adapter only extracts coordinates and deltas, never applies policy.

## Interaction contract

- On press: an exact navigation binding wins; otherwise a left press can be
  claimed by a tool; otherwise it is selection. Selection still happens on
  **press**, preserving the existing background-click signal contract.
- A tool owns its whole gesture. Late Alt/Primary flags cannot turn a section
  translation or rotation into a camera gesture. Shift snapping stays tool-side.
- Unclaimed/synthesized drags can acquire late modifiers, including when the
  host did not deliver the press. The promotion position becomes the anchor;
  earlier movement is not replayed as a jump.
- Once navigation starts, its initiating button, action, and preference
  snapshot remain fixed. Changing modifiers/preferences affects the next
  gesture, not the current one. This also prevents changing a preset while
  dragging from reinterpreting already accumulated movement.
- Additional presses/releases do not steal or terminate an owned drag. Losing
  the initiating button ends it. Remaining held buttons cannot immediately
  restart navigation through late promotion.
- Focus loss, window deactivation, mouse ungrab, hiding, or document replacement
  cancels input. Hiding/disabling a section tool cancels its capture too. Cancel
  is idempotent; stale held-button moves cannot restart a cancelled gesture.
  All-buttons-up or a fresh single-button press enables input again.

The router knows only whether a tool hit exists. It does not know section
planes, renderer types, hit-test geometry, or world coordinates. The viewport
executes the selected tool's begin/update/end operations.

## Platform and settings compatibility

Modifiers are **logical shortcut modifiers**. `Primary` maps directly to
`Qt::ControlModifier` (Command on macOS, Control on Linux/Windows); `Secondary`
maps to `Qt::MetaModifier` (physical Control on macOS). Qt has already performed
the platform mapping; the adapter must not swap these again. Unsupported flags
are retained as `Other` so exact matching cannot silently ignore an extra key.
Native glyphs still come from `QKeySequence::NativeText`; translation contexts
and strings remain unchanged.

Mouse positions remain logical widget pixels, with no device-pixel multiplier.
The section hit-test still performs its own renderer/logical-pixel conversion.
Wheel zoom retains the existing vertical `angleDelta` policy, including small
trackpad deltas. The adapter neither adds `pixelDelta` a second time nor
reverses the sign again for `inverted()`. Pixel-only scrolling and new gestures
are intentionally not introduced by this refactor.

| Existing INI key | Tokens | Default |
| --- | --- | --- |
| `display/navigation_scheme` | `cadly`, `blender`, `rhino`, `fusion360`, `maya` | `cadly` |
| `display/orbit_style` | `free`, `turntable` | `free` |

The store remains `QSettings::IniFormat`, `UserScope`, organization/application
`Cadly`/`Cadly` on all platforms. In particular, the orbit token/path used by
external consumers such as Quick Look does not move. Reading missing/unknown
values returns the historical defaults without writing or migrating anything.
Editing writes only changed fields; an unknown token in an untouched field and
all unrelated keys survive. General, display, section-style, import, recents,
and window/layout settings retain their existing paths and owners.

## Extending it

- New preset: add its identifier and registry entry in Input, plus independent
  expected bindings in the compatibility test. The combo and legend enumerate
  the registry; camera, viewport event handlers, and persistence need no new
  switch or forwarding method.
- New sensitivity/inversion preference: add the typed field/default and its
  equality/validation, persist it in AppSettings, add its editor control, and
  consume it in Input's motion conversion. Editor callbacks copy the current
  complete configuration before changing their field. MainWindow, the viewport
  subscription, and camera execution do not change.
- New camera algorithm: implement the math and a semantic command where needed;
  that is a genuine new camera capability, not a button-mapping concern.
- New tool: keep hit testing and geometry in the host/tool module, reusing the
  router's ownership contract. Do not add per-preset conditions to tool handlers.

This is not a general rewrite of application settings or a user-programmable
binding system. Display/layout persistence and normal QAction shortcuts are
outside this change.

## Regression gates

- `cadly_navigation_scheme`: independent legacy binding fixture; exhaustive
  supported/extra modifier combinations, registry uniqueness, token/default
  behavior, and exact motion coefficients.
- `cadly_pointer_router`: every preset/binding/orbit style; capture, promotion,
  preference snapshots, extra buttons, missing releases, cancellation, and
  independent viewport state.
- `cadly_qt_input_adapter`: Qt flag matrix including Command vs physical Control,
  logical positions, native legend glyphs, natural-scroll metadata, high-resolution
  angular deltas, and atomic/idempotent configuration notifications.
- `cadly_input_settings`: isolated INI fixtures, every old token combination,
  reload/round trips, missing/unknown values, unrelated/forward-compatible keys,
  original general settings, and independent stores.
- `cadly_camera_controller`: projection-specific zoom safety, cursor anchoring,
  rays/projection, pan/dolly, pivot lifetime, free/turntable behavior, view/restore
  state, and invalid/no-op commands. It needs only Qt Core, not a GL context.
- `cadly_viewport_input` and `_hidpi`: actual Qt event dispatch across all
  bindings, both orbit styles/projections, section on/off, interruption, extra
  buttons, document changes, section rotation/snapping, and wheel anchoring;
  repeated at device scale 2.
- `cadly_preferences_dialog`: silent initialization, multiple synchronized
  editors, field preservation, persistence wiring, and the shell's aggregate-only
  subscription/no navigation writes.

Test configuration also checks the four modules' direct dependency allowlists,
so importing a widget/camera/settings dependency across a boundary fails early.
The first two run even with `CADLY_BUILD_GUI=OFF`. The next three do not require
QtTest and therefore also run on the Windows/vcpkg build without `testlib`.
Linux CI also configures and builds Input with the GUI disabled and warnings
as errors, so adding an accidental Qt/camera dependency fails that gate.
Their checks remain active in release builds (no `assert`-only tests). Widget
tests use the existing offscreen QtTest setup; rendering retains its separate
framebuffer regression test.

Normal validation:

```sh
cmake --build --preset linux-debug -j 4
ctest --preset linux-debug
cmake --build --preset linux-release -j 4
ctest --preset linux-release
```
