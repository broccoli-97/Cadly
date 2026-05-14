// cad_import_cli: command-line driver to validate STEP/IGES import without
// needing the GUI. Prints geometry stats and any diagnostics produced.

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/platform/Log.h"

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage(const char* argv0) {
  std::cerr <<
    "usage: " << argv0 << " <file.step|file.iges> [options]\n"
    "options:\n"
    "  --linear-deflection <d>   (default 0.5)\n"
    "  --angular-deflection <a>  (default 0.5 rad)\n"
    "  --no-colors\n"
    "  --no-names\n"
    "  --verbose\n";
}

} // namespace

int main(int argc, char** argv) {
  using namespace cadly;

  if (argc < 2) {
    print_usage(argv[0]);
    return 2;
  }

  platform::init_logging("info");

  cad::ImportOptions opts;
  std::filesystem::path path;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
    if (a == "--verbose") { platform::init_logging("debug"); continue; }
    if (a == "--no-colors") { opts.load_colors = false; continue; }
    if (a == "--no-names")  { opts.load_names  = false; continue; }
    if (a == "--linear-deflection" && i + 1 < argc) {
      opts.linear_deflection = std::stod(argv[++i]); continue;
    }
    if (a == "--angular-deflection" && i + 1 < argc) {
      opts.angular_deflection = std::stod(argv[++i]); continue;
    }
    if (!a.empty() && a[0] != '-') {
      path = a; continue;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    print_usage(argv[0]);
    return 2;
  }

  if (path.empty()) {
    print_usage(argv[0]);
    return 2;
  }

  auto result = cad::ImporterRegistry::instance().import(path, opts);

  std::cout << "------------------------------------------------------------\n";
  std::cout << "  source       : " << path.string() << "\n";
  std::cout << "  success      : " << (result.success ? "yes" : "no") << "\n";
  std::cout << fmt::format("  parse time   : {} ms\n", result.summary.parse_time.count());
  std::cout << fmt::format("  mesh time    : {} ms\n", result.summary.mesh_time.count());
  std::cout << fmt::format("  total time   : {} ms\n", result.summary.total_time.count());
  std::cout << fmt::format("  shapes/nodes : {}\n", result.summary.shape_count);
  std::cout << fmt::format("  faces        : {}\n", result.summary.face_count);
  std::cout << fmt::format("  triangles    : {}\n", result.summary.triangle_count);
  std::cout << fmt::format("  vertices     : {}\n", result.summary.vertex_count);
  if (result.scene && result.scene->world_bounds.valid()) {
    const auto& b = result.scene->world_bounds;
    std::cout << fmt::format("  bounds min   : ({:.3f}, {:.3f}, {:.3f})\n",
                             b.min.x, b.min.y, b.min.z);
    std::cout << fmt::format("  bounds max   : ({:.3f}, {:.3f}, {:.3f})\n",
                             b.max.x, b.max.y, b.max.z);
    const auto e = b.max - b.min;
    std::cout << fmt::format("  bounds size  : ({:.3f}, {:.3f}, {:.3f})\n",
                             e.x, e.y, e.z);
  }
  std::cout << "------------------------------------------------------------\n";
  if (!result.summary.diagnostics.empty()) {
    std::cout << "  diagnostics:\n";
    for (const auto& d : result.summary.diagnostics) {
      const char* tag =
        d.severity == cad::DiagnosticSeverity::Error   ? "ERROR  "
      : d.severity == cad::DiagnosticSeverity::Warning ? "WARN   "
                                                       : "info   ";
      std::cout << "    " << tag << " " << d.message << "\n";
    }
  }

  return result.success ? 0 : 1;
}
