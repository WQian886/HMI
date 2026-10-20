// ============================================================================
// ★ 多 PLC 2b · 阶段 6(2026-10-19):PLC 配置对话框 实现
//
// 设计形状完全照 AlarmConfigDialog(表格 + 行内编辑 + 草稿数组 + 公有只读访问器 +
// objectName 约定),理由与取舍见 PlcConfigDialog.h 顶部。
//
// 本文件里三处"刻意与直觉相反"的地方,改之前先读:
//   ① removeRowFromDraft 不含 QMessageBox(询问是 UI 的职责)
//   ② 删被引用的 PLC 不自动改变量的 plcId(自动改 = 静默写错设备)
//   ③ 应用/存盘是两个分开的按钮,提示文案各自说明对方不做什么
// ============================================================================

#include "plc/PlcConfigDialog.h"

#include "../../Project.h"
#include "../../Variable.h"
#include "../../VariableManager.h"
#include "../auth/CurrentUser.h"
#include "../auth/Permission.h"
#include "../drivers/DriverFactory.h"
#include "../drivers/IPollDriver.h"
#include "../drivers/ProtocolAvailabilityUi.h"
#include "plc/PlcConnectionManager.h"

#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(hmiPlcCfg, "hmi.plc.config")

namespace qwhmi::plc {
namespace {

// ★ 列常量。【不要在中间插列】—— tst_PlcConfigDialog 用这些列号定位,
//   插在中间会让已写好的断言指到别的列上,而测试往往还是绿的。
constexpr int kColId       = 0;   // plc_id
constexpr int kColHost     = 1;
constexpr int kColPort     = 2;
constexpr int kColProtocol = 3;
constexpr int kColTimeout  = 4;
constexpr int kColRetries  = 5;
constexpr int kColPoll     = 6;
constexpr int kColSlaveId  = 7;
constexpr int kColStatus   = 8;
constexpr int kColRefCount = 9;   // ★ 正被几个变量引用
constexpr int kColCount    = 10;

// ★ slave_id 的取值范围。
//   上限 247 是 Modbus 规范给 unit id 的规定值(248..255 保留)。
//   下限【不给 0】:0 是广播地址 —— 写下去全网响应、读没有响应,
//   而它是 TCP 帧 Unit ID 字段(1 字节)完全装得下的合法值,
//   也就是说这个错误一旦存进工程文件,程序【没有任何一层能发现】。
//   所以从 SpinBox 入口就堵死,而不是靠保存时报错。
constexpr int kMinSlaveId = 1;
constexpr int kMaxSlaveId = 247;
// ★ 缺省值。exportDraftToJson 用它判断"用户到底改过没有"。
constexpr int kDefaultSlaveId = 1;

// ★ 协议下拉的候选集。顺序即显示顺序。
//   【为什么把未实现的协议也摆进来】摆出来 + 置灰 + tooltip 讲原因,
//   比干脆不摆要好:用户至少知道"程序认这个协议,只是还没做",
//   而不是以为这里列出的就是全部可能性。
//   不摆的话用户会问"支持 Profinet 吗",问一次是一次的成本。
QStringList kProtocolChoices() {
    return QStringList{QStringLiteral("modbus"),
                       QStringLiteral("s7"),
                       QStringLiteral("eip")};
}

} // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

PlcConfigDialog::PlcConfigDialog(PlcConnectionManager* plcMgr,
                                 VariableManager* varMgr,
                                 QWidget* parent)
    : QDialog(parent), plcMgr_(plcMgr), varMgr_(varMgr) {
    setWindowTitle(tr("PLC 配置"));
    resize(860, 420);

    table_ = new QTableWidget(this);
    infoLabel_ = new QLabel(this);
    refHint_   = new QLabel(this);

    setupTable();
    setupButtons();

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(table_);
    lay->addWidget(infoLabel_);
    lay->addWidget(refHint_);

    auto* box = new QGroupBox(tr("操作"), this);
    auto* bl  = new QGridLayout(box);
    int col = 0;
    for (QPushButton* b : {addBtn_, removeBtn_}) {
        bl->addWidget(b, 0, col++);
    }
    col = 3;
    for (QPushButton* b : {saveBtn_, applyBtn_, closeBtn_}) {
        bl->addWidget(b, 0, col++);
    }
    lay->addWidget(box);

    reloadFromManager();
}

PlcConfigDialog::~PlcConfigDialog() = default;

// ============================================================================
// 表格骨架
// ============================================================================

void PlcConfigDialog::setupTable() {
    // ★ objectName 是给测试找控件用的,不是给用户看的。
    //   没有它测试只能 findChildren 按顺序猜 —— 猜错就断言到别的行上,
    //   而测试还是绿的(同 AlarmConfigDialog::setupTable 的同一段理由)。
    table_->setObjectName(QStringLiteral("plcConfigTable"));
    table_->setColumnCount(kColCount);
    table_->setHorizontalHeaderLabels(QStringList()
        << tr("PLC ID") << tr("地址") << tr("端口") << tr("协议")
        << tr("超时") << tr("重试") << tr("轮询") << tr("从站号")
        << tr("状态") << tr("引用变量"));
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // ★ 拉伸【地址】列,不是最后一列 —— 最后一列是数字(引用变量数),
    //   宽度固定。IP 才是宽度不确定的那个。
    table_->horizontalHeader()->setSectionResizeMode(kColHost, QHeaderView::Stretch);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::AnyKeyPressed);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->verticalHeader()->setVisible(false);   // 行号在这张表里没有意义

