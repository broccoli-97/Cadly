#pragma once

#include <QString>

namespace cadly::ui {

// Qt lays out plain-text tooltips as one natural-width line. Convert text
// that would exceed the inspector width to bounded rich text so the popup
// stays local to its control on every window-system backend. Also escapes
// short tag-like strings (legal in file paths) that Qt::AutoText would
// otherwise misparse as rich text.
QString bounded_tooltip(const QString& text);

// Install an application-wide QEvent::ToolTip filter that routes every
// widget tooltip through bounded_tooltip() at show time. Wrapping at show
// time rather than at each setToolTip call keeps the stored toolTip()
// property plain — QAccessibleWidget reports that property verbatim as the
// accessible Description, so pre-wrapped markup would be announced by
// screen readers — and bounds future call sites by construction. QTabBar
// per-tab tooltips and QMenu action tooltips bypass the widget property and
// this filter; those call sites wrap explicitly. Idempotent.
void install_bounded_tooltips();

} // namespace cadly::ui
