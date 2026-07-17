#include "Popover.h"

#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QTest>
#include <QToolButton>

namespace cadly::ui {

class PopoverTest : public QObject {
  Q_OBJECT

private slots:
  void pinned_card_can_be_closed();
};

void PopoverTest::pinned_card_can_be_closed() {
  QWidget main_window;
  main_window.resize(640, 480);
  main_window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&main_window));

  QWidget anchor(&main_window);
  anchor.setGeometry(20, 20, 80, 24);
  anchor.show();

  auto* popover = Popover::show_for(new QLabel(QStringLiteral("Details")),
                                    &anchor, QStringLiteral("Part"), true);
  QVERIFY(popover->isVisible());

  const auto buttons = popover->findChildren<QToolButton*>();
  QCOMPARE(buttons.size(), 1);
  QTest::mouseClick(buttons.front(), Qt::LeftButton);

  PinnedCard* card = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT([&] {
    card = main_window.findChild<PinnedCard*>();
    return card && card->isVisible();
  }(), 1000);

  QCOMPARE(card->parentWidget(), &main_window);
  QVERIFY(!card->isWindow());
  const auto close_buttons = card->findChildren<QToolButton*>();
  QCOMPARE(close_buttons.size(), 1);
  const QPoint close_center = close_buttons.front()->mapToGlobal(
    close_buttons.front()->rect().center());
  QTRY_COMPARE(QApplication::widgetAt(close_center), close_buttons.front());
  QPointer<PinnedCard> guard(card);
  QTest::mouseClick(close_buttons.front(), Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 1000);
}

} // namespace cadly::ui

QTEST_MAIN(cadly::ui::PopoverTest)

#include "popover_test.moc"
