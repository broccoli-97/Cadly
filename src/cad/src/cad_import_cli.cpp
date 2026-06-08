// cad_import_cli: command-line driver to validate STEP/IGES import without
// needing the GUI. Prints geometry stats and any diagnostics produced.

#include "cadly/cad/ImporterRegistry.h"
#include "cadly/platform/Log.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* argv0) {
  std::cerr <<
    "usage: " << argv0 << " <file.step|file.iges|dir> [options]\n"
    "options:\n"
    "  --linear-deflection <d>   (default 0.1)\n"
    "  --angular-deflection <a>  (default 0.35 rad)\n"
    "  --no-colors\n"
    "  --no-names\n"
    "  --profile                 print detailed import stage timings\n"
    "  --verbose\n";
}

bool is_cad_file(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(
    static_cast<unsigned char>(c)));
  return ext == ".step" || ext == ".stp" || ext == ".p21" ||
         ext == ".iges" || ext == ".igs";
}

std::vector<std::filesystem::path>
expand_inputs(const std::vector<std::filesystem::path>& inputs) {
  std::vector<std::filesystem::path> out;
  for (const auto& input : inputs) {
    if (std::filesystem::is_directory(input)) {
      for (const auto& entry : std::filesystem::directory_iterator(input)) {
        if (entry.is_regular_file() && is_cad_file(entry.path())) {
          out.push_back(entry.path());
        }
      }
    } else {
      out.push_back(input);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

void print_result(const std::filesystem::path& path,
                  const cadly::cad::ImportResult& result,
                  bool profile) {
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
  if (result.scene) {
    std::size_t mesh_count = 0, double_sided = 0;
    for (const auto& m : result.scene->meshes) {
      if (!m) continue;
      ++mesh_count;
      if (m->double_sided) ++double_sided;
    }
    std::cout << fmt::format("  meshes       : {} ({} double-sided / non-solid)\n",
                             mesh_count, double_sided);
  }
  if (profile && !result.summary.timings.empty()) {
    std::cout << "  timings:\n";
    auto timings = result.summary.timings;
    std::sort(timings.begin(), timings.end(),
              [](const auto& a, const auto& b) {
                return a.duration > b.duration;
              });
    for (const auto& timing : timings) {
      const double ms = static_cast<double>(timing.duration.count()) / 1000.0;
      std::cout << fmt::format("    {:32} {:>10.3f} ms\n",
                               timing.stage, ms);
    }
  }
  std::cout << "------------------------------------------------------------\n";
  if (!result.summary.diagnostics.empty()) {
    std::cout << "  diagnostics:\n";
    for (const auto& d : result.summary.diagnostics) {
      const char* tag =
        d.severity == cadly::cad::DiagnosticSeverity::Error   ? "ERROR  "
      : d.severity == cadly::cad::DiagnosticSeverity::Warning ? "WARN   "
                                                              : "info   ";
      std::cout << "    " << tag << " " << d.message << "\n";
    }
  }
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
  bool profile = false;
  std::vector<std::filesystem::path> inputs;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
    if (a == "--verbose") { platform::init_logging("debug"); continue; }
    if (a == "--profile") { opts.profile_timings = true; profile = true; continue; }
    if (a == "--no-colors") { opts.load_colors = false; continue; }
    if (a == "--no-names")  { opts.load_names  = false; continue; }
    if (a == "--linear-deflection" && i + 1 < argc) {
      opts.linear_deflection = std::stod(argv[++i]); continue;
    }
    if (a == "--angular-deflection" && i + 1 < argc) {
      opts.angular_deflection = std::stod(argv[++i]); continue;
    }
    if (!a.empty() && a[0] != '-') {
      inputs.emplace_back(a); continue;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    print_usage(argv[0]);
    return 2;
  }

  if (inputs.empty()) {
    print_usage(argv[0]);
    return 2;
  }

  const auto files = expand_inputs(inputs);
  if (files.empty()) {
    std::cerr << "No CAD files found.\n";
    return 2;
  }

  bool all_ok = true;
  for (const auto& file : files) {
    auto result = cad::ImporterRegistry::instance().import(file, opts);
    print_result(file, result, profile);
    all_ok = all_ok && result.success;
  }

  return all_ok ? 0 : 1;
}