    connect(table_, &QTableWidget::itemChanged, this, &PlcConfigDialog::onApplyFieldEdits);
}

void PlcConfigDialog::setupButtons() {
    addBtn_ = new QPushButton(tr("添加(&A)..."), this);
    removeBtn_ = new QPushButton(tr("删除(&D)"), this);
    saveBtn_ = new QPushButton(tr("保存到工程文件(&S)..."), this);
    applyBtn_ = new QPushButton(tr("应用到运行中(&A)"), this);
    closeBtn_ = new QPushButton(tr("关闭(&C)"), this);

    addBtn_->setObjectName(QStringLiteral("plcConfigAddBtn"));
    removeBtn_->setObjectName(QStringLiteral("plcConfigRemoveBtn"));
    saveBtn_->setObjectName(QStringLiteral("plcConfigSaveBtn"));
    applyBtn_->setObjectName(QStringLiteral("plcConfigApplyBtn"));
    closeBtn_->setObjectName(QStringLiteral("plcConfigCloseBtn"));

    // ★ 没有 plcMgr_ 就把"应用到运行中"置灰,而不是让它点了报错。
    //   置灰 + 一句说明(见 updateInfo)比"点了弹一个错误框"更好:
    //   用户一眼就知道这个环境里没有运行中的连接可改。
    applyBtn_->setEnabled(plcMgr_ != nullptr);

    connect(addBtn_, &QPushButton::clicked, this, &PlcConfigDialog::onAdd);
    connect(removeBtn_, &QPushButton::clicked, this, &PlcConfigDialog::onRemove);
    connect(saveBtn_, &QPushButton::clicked, this, &PlcConfigDialog::onSaveToProject);
    connect(applyBtn_, &QPushButton::clicked, this, &PlcConfigDialog::onApplyToRunning);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::reject);
}

// ============================================================================
// 读成草稿
// ============================================================================

