#include "ToolTip.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QPointer>
#include <QTextDocument>
#include <QToolTip>
#include <QWidget>

namespace cadly::ui {

namespace {

constexpr int kMaximumLineWidth = 240;

// QTextDocument treats the fixed table width in bounded_tooltip() as a
// *preferred* width: a cell never shrinks below its minimum content width,
// i.e. the longest run with no line-break opportunity. Windows paths
// ("C:\...\part.step") and underscore_heavy basenames contain no UAX-14
// break points at all, so without help the table expands right past the
// bound (measured 425 px for a typical path). Insert zero-width spaces as
// explicit break points: after a separator once a run has grown long, and
// unconditionally once a run exceeds what fits on one bounded line. Words
// of normal prose stay far below kBreakAfterSeparator and pass through
// byte-identical.
constexpr int kBreakAfterSeparator = 16;
constexpr int kForcedBreak         = 32;

QString with_break_opportunities(const QString& text) {
  QString out;
  out.reserve(text.size() + 8);
  int run = 0;
  for (const QChar ch : text) {
    out.append(ch);
    if (ch.isSpace()) {
      run = 0;
      continue;
    }
    ++run;
    const bool separator =
      ch == QLatin1Char('/') || ch == QLatin1Char('\\') ||
      ch == QLatin1Char('_') || ch == QLatin1Char('.') ||
      ch == QLatin1Char('-') || ch == QLatin1Char(':');
    if ((separator && run >= kBreakAfterSeparator) || run >= kForcedBreak) {
      out.append(QChar(0x200B)); // zero-width space: break point, no ink
      run = 0;
    }
  }
  return out;
}

// App-wide QEvent::ToolTip interceptor; see install_bounded_tooltips() in
// the header for why wrapping happens at show time.
class TooltipBounder final : public QObject {
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::ToolTip) return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return false;
    const QString tip = widget->toolTip();
    if (tip.isEmpty()) return false; // e.g. QTabBar: per-tab tips, not ours
    const QString bounded = bounded_tooltip(tip);
    if (bounded == tip) return false; // short plain text: default handling
    QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), bounded,
                       widget);
    return true;
  }
};

} // namespace

QString bounded_tooltip(const QString& text) {
  // Qt tooltips use Qt::AutoText: a short unescaped path like
  // "/tmp/a<b>.step" (legal on Linux) is sniffed as rich text and the tag
  // characters silently vanish from the popup, so tag-like text must be
  // escaped even when it fits on one line.
  const bool tag_like = Qt::mightBeRichText(text);
  const bool fits =
    QFontMetrics(QToolTip::font()).horizontalAdvance(text) <=
    kMaximumLineWidth;
  if (!tag_like && fits) return text;

  QString escaped = with_break_opportunities(text).toHtmlEscaped();
  escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
  if (fits) {
    // Escaped for correct display, marked rich explicitly, but no width
    // table — a fixed 240 px box around a 90 px path would look broken.
    return QStringLiteral("<qt>%1</qt>").arg(escaped);
  }

  // A fixed-width table is deliberately used here: QTextDocument honours
  // table widths consistently across the Qt 6.4 and 6.8 versions we support,
  // while CSS max-width on a div is platform/style dependent.
  return QStringLiteral(
           "<qt><table width=\"%1\" cellspacing=\"0\" cellpadding=\"0\">"
           "<tr><td>%2</td></tr></table></qt>")
    .arg(kMaximumLineWidth)
    .arg(escaped);
}

void install_bounded_tooltips() {
  static QPointer<TooltipBounder> installed;
  if (installed || !qApp) return;
  installed = new TooltipBounder(qApp);
  qApp->installEventFilter(installed);
}

} // namespace cadly::ui
