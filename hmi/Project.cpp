// 阶段 6.2(2026-08-07):Project 类实现
//   ★ E.3(2026-09-21):JSON 存读扩展
//   - schema_version 2 → 3 + migrateV2ToV3
//   - 加 controls JSON 序列化(走 ControlBase::propertiesToJson 反射)
//   - 加 controlsLoaded 信号

#include "Project.h"
#include "VariableManager.h"
#include "Variable.h"
#include "AlarmManager.h"
#include "Alarm.h"
#include "ActionManager.h"
#include "ThresholdAlarmChecker.h"   // ★ 6.3 需要:loadFromFile 构造 Checker
#include "ThresholdAlarmChecker.h"   // ★ 6.2 需要:dynamic_cast 用
#include "TcpClient.h"
#include "JsonSerializer.h"
#include "logging/Logging.h"
// ★ E.3:controls JSON 序列化走反射
#include "widgets/base/ControlBase.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>
#include <QSet>   // ★ F4-c:validateAlarmsArray 查重名
#include <QDebug>

namespace qwhmi::widgets { class ControlBase; }

Project::Project(VariableManager* varMgr,
                 AlarmManager* alarmMgr,
                 ActionManager* actionMgr,
                 PollScheduler* pollScheduler,
                 TcpClient* tcp,
                 QObject* parent)
    : QObject(parent),
      varMgr_(varMgr),
      alarmMgr_(alarmMgr),
      actionMgr_(actionMgr),
      pollScheduler_(pollScheduler),
      tcp_(tcp) {}

Project::~Project() = default;

// =============================================================================
// ★ 2026-10-16:PLC 配置存取(多 PLC 2a)
// =============================================================================

namespace {

// 单台 PLC 配置的合法性检查。
// ★【为什么只查这三样,别的都不查】
//   id   —— 空 id 会让变量的 plc_id 对不上任何一台,那些变量从此静默不刷新
//          (PollScheduler 按 plcId 查 driver,查不到就跳过,而跳过【不报错】)
//   host —— 空 host 连不上,表现是"一直重连",看不出是配置问题
//   port —— 0 或越界会让 connectTo 拿到非法端口
//   刻意【不】查 protocol 是否受支持:那属于 2b 的判断(那时每台 PLC 才真建连接),
//   现在查了也无处可去,只会把"配了个还没实现的协议"变成加载失败。
int validatePlcEntry(const QJsonObject& plc, int index, QJsonArray* outOk) {
    const QString id = plc.value("id").toString();
    if (id.trimmed().isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("[Project] plcs[%1] 缺少 \"id\",已跳过 —— "
                              "没有 id 的配置无法和变量的 plc_id 对应,留着等于噪声。")
                   .arg(index);
        return 0;
    }
    const QString host = plc.value("host").toString();
    const int port = plc.value("port").toInt(-1);
    if (host.trimmed().isEmpty() || port <= 0 || port > 65535) {
        qWarning().noquote()
            << QStringLiteral("[Project] plcs[\"%1\"] 的 host/port 不合法(host=\"%2\" port=%3),已跳过 —— "
                              "这台的地址填错了。请改成实际 PLC 的 IP 和端口。")
                   .arg(id, host)
                   .arg(port);
        return 0;
    }
    if (outOk) outOk->append(plc);
    return 1;
}

} // namespace