void PlcConfigDialog::reloadFromManager() {
    rows_.clear();
    // 数据源是 plcMgr_->lastConfig(),不是 Project::plcs()。
    //   理由见头注释:前者是"用户最后应用过的",后者是"文件里写的"。
    //   两者不一致时,对话框该显示前者 —— 否则用户改完应用了、
    //   再打开对话框看到的却还是旧的,会以为应用没生效。
    if (!plcMgr_) return;

    const QJsonArray cfg = plcMgr_->lastConfig();
    for (const QJsonValue& v : cfg) {
        if (!v.isObject()) continue;   // 不是对象的项静默跳过(和 parseEntry 同口径)
        PlcConfigRow r;
        const QJsonObject o = v.toObject();

        // ★ 键名是 "id",【不是】"plc_id" —— 后者是【变量】那个字段
        //   (JsonSerializer.cpp:30,变量指向哪台 PLC)。
        //   这两个词长得像但在不同层级:plcs[] 里叫 id,variables[] 里叫 plc_id。
        //   写错了不会编译报错,只会让 PlcConnectionManager::parseEntry
        //   报"缺少 id"然后整台 rejected —— 而界面状态列显示"不在线",
        //   用户完全看不出是自己(或代码)把键名写错了。
        r.id       = o.value(QStringLiteral("id")).toString();
        r.host     = o.value(QStringLiteral("host")).toString();
        r.port     = o.value(QStringLiteral("port")).toInt(502);
        // 协议必须归一:文件里可能写 "Modbus"/"modbus"/"modbus_tcp",
        //   而 sameConnection 比的是归一后的值。不归一的话,
        //   只把协议字段的写法改一下就会被判成"变了" → 白白断线重连一次
        //   (理由见 PlcConnectionManager.cpp:28-33)。
        r.protocol = qwhmi::drivers::DriverFactory::normalizeProtocolName(
                         o.value(QStringLiteral("protocol")).toString(QStringLiteral("modbus")));
        // ★ 三个通讯参数在【嵌套的 modbus 对象】里,不是平铺在 plc 这一层
        //   (见 Project::defaultPlcFromTcp:133-137)。
        //   平铺读会永远读到缺省值 —— 而界面上显示的是真值,
        //   用户改了它也说"已应用",实际一个字都没存进去。
        const QJsonObject modbus = o.value(QStringLiteral("modbus")).toObject();
        r.timeoutMs  = modbus.value(QStringLiteral("timeout_ms")).toInt(1000);
        r.maxRetries = modbus.value(QStringLiteral("max_retries")).toInt(5);
        r.pollMs     = modbus.value(QStringLiteral("poll_interval_ms")).toInt(1000);
        // ★ 缺省 1,与 PlcConnectionManager::parseEntry:95 的 toInt(1) 一致。
        //   【不因为"顺手"就把它写回去】—— 工程文件里没写的字段
        //   不该因为打开一次对话框就冒出来,那会在用户的版本控制里
        //   冒出一片"我什么都没改"的 diff(哨兵用例 slaveId_absentInFile_
        //   isNotGeneratedOnSave 守的就是这件事)。
        //
        //   ★ 记住"原来文件里到底有没有这个键",原样带到 export 里去 ——
        //   原来没有 → 存回去还是没有。见 exportDraftToJson 的说明。
        const QJsonValue sid = o.value(QStringLiteral("slave_id"));
        r.slaveId = sid.isUndefined() || sid.isNull() ? 1 : sid.toInt(1);
        // hadSlaveId_ 记的是"用户文件里原来有没有这个键",不是"值是不是缺省"。
        //   值是 1 但键存在 ≠ 键不存在:前者是用户手填过 1,后者是没填过。
        //   把前者存成后者 = 替用户改了他明确写下的东西。
        r.hadSlaveIdKey = !sid.isUndefined() && !sid.isNull();

        // ★ 剩下用户可能有、代码不认识的字段原样带着。
        r.extra = o;
        for (const QString& known : {QStringLiteral("id"), QStringLiteral("host"),
                                     QStringLiteral("port"), QStringLiteral("protocol"),
                                     QStringLiteral("modbus"), QStringLiteral("slave_id")}) {
            r.extra.remove(known);
        }
        // modbus 里我们只认三个键,里面可能还有别的 —— 一起原样带走。
        if (o.contains(QStringLiteral("modbus"))) {
            QJsonObject rest = modbus;
            rest.remove(QStringLiteral("timeout_ms"));
            rest.remove(QStringLiteral("max_retries"));
            rest.remove(QStringLiteral("poll_interval_ms"));
            if (!rest.isEmpty()) r.extraModbus = rest;
        }

        r.refCount = countVariablesUsing(varMgr_, r.id);
        rows_.append(r);
    }
    fillTable();
}

