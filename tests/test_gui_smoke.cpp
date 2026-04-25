#include <QtTest/QtTest>
#include "../src/ui/MainWindow.h"

class GuiSmoke : public QObject {
  Q_OBJECT
private slots:
  void canCreate();
};

void GuiSmoke::canCreate() {
  MainWindow w;
  QVERIFY(!w.isVisible());
}

int runGuiSmoke(int argc, char** argv) {
  GuiSmoke tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "test_gui_smoke.moc"
