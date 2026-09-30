// ============================================================================
// 阶段 E · E.8 · 控件自定义底图(ControlBase::imagePath)
//
// 验证 5 件事,每件都对应一个"不做就会出现什么怪象":
//   1. 没设 imagePath → 手绘矢量照旧(不是"画不出东西")
//   2. 设了有效图片 → 底图真的画进 QImage(不是"设了没生效")
//   3. 路径失效 → 回退手绘矢量 + imageLoaded() 为 false
//      (不能崩,也不能静默画灰底让人以为"设了没反应")
//   4. 相对路径 + basePath → 能解析(工程文件拷走/发给同事后图片还在)
//   5. 图片走【下层】,矢量层仍在上 —— 储罐液位不能被图盖掉
//
// 关联:ControlBase.{h,cpp}, ImageControl.{h,cpp}, PropertySheet.cpp
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTemporaryDir>
#include <QUndoStack>

#include "widgets/base/ControlBase.h"
#include "widgets/TankControl.h"
#include "widgets/PumpControl.h"
#include "editor/PropertySheet.h"
#include "editor/PropertyChangeCommand.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

// 造一张纯色 PNG 当底图素材。真实项目里是用户自己的图,
// 这里只需要"确定存在且可解码"。
static bool writeTestPng(const QString& path, const QColor& c) {
    QImage img(40, 40, QImage::Format_ARGB32);
    img.fill(c);
    return img.save(path, "PNG");
}

// 把控件渲到 QImage 上,返回像素供断言
static QImage renderToImage(ControlBase* c, int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    QStyleOptionGraphicsItem opt;
    opt.exposedRect = QRectF(0, 0, w, h);
    c->paint(&p, &opt);
    p.end();
    return img;
}

class TestControlBackgroundImage : public QObject {
    Q_OBJECT
private slots:
    void noImage_keepsVectorDrawing();
    void validImage_actuallyPaints();
    void brokenPath_fallsBackToVector();
    void relativePath_resolvesAgainstBasePath();
    void imageIsUnderneathVectorLayer();
    void jsonRoundTrip_carriesImagePath();
    void basePathChange_invalidatesCache();
    void propertySheet_hasBrowseAndClearButtons();
};

// ===== 1. 没设图 → 手绘矢量照旧 =====
void TestControlBackgroundImage::noImage_keepsVectorDrawing() {
    auto* tank = new TankControl;
    tank->setSize(QSizeF(60, 100));
    // 储罐默认是灰蓝罐壁,渲出来应有非透明像素
    const QImage img = renderToImage(tank, 60, 100);
    int painted = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0) ++painted;
    QVERIFY2(painted > 100, "没设底图时储罐仍应画出手绘矢量");
    QVERIFY(tank->imageLoaded());   // 没配图不算"失败"
    delete tank;
}

// ===== 2. 有效图片 → 真的画出来 =====
void TestControlBackgroundImage::validImage_actuallyPaints() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.filePath("bg.png");
    QVERIFY(writeTestPng(png, QColor(255, 0, 0)));   // 纯红

    auto* tank = new TankControl;
    tank->setSize(QSizeF(60, 100));
    tank->setImagePath(png);

    QVERIFY2(tank->imageLoaded(), "有效 PNG 应加载成功");

    // ★ 关键:画面上要真的有红像素。设了路径但没画出来是本功能最容易的失败方式
    const QImage img = renderToImage(tank, 60, 100);
    bool sawRed = false;
    for (int y = 0; y < img.height() && !sawRed; ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 80 && c.blue() < 80) { sawRed = true; break; }
        }
    }
    QVERIFY2(sawRed, "底图像素没出现在渲染结果里 —— 底图层没生效");
    delete tank;
}

// ===== 3. 路径失效 → 回退手绘,不崩 =====
void TestControlBackgroundImage::brokenPath_fallsBackToVector() {
    auto* tank = new TankControl;
    tank->setSize(QSizeF(60, 100));
    tank->setImagePath("Z:/绝对不存在的路径/nope.png");

    // ★ 不能崩是硬要求
    QVERIFY2(!tank->imageLoaded(), "不存在的路径应报告加载失败");
    // 回退:矢量层仍要画出来,画面不能空白
    const QImage img = renderToImage(tank, 60, 100);
    int painted = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0) ++painted;
    QVERIFY2(painted > 100, "底图失效时应回退手绘矢量,而不是画一片空白");
    delete tank;
}