void PlcConfigDialog::fillTable() {
    suppressItemChanged_ = true;   // ★ 见头注释:不挡的话填表会触发回写
    table_->setRowCount(rows_.size());

    for (int i = 0; i < rows_.size(); ++i) {
        const PlcConfigRow& r = rows_.at(i);

        auto put = [&](int col, const QString& text, bool editable) {
            auto* it = new QTableWidgetItem(text);
            if (editable) it->setFlags(it->flags() | Qt::ItemIsEditable);
            table_->setItem(i, col, it);
        };

        put(kColId,       r.id,       true);
        put(kColHost,     r.host,     true);
        put(kColPort,     QString::number(r.port),     true);
        put(kColTimeout,  QString::number(r.timeoutMs),  true);
        put(kColRetries,  QString::number(r.maxRetries), true);
        put(kColPoll,     QString::number(r.pollMs),     true);

        // ---- 协议列:QComboBox,不是可编辑文本格 ----
        //   ★【为什么不用文本格】用户在这里敲 "modbus_tcp" 是合法输入,
        //   但敲 "Modbus TCP"(带空格)就建不出 driver,状态列显示
        //   "不在线(协议不支持)" —— 而他输入的地方【长得像个能随便填的格子】,
        //   没有任何提示说这里只能选。
        //   下拉框把可选集合摆在明面上,敲不出来。
        //   ★ 置灰逻辑【复用】applyProtocolAvailabilityStyling(已由
        //   tst_ProtocolComboGrey 钉住),不重写一份 —— 重写的那份
        //   改了没人会发现,正是不抽这个文件的原因(见 ProtocolAvailabilityUi.h)。
        auto* proto = new QComboBox(table_);
        proto->addItems(kProtocolChoices());
        qwhmi::drivers::applyProtocolAvailabilityStyling(proto);
        const int pidx = qwhmi::drivers::findProtocolItemIndex(
                              kProtocolChoices(), r.protocol);
        // 找不到(工程文件里写了个本程序没有的协议,比如 "profinet")时
        //   【照原样加进下拉框并选中它】,而不是悄悄换成 modbus ——
        //   用户打开对话框是为了看配置,看见的必须是文件里真正写着的那个协议名。
        if (pidx >= 0) {
            proto->setCurrentIndex(pidx);
        } else {
            proto->addItem(r.protocol);
            proto->setCurrentIndex(proto->count() - 1);
        }
        proto->setProperty("plcRow", i);
        table_->setCellWidget(i, kColProtocol, proto);
        connect(proto, &QComboBox::currentIndexChanged, this,
                &PlcConfigDialog::onProtocolComboChanged);

        // ---- 从站号列:QSpinBox,范围锁死 1..247 ----
        //   ★ 计划里写的就是 SpinBox,不是文本格。
        //   SpinBox 的 setRange 让 0 和 300 【根本敲不进去】——
        //   文本格只能事后夹,用户会先看见自己填的错值再被改掉,
        //   而 SpinBox 的上下箭头到 247 就停,0 根本到不了。
        //   0 是 Modbus 广播地址:写下去全网响应、读永远没有响应,
        //   而它是 Unit ID 那个 1 字节字段装得下的合法值,
        //   也就是说存进工程文件之后【没有任何一层能发现】(见 kMinSlaveId)。
        auto* slave = new QSpinBox(table_);
        slave->setRange(kMinSlaveId, kMaxSlaveId);
        slave->setValue(r.slaveId);
        slave->setProperty("plcRow", i);
        table_->setCellWidget(i, kColSlaveId, slave);
        connect(slave, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &PlcConfigDialog::onSlaveIdSpinChanged);

        // ★ 状态列:在线/连接中/不在线。这不是装饰 —— 用户配了 3 台却只连上 2 台时,
        //   【看不出少了哪台】正是这个界面前最大的坑(见头注释)。
        //   不在这里显示,用户只能去翻日志。
        //
        //   ★★ 【原来这里只判 driverFor(r.id) 非空就写"在线" —— 那是撒谎】。
        //   createAndConnect 里 DriverFactory::create 成功就【立刻】把 driver
        //   放进 entries_,而 connectTo 是【异步】的:地址写错、DNS 解析不了、
        //   对端没上电,TCP 连接全都还没建立成功,但状态列已经绿了。
        //   于是这个专为"告诉我哪台没连上"而做的列,恰恰在最需要它的场景下
        //   骗人 —— 三台全绿,而现场一台都没通。
        //   必须问【driver 自己的 isConnected()】,那才是 socket 的真实状态。
        QString status;
        if (!plcMgr_) {
            status = tr("(无运行中的连接)");
        } else {
            auto* d = plcMgr_->driverFor(r.id);
            if (!d) {
                // 建不出 driver = 协议不支持。与"建出了但连不上"是两回事,
                // 用户要做的也不同(前者改协议,后者查网线/IP)。
                status = tr("不在线(协议不支持)");
            } else if (d->isConnected()) {
                status = tr("在线");
            } else {
                // 建出了 driver 但 socket 没连上,可能正在退避重连。
                // 说"连接中"而不是"不在线":TcpClient 的重连退避 kMaxRetries=5,
                // 耗尽之后才真的没救了,而那时候用户早就看到"连接中"了。
                status = tr("连接中");
            }
        }
        put(kColStatus, status, false);

        put(kColRefCount, QString::number(r.refCount), false);
    }
    suppressItemChanged_ = false;
    updateInfo();
}

void PlcConfigDialog::updateInfo() {
    infoLabel_->setText(tr("共 %1 台 PLC。地址/端口/协议/从站号改了会立即重连;"
                           "超时/重试/轮询要重新连接才生效。").arg(rows_.size()));

    // ★ "引用变量"这一列是什么意思,必须写在界面上。
    //   它决定删除那一行时会不会把别人的变量悬空 —— 不解释的话
    //   用户会把它当成一个可有可无的统计数字。
    refHint_->setText(tr("「引用变量」= 有多少个变量指向这台 PLC。"
                         "删除一台被引用的 PLC 时,那些变量会变成无连接状态"
                         "(【不会】被自动改到别的 PLC 上)——自动改等于把数据"
                         "悄悄写进另一台机器。"));
}

int PlcConfigDialog::countVariablesUsing(const VariableManager* varMgr, const QString& plcId) {
    if (!varMgr || plcId.isEmpty()) return 0;
    int n = 0;
    for (Variable* v : varMgr->allVariables()) {
        if (v && v->plcId() == plcId) ++n;
    }
    return n;
}

// ============================================================================
// 表格编辑 → 草稿
// ============================================================================