int Project::loadPlcsFromJson(const QJsonObject& root) {
    plcs_ = QJsonArray();      // QJsonArray 没有 clear(),只有赋值
    const QJsonArray arr = root.value("plcs").toArray();
    if (arr.isEmpty()) {
        // 没有 plcs 键是【合法】的:v1 工程走 migrateV1ToV2 之后才会有,
        // 而 migrate 失败会在调用方那里报 loadError。走到这里说明已经是新格式,
        // 只是这个工程恰好没配 PLC —— 不该因此报错。
        return 0;
    }
    for (int i = 0; i < arr.size(); ++i) {
        validatePlcEntry(arr.at(i).toObject(), i, &plcs_);
    }

    // ★ 这里【故意】不做的事,记下来免得下一个人以为漏了:
    //   本函数【只负责读和校验】,不建任何连接。
    //   建连接是第 8 步的事(交给 plcApplier_ 指定的 PlcConnectionManager)。
    //   —— 2a 之后的一段时间里这里是"读进来但没人连",日志里明写着"2b 尚未实现";
    //   2b 起那条日志已删,因为它变成了骗人的话。
    qCDebug(hmiComm) << "[Project] PLC 配置读入" << plcs_.size()
                      << "台(共" << arr.size() << "项,已剔除" << (arr.size() - plcs_.size()) << "项)。";
    return plcs_.size();
}

int Project::setPlcs(const QJsonArray& plcs) {
    QJsonArray ok;
    for (int i = 0; i < plcs.size(); ++i) {
        validatePlcEntry(plcs.at(i).toObject(), i, &ok);
    }
    plcs_ = ok;
    return plcs_.size();
}

QJsonObject Project::defaultPlcFromTcp(TcpClient* tcp) {
    // 抽出来是因为原先这段逻辑内联在 saveToFile 里,而 2a 之后
    // saveToFile 有三条不同的路(没配过 / 配过 N 台 / 退化成单台默认),
    // 三处都要用同一份默认值 —— 复制三遍必然走偏。
    QJsonObject plc;
    plc["id"]       = QStringLiteral("plc1");
    plc["protocol"] = QStringLiteral("modbus_tcp");
    if (tcp) {
        plc["host"] = tcp->host();
        plc["port"] = tcp->port();
    } else {
        plc["host"] = QStringLiteral("127.0.0.1");
        plc["port"] = 502;
    }
    QJsonObject modbus;
    modbus["timeout_ms"]       = TcpClient::kConnectTimeoutMs;
    modbus["max_retries"]      = TcpClient::kMaxRetries;
    modbus["poll_interval_ms"] = 100;
    plc["modbus"] = modbus;
    return plc;
}

// ===== E.3:静态工具:控件列表 ↔ JSON 数组 =====

// ★ 反射遍历每个控件 → QJsonObject(走 ControlBase::propertiesToJson)+ type 字段
QJsonArray Project::controlsToJson(const QList<qwhmi::widgets::ControlBase*>& controls) {
    QJsonArray arr;
    for (const auto* c : controls) {
        if (!c) continue;
        QJsonObject obj = c->propertiesToJson();
        // ★ E.3:type 字段存注册表短名("Bar" 而不是 "BarControl"),
        //   loadControlsFromJson 用 ControlBase::create(type) 反查注册表
        obj["type"] = qwhmi::widgets::ControlBase::registryKeyFor(c->metaObject());
        arr.append(obj);
    }
    return arr;
}

// ★ 反向:JSON 数组 → 控件列表
//   factory 决定 type → ControlBase* 的映射(由调用方决定所有权;本函数不 delete 返回的指针)
//   失败 type(unknown)跳过 + log warning,继续处理后面的
int Project::loadControlsFromJson(const QJsonArray& arr,
                                  qwhmi::widgets::ControlBase* (*factory)(const QString& typeName),
                                  QList<qwhmi::widgets::ControlBase*>& outControls) {
    outControls.clear();
    int okCount = 0;
    for (const QJsonValue& v : arr) {
        QJsonObject obj = v.toObject();
        QString typeName = obj["type"].toString();
        if (typeName.isEmpty()) {
            qWarning() << "[Project] loadControlsFromJson:控件缺 type 字段,跳过";
            continue;
        }
        qwhmi::widgets::ControlBase* c = factory(typeName);
        if (!c) {
            qWarning() << "[Project] loadControlsFromJson:未知控件类型" << typeName << ",跳过";
            continue;
        }
        // ★ 反射写回所有属性
        c->propertiesFromJson(obj);
        outControls.append(c);
        okCount++;
    }
    return okCount;
}

