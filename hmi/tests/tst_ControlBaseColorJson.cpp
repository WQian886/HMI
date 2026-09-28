// ============================================================================
// 阶段 E · E.1c · tst_ControlBaseColorJson —— QColor 属性存读回归
//
// 【先说清楚这不是什么 bug】动手前我判断「QJsonValue::fromVariant 不认
//   QColor,17 个控件的颜色属性存盘后全丢」,并按这个前提写了一整套修复。
//   实测结论:【判断错了】。Qt 6 的 fromVariant 认 QColor,不透明色的往返
//   完全正常(#aabbcc 进 #aabbcc 出)。tst_ProjectSchema.cpp:337 的老注释
//   早就写明了「QColor 在 JSON 里是 hex 字符串(Qt 6 QJsonValue::fromVariant
//   行为)」。错因:只看了 QJsonValue 文档里「只支持 map/list/string/double/
//   bool/int」的概述就下结论,没实测。教训记在 docs/今日教训 里了。
//
// 【真正被这次改动修掉的两条】
//
//  ① alpha 通道丢失(真的)
//     QColor::toString() 走 name(),对 alpha != 255 的色输出 6 位 #RRGGBB,
//     alpha 直接消失。实测 QColor(1,2,3,78) → 存 "#010203" → 读回
//     QColor(1,2,3,255)。控件里半透明很常见(液体高光、禁用态遮罩、
//     玻璃罐体),存一次盘再打开就变实心 —— 不报错不崩溃,画面上表现为
//     「某些控件莫名其妙变实了」,极难定位。
//
//  ② 非法颜色值会把控件画没(顺手一起修)
//     JSON 里 fillColor:"not-a-color"(工程文件是手写的,打错字很正常)
//     → QVariant 是 QString → prop.write 后 QColor 解析失败成 QColor()
//     = 全透明黑 → 控件直接从画面上消失。改成用【当前值】当 fallback。
//
// 【为什么单独一个测试文件】这是基类行为,不是某个控件的。
//   塞进 tst_TankControl 会让人以为只影响储罐;实际影响所有带颜色的控件。
//   按【基类行为】组织,以后每加带颜色的控件都不用记得补测。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QJsonObject>
#include "widgets/base/ControlBase.h"
#include "widgets/RectangleControl.h"
#include "widgets/LedControl.h"
#include "widgets/GaugeControl.h"
#include "widgets/TankControl.h"

using namespace qwhmi::widgets;

class TestControlBaseColorJson : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    // ★ 核心:带颜色属性的控件,toJson 出来的颜色字段不能是 null
    void colorProperty_serializedAsHexString();
    // ★ 核心:存-读往返颜色不变
    void colorRoundTrips_throughToFromJson();
    // ★ 真正修掉的 bug ①:alpha 通道往返不丢
    void alphaChannel_survivesRoundTrip();
    // ★ 不透明色必须仍是 6 位 —— 不能为了修 alpha 把存量工程文件格式改了
    void opaqueColor_staysSixDigit();
    // ★ 修法选了 QColor::name() 自己解析,六位/八位都得认
    void colorFromJson_acceptsSixDigitHex();
    // ★ 格式非法 / 字段缺失时保留原色,不能变全透明黑(控件直接画没)
    void malformedColor_keepsExistingValue();
    void missingColorKey_keepsExistingValue();
    // ★ 抽查:既有控件里的几个,证明修的是基类不是储罐
    void existingControls_colorsAlsoRoundTrip();
};

void TestControlBaseColorJson::colorProperty_serializedAsHexString() {
    TankControl t;
    t.setLiquidColor(QColor(10, 200, 30));
    t.setTankColor(QColor(200, 210, 220));

    const QJsonObject o = t.toJson();
    QVERIFY(o.value("liquidColor").isString());
    QCOMPARE(o.value("liquidColor").toString(), QString("#0ac81e"));
    QCOMPARE(o.value("tankColor").toString(), QString("#c8d2dc"));
}

void TestControlBaseColorJson::colorRoundTrips_throughToFromJson() {
    TankControl src;
    src.setLiquidColor(QColor(12, 34, 56));
    src.setTankColor(QColor(200, 100, 50));

    TankControl dst;
    dst.fromJson(src.toJson());

    QCOMPARE(dst.liquidColor(), QColor(12, 34, 56));
    QCOMPARE(dst.tankColor(), QColor(200, 100, 50));
}