void PlcConfigDialog::onApplyFieldEdits(QTableWidgetItem* item) {
    if (suppressItemChanged_ || !item) return;
    const int row = item->row();
    if (row < 0 || row >= rows_.size()) return;

    PlcConfigRow& r = rows_[row];
    const QString text = item->text().trimmed();
    // ★ 解析失败【不改草稿】,只把这格打回原值。
    //   改草稿 = 存一个解析不出来的东西,而 validateDraft 只查 id/host/port,
    //   slave_id 解析失败会被静默存成 0 —— 广播地址,存进去就没人能发现。
    auto toInt = [&](const QString& s, int fallback) {
        bool ok = false;
        const int v = s.toInt(&ok);
        return ok ? v : fallback;
    };

    switch (item->column()) {
        case kColId:       r.id       = text; break;
        case kColHost:     r.host     = text; break;
        case kColPort:     r.port     = toInt(text, r.port); break;
        case kColTimeout:  r.timeoutMs  = toInt(text, r.timeoutMs);  break;
        case kColRetries:  r.maxRetries = toInt(text, r.maxRetries); break;
        case kColPoll:     r.pollMs     = toInt(text, r.pollMs);     break;
        // 协议和从站号是 cellWidget(QComboBox / QSpinBox),不是文本格 ——
        //   那一列没有 QTableWidgetItem,itemChanged 根本不会为它们发。
        //   走 onProtocolComboChanged / onSlaveIdSpinChanged。
        default: return;   // 状态/引用列不可编辑,不该走到这里
    }
    updateInfo();
}

void PlcConfigDialog::onProtocolComboChanged(int) {
    auto* combo = qobject_cast<QComboBox*>(sender());
    if (!combo || suppressItemChanged_) return;
    const int row = combo->property("plcRow").toInt();
    if (row < 0 || row >= rows_.size()) return;
    // ★ 归一:下拉里的显示名可能和协议 ID 不是同一个词
    //   ("EtherNet/IP" → "eip")。不归一的话只把写法改一下就会被
    //   PlcConnectionManager::sameConnection 判成"变了" → 白白重连一次。
    rows_[row].protocol = qwhmi::drivers::DriverFactory::normalizeProtocolName(
                             combo->currentText());
    updateInfo();
}

void PlcConfigDialog::onSlaveIdSpinChanged(int value) {
    auto* spin = qobject_cast<QSpinBox*>(sender());
    if (!spin || suppressItemChanged_) return;
    const int row = spin->property("plcRow").toInt();
    if (row < 0 || row >= rows_.size()) return;
    // ★【用户一旦碰过这个 SpinBox,就说明他关心从站号】——
    //   哪怕他最后填回了 1,也照样写进导出结果。
    //   否则用户特意去确认/设置过从站号,存回去文件里却还是没这个键,
    //   下次打开还是 1,而他明明改过。
    rows_[row].slaveId = qBound(kMinSlaveId, value, kMaxSlaveId);
    rows_[row].hadSlaveIdKey = true;
}

// ============================================================================
// 增 / 删
// ============================================================================

QString PlcConfigDialog::nextFreeDraftId() const {
    // ★ 顺延而不是找空位 —— 【原来这里写的是"从 plc1 起找第一个没用过的"】,
    //   plc1/plc3 都有、plc2 空着时会返回 plc2,和它自己上面那句注释
    //   ("叫 plc4 更符合'又加了一台'")正好相反。
    //   那种编号法的问题是:用户配 plc1/plc3 是因为拆掉了中间那台,
    //   界面再补一个 plc2,他看到的是"这机器一直在啊,只是没显示",
    //   于是去变量表里排查一个不存在的连接问题。
    //
    //   只认严格形如 plcN 的 id(带前缀、后面【全是】数字)。
    //   用户自己起的名字("东侧泵站")不参与,maxN 保持 0 → 新行叫 plc1,
    //   而不是去猜一个"东侧泵站2"。
    int maxN = 0;
    for (const PlcConfigRow& r : rows_) {
        const QString kPrefix = QStringLiteral("plc");
        if (!r.id.startsWith(kPrefix)) continue;
        bool ok = false;
        const int n = r.id.mid(kPrefix.size()).toInt(&ok);
        if (ok && n > maxN) maxN = n;
    }
    return QStringLiteral("plc%1").arg(maxN + 1);
}

int PlcConfigDialog::addRowToDraft() {
    PlcConfigRow r;
    r.id     = nextFreeDraftId();
    r.refCount = 0;
    rows_.append(r);
    fillTable();
    const int idx = rows_.size() - 1;
    if (idx >= 0) table_->selectRow(idx);
    return idx;
}

void PlcConfigDialog::onAdd() {
    const int row = addRowToDraft();
    if (row < 0) return;
    // 新行只有 id 有值,host 是空的 —— 让光标直接落在最需要填的那一格。
    if (auto* it = table_->item(row, kColHost)) table_->editItem(it);
}