// ===== 私有 saveRootToFile:把 root QJsonObject 写文件(原子写)=====
bool Project::saveRootToFile(const QString& path, const QJsonObject& root) {
    QJsonDocument doc(root);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit loadError(QString("打开文件失败: %1").arg(f.errorString()));
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        emit loadError(QString("写入文件失败: %1").arg(f.errorString()));
        return false;
    }
    return true;
}

// ============================================================================
// ★ F4-c(2026-10-08):只替换工程文件里的 alarms 段
//
// 【这个函数存在的唯一理由】
//   草稿要落地,但 saveToFile 会把运行中的 AlarmManager 重新序列化一遍,
//   覆盖掉草稿。所以只能"读原文件 → 只换 alarms → 写回"。
//
// 【为什么不顺手校验 variable 存在性】
//   loadRootFromFile:211 会在 variable 找不到时让【整个文件】加载失败。
//   这一层的检查需要 VariableManager,而这里是 static。
//   所以拆成两层:validateAlarmsArray(纯 JSON,能单测)
//   + 调用方在有 varMgr 的地方补一次存在性检查。
// ============================================================================

bool Project::validateAlarmsArray(const QJsonArray& alarms, QString* errorOut) {
    auto fail = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
        return false;
    };

    QSet<QString> seen;
    for (int i = 0; i < alarms.size(); ++i) {
        const QJsonObject a = alarms.at(i).toObject();
        const QString name = a["name"].toString();

        // ① 名字不能空 —— AlarmManager::addChecker 拒收空名(AlarmManager.cpp:13),
        //    加载时会整体失败。
        if (name.trimmed().isEmpty()) {
            return fail(QString("第 %1 条报警没有名字 —— 名字不能为空").arg(i + 1));
        }
        // ② 名字不能重 —— addChecker 用 name 作 key,重名会 return false,
        //    加载时同样整体失败。存进去之前必须拦住。
        if (seen.contains(name)) {
            return fail(QString("报警名 \"%1\" 重复 —— 名字必须唯一(第 %2 条)")
                            .arg(name).arg(i + 1));
        }
        seen.insert(name);
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool Project::replaceAlarmsInFile(const QString& path,
                                  const QJsonArray& alarms,
                                  QString* errorOut) {
    auto fail = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
        return false;
    };

    // ---- ① 先校验,坏草稿绝不落盘 ----
    QString verr;
    if (!validateAlarmsArray(alarms, &verr)) return fail(verr);

    // ---- ② 读原文件 ----
    //   不存在就报错,不凭空造。只有 alarms 的文件加载时必然缺 variables 而失败,
    //   写出一个打不开的工程比不写更糟。
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return fail(QString("打不开工程文件: %1\n(请先在主界面用“打开工程”载入它,"
                            "F4-c 只改已有工程,不新建)").arg(f.errorString()));
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        return fail(QString("原工程文件不是合法 JSON(%1,offset %2),拒绝覆盖")
                        .arg(err.errorString()).arg(err.offset));
    }
    if (!doc.isObject()) {
        return fail("原工程文件顶层不是 object,拒绝覆盖");
    }

    // ---- ③ 只换 alarms,其余键原样保留 ----
    //   variables / actions / plcs / screens[].controls 一个都不动。
    //   ★ 这一步是本函数的核心约束:对话框只管报警,
    //   绝不能顺手把用户的画面和变量清空。
    QJsonObject root = doc.object();
    root["alarms"]    = alarms;

    // ---- ④ 原子写回 ----
    //   QSaveFile:写临时文件再 rename,中途失败不会把原工程写成半截。
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return fail(QString("打开文件准备写入失败: %1").arg(out.errorString()));
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        return fail(QString("写入失败: %1").arg(out.errorString()));
    }

    if (errorOut) errorOut->clear();
    return true;
}

