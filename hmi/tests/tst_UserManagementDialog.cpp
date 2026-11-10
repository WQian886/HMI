// ============================================================================
// 阶段 B · B2.4 · 用户管理对话框 单元测试(★ 2026-11-10 新增,G-30)
//
// 为什么这个文件以前不存在
// ------------------------
//   2026-11-10 做了一次全仓体检(生产 .cpp 有没有在某个 tst_*.cpp 里出现过),
//   86 个生产 .cpp 里有 4 个【零测试引用】:
//     auth/ChangePasswordDialog.cpp
//     auth/NewUserDialog.cpp
//     auth/UserManagementDialog.cpp   ← 本文件
//     tray/SystemTray.cpp
//   零覆盖的偏偏是权限入口的最后一米 —— "能不能禁用用户"这条判据
//   就写在 UserManagementDialog 里,却从来没有人测过它。
//   2026-11-10 (G-30)查出来的正是它:判据只活在对话框里、拿【显示文本】
//   比角色名,而确认框还在跟用户说"(可后续用密码恢复)"(密码根本救不回来)。
//
// 本文件验两件事
// --------------
//   1. 【行为】对话框把表填对了、"启用选中"按钮在、选中行能取到
//   2. 【接线】判据来自公共层,不是对话框自己拿显示文本比
//
// ★ 为什么不点按钮
//   onDeleteUser / onEnableUser 里都有 QMessageBox::question ——
//   模态框会【等用户点确定】,测试里没人点它就永远挂着(测试会超时而不是失败,
//   那种"卡住"比红更坏)。所以:
//     · 按钮【接到哪个槽】→ 由源码接线判据钉住
//     · 按钮按下之后的行为 → 由公共层测(见 tst_UserManager 的
//       canDisableUser_* / enableUser_*)
//   这就是本仓库一贯的分工:主画面/对话框只留接线,判据搬进公共层。
//
// 关联:UserManagementDialog.h, UserManager.h(公共层判据的测试在 tst_UserManager)
// ============================================================================

#include <QtTest>
#include <QFile>
#include <QPushButton>
#include <QTableWidget>

#include "auth/UserManager.h"
#include "auth/UserManagementDialog.h"

using namespace qwhmi::auth;

namespace {

// 读源码文本(判据的判据 —— hmi_core 编的是"类",编不出"接线")
QString readDialogSource() {
    QFile f(QStringLiteral("%1/src/auth/UserManagementDialog.cpp")
                .arg(QString::fromLatin1(HMI_SRC_DIR_STRING)));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

// 去掉注释再比对。
//   ★ 这一步不是讲究,是必需的:UserManagementDialog.cpp 里有一整段注释
//     【故意】写着"可后续用密码恢复"(说明那句话以前是假的),
//     不剥就会把"注释提到了"当成"代码还在",那条测试永远绿。
QString codeOnly(const QString& text) {
    QString out;
    out.reserve(text.size());
    bool inBlock = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        QString keep;
        int i = 0;
        while (i < line.size()) {
            if (inBlock) {
                const int end = line.indexOf(QLatin1String("*/"), i);
                if (end < 0) { i = line.size(); break; }
                inBlock = false;
                i = end + 2;
                continue;
            }
            const int b = line.indexOf(QLatin1String("/*"), i);
            const int s = line.indexOf(QLatin1String("//"), i);
            if (s >= 0 && (b < 0 || s < b)) { keep += line.mid(i, s - i); break; }
            if (b >= 0) { keep += line.mid(i, b - i); inBlock = true; i = b + 2; continue; }
            keep += line.mid(i);
            break;
        }
        out += keep;
        out += QLatin1Char('\n');
    }
    return out;
}

// 找按钮:按【显示文本】找,而不是按成员变量名
//   (成员是 private,测试链不到 —— 只能从 QObject 的孩子里找)
QPushButton* findButton(const QWidget* root, const QString& text) {
    const auto buttons = root->findChildren<QPushButton*>();
    for (QPushButton* b : buttons) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

} // namespace

class TestUserManagementDialog : public QObject {
    Q_OBJECT
private slots:
    void table_listsUsersWithRoleAndDisabledState();
    void enableButton_exists();

