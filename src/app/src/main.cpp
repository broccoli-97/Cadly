// Cadly entry point. Wires up the Qt application, MainWindow, and shared
// process state (logging, surface format, theme, recents persistence).

#include "cadly/app/RecentFiles.h"
#include "cadly/app/Settings.h"
#include "cadly/app/Theme.h"
#include "cadly/platform/Log.h"
#include "cadly/ui/MainWindow.h"
#include "cadly/ui/ThemeTokens.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>

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

#ifdef Q_OS_LINUX
  // WSLg ships both a Wayland compositor and XWayland, and Qt prefers the
  // wayland platform whenever WAYLAND_DISPLAY is set. On WSLg's Weston +
  // Mesa D3D12 stack, creating our 4.1 core context under the wayland
  // platform deadlocks in a compositor roundtrip inside show(): the process
  // stays alive blocked in poll(), no window ever appears, and the event
  // loop never starts. The identical context request through XWayland/GLX
  // works (GL 4.6 core via D3D12 passthrough), so under WSL steer Qt to
  // xcb — but only when the user hasn't chosen a platform explicitly.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    QFile osrelease(QStringLiteral("/proc/sys/kernel/osrelease"));
    if (osrelease.open(QIODevice::ReadOnly) &&
        osrelease.readAll().toLower().contains("microsoft")) {
      qputenv("QT_QPA_PLATFORM", "xcb");
    }
  }
#endif

  QApplication app(argc, argv);
  app.setOrganizationName("Cadly");
  app.setApplicationName("Cadly");
  app.setApplicationVersion("0.1.0");
  app.setWindowIcon(QIcon());

  cadly::app::Settings settings;

  // Theme before any widget is constructed so nothing ever paints with the
  // stock platform style. ThemeManager (ui) carries the token table the
  // custom chrome reads; apply_theme keeps QStyle/QPalette in step. The
  // changed() hookup makes the View ▸ Dark Appearance toggle restyle the
  // running app and persist the choice.
  auto& theme_mgr = cadly::ui::ThemeManager::instance();
  theme_mgr.set_dark(settings.dark_theme());
  cadly::app::apply_theme(app, theme_mgr.dark());
  QObject::connect(&theme_mgr, &cadly::ui::ThemeManager::changed,
                   &app, [&app, &settings, &theme_mgr]() {
    cadly::app::apply_theme(app, theme_mgr.dark());
    settings.set_dark_theme(theme_mgr.dark());
  });

  cadly::platform::init_logging("info");

  QCommandLineParser parser;
  parser.setApplicationDescription("Native C++ CAD viewer.");
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption logOpt({"l", "log-level"},
    "Log level: trace|debug|info|warn|error", "level", "info");
  parser.addOption(logOpt);
  // Developer aid: grab the fully-started window into a PNG and exit. Lets
  // UI changes be eyeballed from a terminal (CI, agents, WSL) without a
  // human driving the session.
  QCommandLineOption shotOpt("screenshot",
    "Save a screenshot of the window to <path> ~3.5s after startup, then exit.",
    "path");
  parser.addOption(shotOpt);
  QCommandLineOption demoOpt("demo",
    "Dev aid: drive a UI state before the screenshot "
    "(wireframe|hiddenline|light|dark|display|import|views|getinfo|zerochrome).",
    "state");
  parser.addOption(demoOpt);
  parser.addPositionalArgument("file", "Optional CAD file to open at startup.");
  parser.process(app);

  if (parser.isSet(logOpt)) {
    cadly::platform::init_logging(parser.value(logOpt).toUtf8().constData());
  }
  CADLY_LOG_INFO("Cadly {} starting", "0.1.0");

  cadly::ui::MainWindow window;

  // Recents + last-open-dir live in app-side persistence; the shell only
  // displays them and reports successful imports. This wiring is what turns
  // the previously dead RecentFiles/last_open_directory code into features.
  cadly::app::RecentFiles recents;
  window.set_recent_files(recents.entries());
  window.set_last_open_directory(settings.last_open_directory());
  QObject::connect(&window, &cadly::ui::MainWindow::file_imported,
                   &recents, [&recents, &settings, &window](const QString& path) {
    recents.add(path);
    const QString dir = QFileInfo(path).absolutePath();
    settings.set_last_open_directory(dir);
    window.set_last_open_directory(dir);
  });
  QObject::connect(&recents, &cadly::app::RecentFiles::changed,
                   &window, [&recents, &window]() {
    window.set_recent_files(recents.entries());
  });
  QObject::connect(&window, &cadly::ui::MainWindow::recents_clear_requested,
                   &recents, &cadly::app::RecentFiles::clear);

  if (auto blob = settings.window_geometry(); !blob.isEmpty()) {
    window.restoreGeometry(blob);
  }
  window.show();

  // Defer file open until after the GL context has had a chance to come up.
  const auto positionals = parser.positionalArguments();
  if (!positionals.isEmpty()) {
    QMetaObject::invokeMethod(&window, [&window, path = positionals.first()]() {
      window.open_file(path);
    }, Qt::QueuedConnection);
  }

  if (parser.isSet(shotOpt)) {
    const QString shot_path = parser.value(shotOpt);
    const QString demo = parser.value(demoOpt);
    // Trigger the demo state a beat before the grab so any import has landed
    // and the action's repaint has flushed.
    if (!demo.isEmpty()) {
      QTimer::singleShot(3000, &window, [&window, demo]() {
        window.run_demo(demo);
      });
    }
    QTimer::singleShot(3500, &window, [&window, shot_path, demo]() {
      // Popovers are separate top-level Qt::Popup windows; QWidget::grab() on
      // the main window misses them and an X11 screen grab can't see their
      // Wayland surface on WSLg. Grab the active popup widget directly when
      // one is up, so the popover demos actually show the card.
      QPixmap pm;
      const bool has_popup = demo == QLatin1String("views") ||
                             demo == QLatin1String("getinfo");
      if (has_popup) {
        if (auto* popup = QApplication::activePopupWidget()) pm = popup->grab();
      }
      if (pm.isNull()) pm = window.grab();
      pm.save(shot_path);
      QApplication::quit();
    });
  }

  QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
    settings.set_window_geometry(window.saveGeometry());
  });

  return app.exec();
}