bool PlcConfigDialog::removeRowFromDraft(int row) {
    // ★ 本方法【不弹任何框】。理由见头注释:询问是 UI 的职责。
    //   把 QMessageBox 写在这里,offscreen 测试会永久阻塞(实测卡满 300s),
    //   于是最危险的这条路径反而一条测试都写不了。
    if (row < 0 || row >= rows_.size()) return false;
    rows_.removeAt(row);
    fillTable();
    return true;
}

void PlcConfigDialog::onRemove() {
    const int row = table_->currentRow();
    if (row < 0 || row >= rows_.size()) {
        QMessageBox::information(this, tr("先选一行"), tr("请先在表格里选中要删除的 PLC。"));
        return;
    }
    const PlcConfigRow& r = rows_.at(row);

    // ★★★ 这里是最关键的一段,别"顺手简化"掉 ★★★
    //   删除一台仍被变量引用的 PLC 时【必须】问清楚后果,
    //   而且【绝不能】自动把那些变量改到别的 PLC 上。
    //
    //   自动改(比如全改成 plc1)看着是"善意的兜底",实际是最危险的实现:
    //   原来写向 plc2 的压力值/阀门命令会【突然开始写进一号机】,
    //   界面上没有任何提示 —— 这正是多 PLC 2b 要消灭的那个
    //   "静默写错设备"的 bug,换个入口又回来了一次。
    //
    //   正确做法是明说后果让用户自己决定。变量变成"无连接状态"是【看得见】的
    //   (值停在 Bad、变量列表里那个 plcId 找不到任何连接),
    //   而静默改指向是看不见的。
    QString msg = tr("确定删除 PLC \"%1\"(%2:%3)吗?").arg(r.id, r.host).arg(r.port);
    if (r.refCount > 0) {
        // ★★★ 这里原来三个占位符【全都写成 %1】,而 QString::arg() 是
        //   替换【编号最小的那个、且替换全部出现处】——
        //   所以 .arg(r.refCount) 一出手就把三处 %1 全填成了"变量个数",
        //   后面那句 .arg(r.id) 找不到任何占位符可填,Qt 在 stderr 打
        //   "QString::arg: Argument missing"(2026-10-19 补测试时才看见)。
        //   用户实际看到的字是:
        //     原本发给「3」的数据悄悄发给另一台机器
        //   —— 那个 3 是【变量个数】,不是 PLC 的 id。
        //   ★ 为什么这句错得特别危险:它正是本对话框最要紧的一句警告,
        //     而被替换掉的恰好是"哪一台 PLC"—— 用户读到的设备是错的,
        //     错得毫无痕迹(界面上没有任何地方提示这里出过岔子)。
        //   改法:PLC id 用 %2,和变量个数 %1 区分开。
        msg += QStringLiteral("\n\n")
             + tr("★ 这一台正被 %1 个变量引用。删除后那 %1 个变量会变成"
                  "【无连接状态】(值停在 Bad),它们不会再读写任何 PLC。\n\n"
                  "程序【不会】自动把它们改到别的 PLC 上 —— 那样等于把"
                  "原本发给 \"%2\" 的数据悄悄发给另一台机器。\n\n"
                  "如果那些变量还要用,请到「数据 → 变量配置」里手工改它们的"
                  " PLC ID。")
                   .arg(r.refCount).arg(r.id);
    }
    // ★ 2026-10-19:这里原来还有一句无条件追加的
    //   "(它们在变量表里的 PLC ID 不会被改动。)",删掉了。两个原因:
    //   ① 同义反复 —— 上面那段已经用整句话把"不会自动改指向"讲完了,
    //      而且讲得更重("悄悄发给另一台机器")。再说一遍是稀释,不是强调。
    //   ② ★ 它是无条件追加的,所以【一个变量都没被引用时也会出现】,
    //      而那时候"它们"指代的是不存在的对象 —— 用户会花时间找
    //      "到底哪几个变量的 PLC ID 没被动过"。
    //      一句在无关场景里凭空出现的话,比不说更容易让人怀疑哪里出了问题。

    if (QMessageBox::question(this, tr("删除 PLC"), msg) != QMessageBox::Yes) return;
    removeRowFromDraft(row);
}

// ============================================================================
// 只读访问器(验收面)
// ============================================================================

int     PlcConfigDialog::rowCount() const  { return static_cast<int>(rows_.size()); }
QString PlcConfigDialog::idAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).id : QString();
}
QString PlcConfigDialog::hostAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).host : QString();
}
int PlcConfigDialog::portAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).port : 0;
}
QString PlcConfigDialog::protocolAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).protocol : QString();
}
int PlcConfigDialog::slaveIdAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).slaveId : 0;
}
int PlcConfigDialog::refCountAt(int row) const {
    return (row >= 0 && row < rows_.size()) ? rows_.at(row).refCount : 0;
}