// ===== 私有 loadRootFromFile:读文件 + 解析 + schema 校验 + migrate =====
//   返回 true 表示成功(应用 vars/alarms/actions/plcs 到管理器);
//   成功时 outRoot 是 migrate 后的完整 JSON(供 E.3 进一步解析 screens[0].controls)
//   ★ E.3:对 manager null 容错 — 只在非 null 时执行对应 section
bool Project::loadRootFromFile(const QString& path, QJsonObject& outRoot) {
    outRoot = {};
    // 不强制要求 manager 至少一个非 null — E.3 阶段 EditorMainWindow 全传 nullptr
    // 也能加载纯 controls JSON(只解 schema + screens,不做 vars/alarms/actions 应用)

    // 1. 读文件
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit loadError(QString("打开文件失败: %1").arg(f.errorString()));
        return false;
    }
    QByteArray data = f.readAll();
    f.close();

    // 2. 解析 JSON
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit loadError(QString("JSON 解析失败: %1 (offset %2)").arg(err.errorString()).arg(err.offset));
        return false;
    }
    if (!doc.isObject()) {
        emit loadError("JSON 顶层不是 object");
        return false;
    }
    QJsonObject root = doc.object();

    // 3. schema_version 校验 + 自动迁移
    QJsonValue schemaVal = root["schema_version"];
    int schemaVerInt = 0;
    if (schemaVal.isString()) {
        QString s = schemaVal.toString();
        if (s.startsWith("1.")) schemaVerInt = 1;
    } else if (schemaVal.isDouble()) {
        schemaVerInt = schemaVal.toInt();
    }
    if (schemaVal.isUndefined()) schemaVerInt = 1;

    if (schemaVerInt > kCurrentSchemaVersion) {
        emit loadError(QString("schema_version %1 太新,当前 hmi 只支持到 %2")
                       .arg(schemaVerInt).arg(kCurrentSchemaVersion));
        return false;
    }
    if (schemaVerInt < kCurrentSchemaVersion) {
        if (!migrateTo(root, schemaVerInt, kCurrentSchemaVersion)) {
            emit loadError(QString("schema 迁移失败:从 v%1 到 v%2").arg(schemaVerInt).arg(kCurrentSchemaVersion));
            return false;
        }
        qCDebug(hmiApp) << "[Project] schema 迁移完成:v" << schemaVerInt << "→ v" << kCurrentSchemaVersion;
    }

    // 4. 清空现有状态(Q4-A 全删干净)
    //
    // ★★★【这两行之间的"空窗期",2026-10-10 查证后补记】★★★
    //
    //   ThresholdAlarmChecker::variable_ 是【裸 Variable*】,而它的
    //   onVariableReplaced 槽【至今没有任何地方 connect】(hmi.cpp 那条连接
    //   在 E#29 改成"按名查"时被删了,槽成了孤儿)。
    //   所以上面 varMgr_->clear() 执行的时候,【每一个还活着的 checker】
    //   手里的 variable_ 当场变成悬空指针。
    //
    //   ★ 我一度以为"安全"来自这两行的顺序(以为 checker 先被清了)。
    //     实测不是:变量死的那一刻 checker【确实还活着】(Actual=1),
    //     生产里也一样 —— hmi 启动时建的 checker,换工程时变量先被删光。
    //     真正的安全来源是:紧接着的 clearAll() 把 checker 全 delete 了,
    //     而那条路上【没人解引用 variable_】
    //     (clearAll 只 delete,~ThresholdAlarmChecker 是 = default)。
    //
    //   所以这是"碰巧安全",不是"设计安全" —— 代码里没有任何一处表达它。
    //   【哪天有人往析构或 clearAll 里加一行"删之前把变量名打出来"就是 UAF】,
    //   而那行看起来像再正常不过的日志。
    //
    //   【为什么现在不直接给 checker 接上广播】
    //     checker 构造函数只收 Variable*,没有 VariableManager*,接不上;
    //     要接就得把 varMgr 穿过 Project / hmi / 一堆测试的构造签名,
    //     改动面远超一个孤儿槽该有的代价,而它今天确实不崩。
    //     等真要加"变量删除"功能时,连同 removeVariable 那条路一起做。
    //     加载后的可观测后果由 tst_ProjectSchema::loadFromFile_
    //     leavesNoCheckerOnDeadVariable 钉住。
    if (varMgr_)   varMgr_->clear();
    if (alarmMgr_) alarmMgr_->clearAll();   // ★ 必须紧跟在上一行后面,中间别插解引用 variable_ 的代码
    if (actionMgr_) actionMgr_->clear();

    // ★ F3(2026-09-30)问题 3:失败路径统一收口。
    //   原来第 4 步的 clear() 只在【函数开头】调一次,而下面 5 个解析循环各有
    //   return false —— 走到那里时前 N 个变量/报警已经建出来了,
    //   留在内存里就是"半个工程"。调用方若不检查返回值直接继续用就拿到了残缺状态。
    //   抽成 lambda 是为了【不再有第 6 处 return false 忘了清】——
    //   这正是"每处各写一遍"必然漏的形状。
    const auto failWith = [this](const QString& msg) {
        if (varMgr_)     varMgr_->clear();
        if (alarmMgr_)   alarmMgr_->clearAll();
        if (actionMgr_)  actionMgr_->clear();
        emit loadError(msg);
        return false;
    };

    // 5. 解析 variables
    if (varMgr_) {
        QJsonArray variablesArr = root["variables"].toArray();
        for (int i = 0; i < variablesArr.size(); ++i) {
            // ★ F3:把 fromJsonVariable 的具体错误信息带出来。
            //   原来只有一句 "variable 字段缺失或类型错误" —— data_type 拼错、
            //   漏写 access、缺 name 全都报这一句,且不含变量名。
            QString varErr;
            Variable* var = JsonSerializer::fromJsonVariable(variablesArr[i].toObject(),
                                                             varMgr_, &varErr);
            if (!var) {
                return failWith(QString("第 %1 个 variable 解析失败:\n%2")
                                    .arg(i + 1)
                                    .arg(varErr));
            }
            if (!varMgr_->addVariable(var)) {
                const QString dupName = var->name();
                delete var;
                return failWith(QString("variable \"%1\" 重复或非法").arg(dupName));
            }
        }
    }

    // 6. 解析 alarms(依赖 varMgr_)
    if (alarmMgr_ && varMgr_ && pollScheduler_) {
        QJsonArray alarmsArr = root["alarms"].toArray();
        for (const QJsonValue& a : alarmsArr) {
            QJsonObject aObj = a.toObject();
            QString alarmName = aObj["name"].toString();
            QString varName   = aObj["variable"].toString();

            Variable* var = varMgr_->findVariable(varName);
            if (!var) {
                return failWith(QString("alarm \"%1\" 引用了不存在的 variable \"%2\"")
                                    .arg(alarmName, varName));
            }
            CheckerConfig cfg = JsonSerializer::checkerConfigFromJson(aObj);
            auto* checker = new ThresholdAlarmChecker(alarmName, var, pollScheduler_, cfg, alarmMgr_);
            if (!alarmMgr_->addChecker(checker)) {
                delete checker;
                return failWith(QString("alarm \"%1\" 重复或 addChecker 失败").arg(alarmName));
            }
        }
    }

    // 7. 解析 actions
    if (actionMgr_ && varMgr_) {
        QJsonArray actionsArr = root["actions"].toArray();
        for (const QJsonValue& ac : actionsArr) {
            QJsonObject acObj = ac.toObject();
            QString alarmName = acObj["alarm_name"].toString();
            QString varName   = acObj["variable"].toString();
            QVariant writeValue = acObj["write_value"].toVariant();

            Variable* var = varMgr_->findVariable(varName);
            if (!var) {
                return failWith(QString("action \"%1\" 引用了不存在的 variable \"%2\"")
                                    .arg(alarmName, varName));
            }
            actionMgr_->addMapping(alarmName, varName, writeValue);
        }
    }

    // 8. 通讯字段应用
    //   ★ 2026-10-16(多 PLC 2a):读入全部 PLC 配置
    //
    //   ★ 2026-10-19(多 PLC 2b):把"建连接"这个副作用交给调用方。
    //      原来第 8 步自己调 tcp_->connectTo —— Project 于是握着通讯层对象,
    //      "加载工程"这个纯数据动作里藏了一次连网。改法见 Project::setPlcApplier。
    const int plcCount = loadPlcsFromJson(root);

    if (plcCount > 0 && tcp_) {
        // ★ 只应用第一台到 tcp_。取值走 plcs_[0] 而不是原来的 plcsArr[0]:
        //   区别在于 —— plcsArr[0] 可能是【校验没过被跳过的那台】,
        //   而 plcs_[0] 一定是校验通过的。两台以上时这个差别会咬人:
        //   第一台填错了,第二台是好的,用 plcsArr[0] 会拿着错地址去连,
        //   而用户看到的警告是"plcs[0] 已跳过",对不上他在连的那台。
        //
        //   【为什么 2b 之后这里还在】—— tcp_ 是【手工调试通道】:
        //   用户在调试面板手敲一帧原始 Modbus 报文时就用它,跟调度器无关。
        //   但它跟 PlcConnectionManager 建的那 N 条连接是【两条独立 socket】,
        //   真接上一台 PLC 就等于连了两遍。
        //   阶段 5 拼装 hmi 时会把这一段整个删掉(那时 applyConfig 已经接管),
        //   留到那时是因为这个提交必须【行为完全不变】才能单独验证。
        const QJsonObject plc0 = plcs_.at(0).toObject();
        const QString host = plc0["host"].toString();
        const int port = plc0["port"].toInt();
        if (!host.isEmpty() && port > 0 && port < 65536) {
            if (tcp_->host() == host && tcp_->port() == static_cast<quint16>(port)) {
                qCDebug(hmiComm) << "[Project] 通讯参数已应用(无变化,跳过重连):" << host << ":" << port;
            } else {
                tcp_->connectTo(host, static_cast<quint16>(port));
                qCDebug(hmiComm) << "[Project] 通讯参数已应用(切换):" << host << ":" << port;
            }
        }
    }

    // ★ 2b:把全部配置递出去建连接。这是"多台同时在线"真正落地的那一步。
    //   传的是 plcs_ —— 【校验后】的数组,不是原始 arr:
    //   校验没过的那几项(没 id / host 空 / port 越界)本就不该有人去连。
    //   传原始 arr 会让 PlcConnectionManager 再拒一遍,两处判据将来必然走偏。
    if (plcApplier_) {
        plcApplier_(plcs_);
    } else if (plcCount > 1) {
        // ★ 说【当前事实】,不说"未实现"。
        //   原来这里写的是"多台同时在线是 2b 的工作,尚未实现" —— 2b 做完后
        //   那句话就成了纯骗人的日志,还留在代码里误导下一个读日志的人。
        //   现在:没设 applier 就是真的没人连,那就照实说,并点名是谁的锅。
        qCWarning(hmiComm) << "[Project] ★ 读入" << plcCount
                           << "台 PLC 配置,但调用方没有 setPlcApplier ——"
                           << "【没有一条连接会被建立】。配置已存入,可在配置界面查看。";
    }

    outRoot = root;
    return true;
}

