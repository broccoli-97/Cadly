#pragma once

namespace cadly::input {

struct Position {
  int x{0};
  int y{0};
};

// Camera commands contain resolved motion, never buttons or settings keys.
// Orbit x/y are radians; Pan x/y are fractions of camera distance; Zoom uses
// a multiplicative distance factor. Positions are logical viewport pixels.
// This is the boundary for future sensitivity/inversion policies: consumers
// keep executing the same commands without learning about those preferences.
enum class CommandType {
  None, BeginOrbit, EndOrbit, OrbitFree, OrbitTurntable, Pan, Zoom, ZoomAtCursor,
};

struct NavigationCommand {
  CommandType type{CommandType::None};
  Position position{};
  float x{0.0f};
  float y{0.0f};
  float factor{1.0f};
};

} // namespace cadly::input