// ============================================================================
// 校验 / 导出
// ============================================================================

bool PlcConfigDialog::validateDraft(QString* errorOut) const {
    auto fail = [&](const QString& msg) { if (errorOut) *errorOut = msg; return false; };

    QSet<QString> seen;
    for (int i = 0; i < rows_.size(); ++i) {
        const PlcConfigRow& r = rows_.at(i);
        const QString at = tr("第 %1 行").arg(i + 1);

        if (r.id.isEmpty()) {
            return fail(at + tr(":PLC ID 不能为空 —— 变量靠它找到这台 PLC"));
        }
        // ★ 重名必须拦:id 是变量的引用目标,重复的话加载工程时后者覆盖前者,
        //   且【不报错】。与 AlarmConfigDialog 对报警名的检查同一条理由。
        if (seen.contains(r.id)) {
            return fail(at + tr(":PLC ID \"%1\" 重复 —— 必须唯一").arg(r.id));
        }
        seen.insert(r.id);

        if (r.host.isEmpty()) {
            return fail(at + tr("(%1):地址不能为空").arg(r.id));
        }
        if (r.port <= 0 || r.port > 65535) {
            return fail(at + tr("(%1):端口 %2 越界(1..65535)").arg(r.id).arg(r.port));
        }
        // ★ slave_id 单独查一遍,哪怕 SpinBox 已经夹过。
        //   SpinBox 夹的是"用户在这一行里敲进去的值",
        //   而程序化路径(加载文件 / 测试直接构造草稿)不走 SpinBox。
        //   只靠界面兜底 = 换个入口就漏。
        if (r.slaveId < kMinSlaveId || r.slaveId > kMaxSlaveId) {
            return fail(at + tr("(%1):从站号 %2 非法(%3..%4)")
                            .arg(r.id).arg(r.slaveId).arg(kMinSlaveId).arg(kMaxSlaveId));
        }
    }
    if (errorOut) errorOut->clear();
    return true;
}

QJsonArray PlcConfigDialog::exportDraftToJson() const {
    QJsonArray arr;
    for (const PlcConfigRow& r : rows_) {
        // ★ 从 extra 起头,不是从空对象起头。
        //   extra 装的是"用户文件里有、代码不认识的字段"。
        //   从空对象起头 = 那些字段静默消失 —— 与
        //   PlcConnectionManager::lastConfig_ 存原始数组是同一条理由。
        QJsonObject o = r.extra;
        // ★ 键是 "id" 不是 "plc_id",见 reloadFromManager 里那段注释。
        o[QStringLiteral("id")]       = r.id;
        o[QStringLiteral("host")]     = r.host;
        o[QStringLiteral("port")]     = r.port;
        o[QStringLiteral("protocol")] = r.protocol;

        // ★ modbus 三个参数是【嵌套】的(Project::defaultPlcFromTcp:133-137)。
        //   先起头自 extraModbus(modbus 对象里我们不认的键),再填三个认识的。
        QJsonObject modbus = r.extraModbus;
        modbus[QStringLiteral("timeout_ms")]       = r.timeoutMs;
        modbus[QStringLiteral("max_retries")]      = r.maxRetries;
        modbus[QStringLiteral("poll_interval_ms")] = r.pollMs;
        o[QStringLiteral("modbus")] = modbus;

        // ★★★ 整个对话框最容易造成"脏 diff"的地方 ★★★
        //   slave_id 【只在用户原来就填过、或者本次会话真的改过】时才写。
        //   为什么:一份从没配过 slave_id 的工程(全仓库默认),
        //   用户只是打开对话框看了一眼然后关掉 —— 如果这里无条件写 slave_id:1,
        //   那份文件就会凭空多出一个键,git diff 里显示"他改了一行",
        //   而他一个键都没碰过。
        //   现场工程师天天对着 git blame,这种噪声比功能缺失更让人火大。
        //   哨兵用例 slaveId_absentInFile_isNotGeneratedOnSave 守的就是这条,
        //   不过它守的是 saveToFile;这里守的是"对话框往返也不产生"。
        if (r.hadSlaveIdKey || r.slaveId != kDefaultSlaveId) {
            o[QStringLiteral("slave_id")] = r.slaveId;
        }

        arr.append(o);
    }
    return arr;
}

// ============================================================================
// 三个动作:存盘 / 应用 / 关闭
// ============================================================================

bool PlcConfigDialog::saveDraftToProjectFile(const QString& path, QString* errorOut) {
    if (path.isEmpty()) {
        if (errorOut) *errorOut = tr("没有指定工程文件路径");
        return false;
    }
    // 坏草稿绝不落盘 —— 落下去的不是"PLC 配错了",而是"工程打不开了"
    // (或者更糟:一台 host 写错的 PLC 存进去,打开工程时它静默不连)。
    QString err;
    if (!validateDraft(&err)) {
        if (errorOut) *errorOut = err;
        return false;
    }
    return Project::replacePlcsInFile(path, exportDraftToJson(), errorOut);
}