// ===== saveToFile(不带 controls,旧 API)=====
bool Project::saveToFile(const QString& path) {
    return saveToFile(path, {});
}

// ===== saveToFile(带 controls,E.3)=====
//   ★ E.3:对 varMgr_/alarmMgr_/actionMgr_/pollScheduler_ 容错 — null 时跳过该 section
//     (EditorMainWindow 在 E.3 阶段暂未持有真实 manager,只保存 controls JSON 也合法)
//   tcp_ 不影响 IO,只要路径合法就 OK
bool Project::saveToFile(const QString& path, const QList<qwhmi::widgets::ControlBase*>& controls) {
    QJsonObject root;
    root["schema_version"] = kCurrentSchemaVersion;   // 3 (E.3 bump)
    root["project_name"] = "HMI Demo Project";
    QString nowIso = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["created_at"]   = nowIso;
    root["updated_at"]   = nowIso;

    // plcs
    //   ★ 2026-10-16(多 PLC 2a):原来这里【无条件写死一台】(id 恒为 "plc1")。
    //     于是用户配了 3 台 PLC,存一次 → 另外 2 台被静默丢掉,不报错不警告。
    //     现在分三条路:
    //       1) 本次会话【加载过工程】→ 原样写回全部 PLC(一个字节都不改)
    //       2) 没加载过工程、但有人调过 setPlcs → 写那几台
    //       3) 什么都没配 → 退回"从当前 tcp_ 生成一台默认"(老行为)
    {
        if (!plcs_.isEmpty()) {
            // ★ 路径 1/2 一律原样写回。
            //   【为什么不重新生成】重新生成会丢掉用户手填的任何字段 ——
            //   将来 2b 加了每台 PLC 的通讯参数(重试次数、超时…)时,
            //   凡是没被这段代码显式列出来的字段都会消失,而用户无从知晓。
            //   存-取往返必须是恒等的,这是最基本的要求。
            root["plcs"] = plcs_;
        } else {
            // ★ 路径 3:没配过 PLC。给一台默认,而不是写空数组 ——
            //   写空数组的话,loadPlcsFromJson 返回 0,程序连 127.0.0.1 都不连,
            //   而"新建工程然后直接保存"是很常见的动作,不该得到一个空的通讯配置。
            QJsonArray plcsArr;
            plcsArr.append(defaultPlcFromTcp(tcp_));
            root["plcs"] = plcsArr;
        }
    }

    // variables
    {
        QJsonArray variablesArr;
        if (varMgr_) {
            for (Variable* v : varMgr_->allVariables()) {
                if (v) variablesArr.append(JsonSerializer::toJson(*v));
            }
        }
        root["variables"] = variablesArr;
    }

    // alarms
    {
        QJsonArray alarmsArr;
        if (alarmMgr_) {
            for (AlarmChecker* c : alarmMgr_->checkers()) {
                auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
                if (!tc) continue;
                QJsonObject alarmObj = JsonSerializer::toJson(tc->config());
                alarmObj["name"]     = c->name();
                alarmObj["variable"] = tc->variable() ? tc->variable()->name() : QString();
                alarmsArr.append(alarmObj);
            }
        }
        root["alarms"] = alarmsArr;
    }

    // actions
    {
        QJsonArray actionsArr;
        if (actionMgr_) {
            for (const ActionManager::Mapping& m : actionMgr_->mappings()) {
                actionsArr.append(JsonSerializer::toActionMappingJson(m.alarmName, m.variableName, m.value));
            }
        }
        root["actions"] = actionsArr;
    }

    // ★ E.3:screens[0].controls(画布)— 反射遍历控件列表
    {
        QJsonArray screensArr;
        QJsonObject mainScreen;
        mainScreen["name"] = "主屏";
        mainScreen["controls"] = controlsToJson(controls);
        screensArr.append(mainScreen);
        root["screens"] = screensArr;
    }

    if (!saveRootToFile(path, root)) return false;

    qCDebug(hmiApp) << "[Project] saved to" << path
             << "(controls:" << controls.size() << ")";
    emit saved(path);
    return true;
}