// ===== 4. 相对路径 + basePath =====
void TestControlBackgroundImage::relativePath_resolvesAgainstBasePath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 图放在 <工程目录>/assets/ 下
    QVERIFY(QDir(dir.path()).mkpath("assets"));
    const QString png = QDir(dir.path()).filePath("assets/bg.png");
    QVERIFY(writeTestPng(png, QColor(0, 200, 0)));

    auto* pump = new PumpControl;
    pump->setSize(QSizeF(60, 60));
    // ★ 存的是【相对路径】—— 工程文件拷走后还能找到图
    pump->setImagePath(QStringLiteral("assets/bg.png"));
    pump->setBasePath(dir.path());

    QVERIFY2(pump->resolvedImagePath() == png,
             qPrintable(QStringLiteral("解析成了 %1,期望 %2")
                            .arg(pump->resolvedImagePath(), png)));
    QVERIFY2(pump->imageLoaded(), "相对路径应能按 basePath 解析成功");
    delete pump;
}

// ===== 5. 图在下层,矢量在上(储罐液位不被盖掉)=====
void TestControlBackgroundImage::imageIsUnderneathVectorLayer() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 底图 = 不透明纯红
    const QString png = dir.filePath("red.png");
    QVERIFY(writeTestPng(png, QColor(255, 0, 0)));

    auto* tank = new TankControl;
    tank->setSize(QSizeF(60, 100));
    tank->setImagePath(png);
    tank->setMinimum(0.0);
    tank->setMaximum(100.0);
    tank->setValue(100.0);        // 满罐
    tank->setLiquidColor(QColor(0, 0, 255));   // 液体纯蓝
    tank->setTankColor(QColor(0, 255, 255));   // 空腔纯青

    const QImage img = renderToImage(tank, 60, 100);
    // 满罐时,罐体中央应该是液体色(蓝),不是底图色(红)
    const QColor center = img.pixelColor(30, 50);
    QVERIFY2(center.blue() > center.red(),
             qPrintable(QStringLiteral("满罐时罐体中心应是液体色,实际 rgb=(%1,%2,%3) "
                                       "—— 底图盖住了矢量层,或液位没画")
                            .arg(center.red()).arg(center.green()).arg(center.blue())));
    delete tank;
}

// ===== 6. JSON 往返 =====
void TestControlBackgroundImage::jsonRoundTrip_carriesImagePath() {
    auto* tank = new TankControl;
    tank->setImagePath(QStringLiteral("assets/tank.png"));
    tank->setScaleToFit(false);

    const QJsonObject json = tank->propertiesToJson();
    QVERIFY2(json.contains("imagePath"), "imagePath 应进 JSON(否则工程文件存不下底图)");
    QVERIFY2(json.value("imagePath").toString() == QStringLiteral("assets/tank.png"),
             "imagePath 存的值不对");

    auto* tank2 = new TankControl;
    tank2->propertiesFromJson(json);
    QVERIFY2(tank2->imagePath() == QStringLiteral("assets/tank.png"), "imagePath 读回来不对");
    QVERIFY2(tank2->scaleToFit() == false, "scaleToFit 读回来不对");

    delete tank;
    delete tank2;
}

// ===== 7. 换工程目录必须让缓存失效 =====
void TestControlBackgroundImage::basePathChange_invalidatesCache() {
    QTemporaryDir d1, d2;
    QVERIFY(d1.isValid() && d2.isValid());
    // 同一个相对文件名,两个目录各放一张【不同颜色】的图
    QVERIFY(writeTestPng(QDir(d1.path()).filePath("x.png"), QColor(255, 0, 0)));
    QVERIFY(writeTestPng(QDir(d2.path()).filePath("x.png"), QColor(0, 0, 255)));

    auto* tank = new TankControl;
    tank->setSize(QSizeF(40, 40));
    tank->setImagePath(QStringLiteral("x.png"));

    tank->setBasePath(d1.path());
    const QImage a = renderToImage(tank, 40, 40);
    tank->setBasePath(d2.path());   // ★ 换成另一个工程目录
    const QImage b = renderToImage(tank, 40, 40);

    // 若缓存没失效,b 会和 a 一样(还是红的)—— 正是"打开 B 工程还挂着 A 工程的图"
    QVERIFY2(a != b, "换 basePath 后画面没变化 —— 底图缓存没失效,串工程了");
    delete tank;
}

// ===== 8. 属性面板有浏览/清除按钮 =====
void TestControlBackgroundImage::propertySheet_hasBrowseAndClearButtons() {
    QUndoStack stack;
    PropertySheet sheet(&stack);
    auto* tank = new TankControl;
    sheet.setTargets({tank});

    QWidget* browse = sheet.findChild<QWidget*>(QStringLiteral("imagePath_browse"));
    QWidget* clear  = sheet.findChild<QWidget*>(QStringLiteral("imagePath_clear"));
    QWidget* edit   = sheet.findChild<QWidget*>(QStringLiteral("imagePath_edit"));
    QVERIFY2(edit != nullptr, "属性面板应有 imagePath 输入框");
    QVERIFY2(browse != nullptr, "属性面板应有 [浏览…] 按钮");
    QVERIFY2(clear != nullptr, "属性面板应有 [清除] 按钮");
    delete tank;
}

QTEST_MAIN(TestControlBackgroundImage)
#include "tst_ControlBackgroundImage.moc"