// ★ 这是本次真正修掉的 bug。改动前实测:
//     QColor(1,2,3,78) → 存 "#010203" → 读回 QColor(1,2,3,255)
//   alpha 78 直接消失,半透明控件存一次盘就变实心。
void TestControlBaseColorJson::alphaChannel_survivesRoundTrip() {
    TankControl src;
    src.setLiquidColor(QColor(1, 2, 3, 78));
    const QJsonObject o = src.toJson();
    // 半透明色必须输出 8 位带 alpha
    QCOMPARE(o.value("liquidColor").toString(), QString("#4e010203"));

    TankControl dst;
    dst.fromJson(o);
    QCOMPARE(dst.liquidColor(), QColor(1, 2, 3, 78));
    QCOMPARE(dst.liquidColor().alpha(), 78);
}

// ★ 不能为了修 alpha 把不透明色的格式也改了 —— 存量工程文件里全是 6 位,
//   tst_ProjectSchema.cpp:338 也断言了 6 位。name() 对 alpha=255 正好输出 6 位。
void TestControlBaseColorJson::opaqueColor_staysSixDigit() {
    TankControl t;
    t.setLiquidColor(QColor(0xaa, 0xbb, 0xcc));
    const QJsonObject o = t.toJson();
    QCOMPARE(o.value("liquidColor").toString(), QString("#aabbcc"));
    // 6 位 + 前导 '#' = 7 个字符。8 位会变成 9 个
    QCOMPARE(o.value("liquidColor").toString().length(), 7);
}

void TestControlBaseColorJson::colorFromJson_acceptsSixDigitHex() {
    // 工程文件是人手写的/从别处拷来的,六位 #RRGGBB 很常见。
    // QColor 构造自己会处理,这里锁住不被我们的解析挡掉。
    TankControl t;
    t.setLiquidColor(QColor(1, 2, 3));
    QJsonObject obj;
    obj["liquidColor"] = "#ff1234";   // 缺 alpha 位
    t.fromJson(obj);
    QCOMPARE(t.liquidColor(), QColor(0xff, 0x12, 0x34));
}

void TestControlBaseColorJson::malformedColor_keepsExistingValue() {
    // ★ 关键:不是返回 QColor()(全透明黑)。控件会直接"消失"在画面上,
    //   而工程文件里只是有个手滑写错的颜色字段 —— 表现极难定位
    TankControl t;
    t.setLiquidColor(QColor(10, 200, 30));
    QJsonObject obj;
    obj["liquidColor"] = "not-a-color";
    t.fromJson(obj);
    QCOMPARE(t.liquidColor(), QColor(10, 200, 30));
}

void TestControlBaseColorJson::missingColorKey_keepsExistingValue() {
    // 老工程文件可能没有这个字段(控件是后加的属性)
    TankControl t;
    t.setTankColor(QColor(1, 2, 3));
    QJsonObject obj;   // 空对象,一个字段都没有
    t.fromJson(obj);
    QCOMPARE(t.tankColor(), QColor(1, 2, 3));
    QCOMPARE(t.liquidColor(), QColor(0, 140, 220));   // 默认值也不能被清掉
}

void TestControlBaseColorJson::existingControls_colorsAlsoRoundTrip() {
    // ★ 证明修的是 ControlBase 基类 —— 既有控件的 8 个颜色属性同样受益。
    //   这几个都在本次改动之前就存在,如果只测 TankControl 就会漏掉它们。
    {
        RectangleControl src;
        src.setFillColor(QColor(11, 22, 33));
        src.setStrokeColor(QColor(44, 55, 66));
        RectangleControl dst;
        dst.fromJson(src.toJson());
        QCOMPARE(dst.fillColor(), QColor(11, 22, 33));
        QCOMPARE(dst.strokeColor(), QColor(44, 55, 66));
    }
    {
        LedControl src;
        src.setOnColor(QColor(0, 255, 0));
        src.setOffColor(QColor(60, 60, 60));
        LedControl dst;
        dst.fromJson(src.toJson());
        QCOMPARE(dst.onColor(), QColor(0, 255, 0));
        QCOMPARE(dst.offColor(), QColor(60, 60, 60));
    }
    {
        GaugeControl src;
        src.setNeedleColor(QColor(220, 40, 40));
        GaugeControl dst;
        dst.fromJson(src.toJson());
        QCOMPARE(dst.needleColor(), QColor(220, 40, 40));
    }
}

QTEST_MAIN(TestControlBaseColorJson)
#include "tst_ControlBaseColorJson.moc"