// ===== loadFromFile(6.3 + E.3)=====
bool Project::loadFromFile(const QString& path) {
    QJsonObject root;
    if (!loadRootFromFile(path, root)) return false;

    // ★ E.3:解析 screens[0].controls → emit controlsLoaded 给 EditorMainWindow
    QList<qwhmi::widgets::ControlBase*> loadedControls;
    QJsonArray screensArr = root["screens"].toArray();
    if (!screensArr.isEmpty()) {
        QJsonObject screen0 = screensArr[0].toObject();
        QJsonArray controlsArr = screen0["controls"].toArray();

        // ★ 用 ControlBase::create(typeName) 静态工厂作 factory(E.1 registerControlType 注册表)
        int n = loadControlsFromJson(controlsArr,
                                     &qwhmi::widgets::ControlBase::create,
                                     loadedControls);
        qCDebug(hmiApp) << "[Project] loaded" << n << "controls from" << path;
    }
    emit controlsLoaded(loadedControls);   // ★ E.3

    qCDebug(hmiApp) << "[Project] loaded from" << path;
    emit loaded(path);
    return true;
}

// =============================================================================
// schema 演进函数链
// =============================================================================

bool Project::migrateV1ToV2(QJsonObject& root) {
    qCDebug(hmiApp) << "[Project] migrateV1ToV2 开始";
    root["schema_version"] = 2;
    if (!root.contains("plcs")) {
        QJsonObject comm = root["communications"].toObject();
        QJsonObject plc;
        plc["id"] = "plc1";
        plc["protocol"] = comm.value("protocol").toString("modbus_tcp");
        plc["host"] = comm.value("host").toString("127.0.0.1");
        plc["port"] = comm.value("port").toInt(502);
        if (comm.contains("modbus")) {
            plc["modbus"] = comm["modbus"];
        } else {
            QJsonObject modbus;
            modbus["timeout_ms"] = 3000;
            modbus["max_retries"] = 5;
            modbus["poll_interval_ms"] = 100;
            plc["modbus"] = modbus;
        }
        QJsonArray plcsArr;
        plcsArr.append(plc);
        root["plcs"] = plcsArr;
    }
    root.remove("communications");
    qCDebug(hmiApp) << "[Project] migrateV1ToV2 完成(communications → plcs[0])";
    return true;
}