void PlcConfigDialog::onSaveToProject() {
    // QFileDialog 在 offscreen 下没法交互,真正的落盘走
    // saveDraftToProjectFile(路径),它才是被测的那一层。
    const QString path = QFileDialog::getSaveFileName(
        this, tr("把 PLC 配置存进工程文件"), QString(),
        tr("HMI 工程文件 (*.json);;所有文件 (*.*)"));
    if (path.isEmpty()) return;   // 用户按取消

    QString err;
    if (saveDraftToProjectFile(path, &err)) {
        // ★ 必须明说"运行中的连接不受影响"。
        //   这一句话的存在理由与 AlarmConfigDialog::onSaveToProject 完全相同:
        //   表格看着是"PLC 的配置",用户合理地以为保存了就生效了,
        //   而它只写进了文件。
        QMessageBox::information(
            this, tr("已保存"),
            tr("PLC 配置已写入:\n%1\n\n"
               "只替换了 plcs 段,变量/画面/报警都没动。\n\n"
               "注意:运行中的连接【不受影响】。要让新配置生效,"
               "请用「应用到运行中」,或在主界面重新载入这个工程文件。").arg(path));
        qCDebug(hmiPlcCfg) << "[阶段6] PLC 草稿已写入工程文件:" << path
                           << "共" << rows_.size() << "台";
    } else {
        QMessageBox::warning(this, tr("保存失败"), err);
        qCWarning(hmiPlcCfg) << "[阶段6] PLC 配置保存失败:" << err;
    }
}

bool PlcConfigDialog::applyDraftToRunning(QString* errorOut) {
    if (!plcMgr_) {
        if (errorOut) *errorOut = tr("没有连接到运行中的 PLC 管理器,无法应用");
        return false;
    }
    // ★ 同样先全查再全改 —— 与 AlarmConfigDialog::applyDraftToManager 同一条理由:
    //   一边查一边改,改到第 5 条才发现第 3 条有问题,前 4 条已经重连了,
    //   而用户看到的是"应用失败",他不知道到底哪些生效了。
    QString err;
    if (!validateDraft(&err)) {
        if (errorOut) *errorOut = err;
        return false;
    }

    const PlcConnectionManager::ApplyResult res = plcMgr_->applyConfig(exportDraftToJson());
    qCDebug(hmiPlcCfg) << "[阶段6] 已应用到运行中 —— 新增" << res.added
                       << "删除" << res.removed << "重连" << res.reconnected
                       << "未变" << res.unchanged << "被拒" << res.rejected;

    // ★ rejected 【不】当成失败返回。理由见 Project.h setPlcApplier 的注释:
    //   一台填错不挡整个工程,其余 PLC 照常上线。
    //   但必须【说出来】 —— 用户配了 3 台只连上 2 台却没人告诉他少了哪台,
    //   正是这个界面要消灭的坑。
    if (!res.rejected.isEmpty()) {
        if (errorOut) {
            *errorOut = tr("以下 PLC 没连上(协议不支持或地址不可达),其余已生效:\n%1")
                            .arg(res.rejected.join(QStringLiteral(", ")));
        }
        return false;
    }
    if (errorOut) errorOut->clear();
    return true;
}

void PlcConfigDialog::onApplyToRunning() {
    QString err;
    if (applyDraftToRunning(&err)) {
        fillTable();   // 状态列要跟着变 —— 应用完立刻能看到哪几台在线了
        QMessageBox::information(
            this, tr("已应用"),
            tr("PLC 配置已应用到运行中。\n\n"
               "没有变化的 PLC 【不会】重连 —— 现场设备不会因为打开一次"
               "这个对话框就抖一下。\n\n"
               "注意:这些改动【没有】存进工程文件。要留存请另外点"
               "「保存到工程文件」。"));
    } else {
        QMessageBox::warning(this, tr("应用失败"), err);
        qCWarning(hmiPlcCfg) << "[阶段6] 应用到运行中失败:" << err;
    }
}

int PlcConfigDialog::exec() {
    // ★ EditProject —— 与 AlarmConfigDialog 同一条。
    //   改 PLC 配置就是改工程行为,而且改完【立刻重连现场设备】,
    //   权限要求只会比报警配置更高,不会更低。
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                                      qwhmi::auth::Permission::EditProject)) {
        QMessageBox::warning(this, tr("权限不足"),
                             tr("只有工程师/管理员才能修改 PLC 配置"));
        return QDialog::Rejected;
    }
    reloadFromManager();
    return QDialog::exec();
}

} // namespace qwhmi::plc
