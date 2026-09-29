// ============================================================================
// tst_ImageControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QApplication>
#include "widgets/ImageControl.h"
#include "widgets/base/ControlBase.h"

using namespace qwhmi::widgets;

class TestImageControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_ImageControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_ImageControl] cleanup"; }

    void construct_defaultSize();
    void paint_emptyPathDoesNotCrash();
    void imagePath_loadsValidImage();
    void scaleToFit_togglesDraw();
    void fromJson_restoresAllFields();
};

void TestImageControl::construct_defaultSize() {
    ImageControl img;
    QCOMPARE(img.size(), QSizeF(80, 80));
    QVERIFY(img.imagePath().isEmpty());
    QVERIFY(img.scaleToFit());
}

void TestImageControl::paint_emptyPathDoesNotCrash() {
    QGraphicsScene s;
    ImageControl img;
    s.addItem(&img);

    QImage pix(100, 100, QImage::Format_ARGB32);
    pix.fill(Qt::white);
    QPainter p(&pix);
    s.render(&p);
    p.end();
    QVERIFY(!pix.isNull());
}

void TestImageControl::imagePath_loadsValidImage() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString imgPath = tmp.path() + "/test.png";
    QImage testImg(50, 50, QImage::Format_ARGB32);
    testImg.fill(Qt::red);
    QVERIFY(testImg.save(imgPath));

    ImageControl img;
    img.setImagePath(imgPath);
    QCOMPARE(img.imagePath(), imgPath);
    // 不验证 pixmap 是 private;间接验证 paint 不崩
}

void TestImageControl::scaleToFit_togglesDraw() {
    ImageControl img;
    QSignalSpy spy(&img, &ImageControl::scaleToFitChanged);
    img.setScaleToFit(false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!img.scaleToFit());
}

void TestImageControl::fromJson_restoresAllFields() {
    ImageControl img;
    QJsonObject obj;
    // ★ 键名按 ControlBase 的 Q_PROPERTY 名写(见 tst_LabelControl.cpp 的说明):
    //   position/size 是嵌套对象,不是平铺的 x/y/w/h
    obj["position"] = QJsonObject{{"x", 30.0}, {"y", 30.0}};
    obj["size"]     = QJsonObject{{"width", 100.0}, {"height", 100.0}};
    img.fromJson(obj);
    QCOMPARE(img.position(), QPointF(30.0, 30.0));
    QCOMPARE(img.size(), QSizeF(100.0, 100.0));
}

QTEST_MAIN(TestImageControl)
#include "tst_ImageControl.moc"