// ★ E.3:v2 → v3:screens[] 数组准备(老 v2 工程没有 controls,迁移时给空数组)
//   旧 v2 工程加载后画布为空,用户重新拖入控件后再保存就是 v3
bool Project::migrateV2ToV3(QJsonObject& root) {
    qCDebug(hmiApp) << "[Project] migrateV2ToV3 开始";

    // 1. schema_version 数字更新
    root["schema_version"] = 3;

    // 2. 加 screens[0].controls = []
    //    如果已有 screens 字段,只确保 controls 数组存在(否则给空)
    if (!root.contains("screens")) {
        QJsonArray screensArr;
        QJsonObject mainScreen;
        mainScreen["name"] = "主屏";
        mainScreen["controls"] = QJsonArray();
        screensArr.append(mainScreen);
        root["screens"] = screensArr;
    } else {
        QJsonArray screensArr = root["screens"].toArray();
        for (int i = 0; i < screensArr.size(); ++i) {
            QJsonObject screen = screensArr[i].toObject();
            if (!screen.contains("controls")) {
                screen["controls"] = QJsonArray();
                screensArr.replace(i, screen);
            }
        }
        root["screens"] = screensArr;
    }

    qCDebug(hmiApp) << "[Project] migrateV2ToV3 完成(screens[].controls 已补全)";
    return true;
}

bool Project::migrateTo(QJsonObject& root, int fromVersion, int toVersion) {
    int current = fromVersion;
    while (current < toVersion) {
        switch (current) {
            case 1:
                if (!migrateV1ToV2(root)) return false;
                current = 2;
                break;
            case 2:
                if (!migrateV2ToV3(root)) return false;
                current = 3;
                break;
            default:
                qWarning() << "[Project] migrateTo:没有从 v" << current << "到 v" << (current+1) << "的迁移函数";
                return false;
        }
    }
    return true;
}