    // ★ 接线判据(判据的判据)
    void judgmentComesFromSharedLayer();
    void noDisplayTextComparisonAnyMore();
    void falseRecoveryPromise_isGone();
    void enablePathGoesThroughSharedLayer();
};

void TestUserManagementDialog::table_listsUsersWithRoleAndDisabledState() {
    UserManager mgr;
    QVERIFY(mgr.addUser("alice", "p", Role::Admin));
    QVERIFY(mgr.addUser("bob",   "p", Role::Operator));
    // bob 是被禁用的(★ G-30 之后"禁用"可逆了,所以这里先用公共层禁他)
    QVERIFY(mgr.addUser("carol", "p", Role::Admin));
    QVERIFY(mgr.removeUser("bob"));

    UserManagementDialog dlg(mgr);
    auto* table = dlg.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->columnCount(), 4);

    // 表是按名字升序排的(mgr.listUsers() 的排序)
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("alice"));
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("bob"));
    QCOMPARE(table->item(2, 0)->text(), QStringLiteral("carol"));

    // 角色列显示的是中文名
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("管理员"));
    QCOMPARE(table->item(1, 1)->text(), QStringLiteral("操作员"));

    // "已禁用"列:被禁用的显示"是",启用的是空的
    QCOMPARE(table->item(1, 2)->text(), QStringLiteral("是"));
    QVERIFY(table->item(0, 2)->text().isEmpty());
    QVERIFY(table->item(2, 2)->text().isEmpty());
}

void TestUserManagementDialog::enableButton_exists() {
    UserManager mgr;
    UserManagementDialog dlg(mgr);

    // ★ G-30:没有"启用选中","禁用"就是单向的 —— 账号被禁就等于报废
    QPushButton* enableBtn = findButton(&dlg, QStringLiteral("启用选中"));
    QVERIFY(enableBtn != nullptr);
    QVERIFY(enableBtn->isEnabled());

    QPushButton* deleteBtn = findButton(&dlg, QStringLiteral("禁用选中"));
    QVERIFY(deleteBtn != nullptr);
}

// ★ 判据必须来自公共层(UserManager::canDisableUser)——
//   对话框自己判过一次(拿显示文本比"管理员"),那一次在 2026-11-10 被拆掉了。
void TestUserManagementDialog::judgmentComesFromSharedLayer() {
    const QString code = codeOnly(readDialogSource());
    QVERIFY(!code.isEmpty());   // 源码读不到 ⇒ 这条测试没意义(别让它假绿)
    QVERIFY2(code.contains(QStringLiteral("canDisableUser(")),
             "对话框必须调公共层的 canDisableUser —— 判据不许留在 UI 里");
}

// ★ 不许再拿【显示文本】当判据。
//   显示文本会随语言/显示名变 —— "管理员"三个字变了,护栏就没了。
void TestUserManagementDialog::noDisplayTextComparisonAnyMore() {
    const QString code = codeOnly(readDialogSource());
    QVERIFY(!code.isEmpty());
    QVERIFY2(!code.contains(QStringLiteral("kColRole)->text()")),
             "不许再拿角色列的显示文本比角色 —— 那是翻译/显示名改一次就失效的判据");
    QVERIFY2(!code.contains(QStringLiteral("kColDisabled)->text()")),
             "不许再拿禁用列的显示文本比禁用状态");
}

// ★ 那句"(可后续用密码恢复)"是假的:
//   authenticate() 对 disabled 用户一律拒绝,密码根本救不回来。
//   现在有【启用选中】了,所以这句话必须消失 —— 换成说真话的版本。
void TestUserManagementDialog::falseRecoveryPromise_isGone() {
    const QString code = codeOnly(readDialogSource());
    QVERIFY(!code.isEmpty());
    QVERIFY2(!code.contains(QStringLiteral("可后续用密码恢复")),
             "确认框不能承诺一个做不到的恢复方式(密码救不回被禁用的账号)");
}

void TestUserManagementDialog::enablePathGoesThroughSharedLayer() {
    const QString code = codeOnly(readDialogSource());
    QVERIFY(!code.isEmpty());
    QVERIFY2(code.contains(QStringLiteral("enableUser(")),
             "启用必须走公共层的 enableUser —— 不许在对话框里直接改 disabled 字段");
}

QTEST_MAIN(TestUserManagementDialog)
#include "tst_UserManagementDialog.moc"
