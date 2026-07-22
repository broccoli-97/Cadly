Cadly - Linux x86-64 portable build

Qt, Open CASCADE, fmt, spdlog, and their runtime dependencies are included.
No distribution packages need to be installed for Cadly itself.

Run:
  bin/cadly sample-files/as1-ug-214.stp
  bin/cad_import_cli sample-files/as1-ug-214.stp

System requirements:
  - x86-64 Linux with glibc 2.39 or newer (Ubuntu 24.04 or equivalent)
  - an X11/XWayland or Wayland desktop session
  - a GPU and system graphics driver supporting OpenGL 4.1 or newer

glibc, the ELF loader, and GPU driver libraries intentionally come from the
operating system because they must match the running kernel and graphics
driver. They are not application packages users should install separately on
a supported desktop distribution.
