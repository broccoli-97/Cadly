// Cadly entry point. Wires up the Qt application, MainWindow, and shared
// process state (logging, surface format).

#include "cadly/app/RecentFiles.h"
#include "cadly/app/Settings.h"
#include "cadly/platform/Log.h"
#include "cadly/ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
  // Request a 4.1 core context before QApplication exists; QOpenGLWidget will
  // honour this when creating its surface.
  //
  // setSamples is intentionally left at 0 (single-sample). The renderer owns
  // MSAA via its own offscreen multisample framebuffer (see RenderTypes.h's
  // DisplayMode::msaa_samples and GLRenderer's ensure_msaa_target), and
  // resolves into this default framebuffer at the end of every frame. Asking
  // Qt for a multisample default framebuffer here would force a second blit
  // pass and turn the resolve into a sample-count mismatch under any
  // non-matching MSAA setting.
  //
  // setAlphaBufferSize(0) tells the platform we don't want an alpha channel
  // in the default framebuffer. The OS compositor (DWM / Quartz / Wayland /
  // X) then treats the window as fully opaque regardless of what the GL
  // pipeline writes to the colour buffer's alpha — which protects us from a
  // class of bugs where a blended pass (lines, grid, pivot) reduces dst
  // alpha and the desktop bleeds through "transparent" pixels. The renderer
  // also clears with alpha=1 and uses alpha-preserving blend funcs (see
  // GLRenderer.cpp), so this is defence-in-depth rather than the sole fix.
  QSurfaceFormat fmt;
  fmt.setVersion(4, 1);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setStencilBufferSize(8);
  fmt.setAlphaBufferSize(0);
  QSurfaceFormat::setDefaultFormat(fmt);

  QApplication app(argc, argv);
  app.setOrganizationName("Cadly");
  app.setApplicationName("Cadly");
  app.setApplicationVersion("0.1.0");
  app.setWindowIcon(QIcon());

  cadly::platform::init_logging("info");

  QCommandLineParser parser;
  parser.setApplicationDescription("Native C++ CAD viewer.");
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption logOpt({"l", "log-level"},
    "Log level: trace|debug|info|warn|error", "level", "info");
  parser.addOption(logOpt);
  parser.addPositionalArgument("file", "Optional CAD file to open at startup.");
  parser.process(app);

  if (parser.isSet(logOpt)) {
    cadly::platform::init_logging(parser.value(logOpt).toUtf8().constData());
  }
  CADLY_LOG_INFO("Cadly {} starting", "0.1.0");

  cadly::ui::MainWindow window;

  cadly::app::Settings settings;
  if (auto blob = settings.window_geometry(); !blob.isEmpty()) {
    window.restoreGeometry(blob);
  }
  if (auto blob = settings.window_state(); !blob.isEmpty()) {
    window.restoreState(blob);
  }
  window.show();

  // Defer file open until after the GL context has had a chance to come up.
  const auto positionals = parser.positionalArguments();
  if (!positionals.isEmpty()) {
    QMetaObject::invokeMethod(&window, [&window, path = positionals.first()]() {
      window.open_file(path);
    }, Qt::QueuedConnection);
  }

  QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
    settings.set_window_geometry(window.saveGeometry());
    settings.set_window_state(window.saveState());
  });

  return app.exec();
}
