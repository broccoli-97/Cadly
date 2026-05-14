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
  QSurfaceFormat fmt;
  fmt.setVersion(4, 1);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  fmt.setStencilBufferSize(8);
  fmt.setSamples(4);
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
