# 阶段 B · 从这里继续

> 适用:**阶段 B(用户/角色/权限 + 操作审计)** 已收尾到 B3,B4/B5 待启动时翻这份。
> 维护人:Wqian + AI 助手  ｜  最后更新:2026-09-17(B3 收尾后)
> 配套:`docs/现状.md` / `docs/路线图.md` / `docs/改善清单.md` / `docs/优化与改善计划.html`(v0.2.0)
> ⚠ **状态(2026-09-28 基于代码核对)**:✅ **B 阶段代码已落地**(B1-B5 全部完成,见 `src/auth/*` + 8 测试);本文件保留作为"开干手册"参考。完整阶段状态见 `docs/路线图.md` 阶段总览表。

---

## 0. 一句话现状

**B3 已完成且测试覆盖完整**,Permission 枚举 + checkPermission 已接入 2 个真实业务路径
(写值 / 报警确认)。**B4(画面级权限)和 B5(操作审计)是阶段 B 最后的两个子阶段。**

---

## 1. 阶段 B 已完成的子阶段清单

| 子阶段 | 内容 | 关键文件 | commit(参考) |
|---|---|---|---|
| **B1.1** | Role 枚举(Admin/Engineer/Operator/Viewer)+ 字符串互转 | `hmi/src/auth/Role.h/.cpp` | — |
| **B1.2** | UserManager(CRUD + signals 给 B5 审计用) | `hmi/src/auth/UserManager.h/.cpp` | — |
| **B1.3** | UserStore(JSON 持久化 + 首启 AdminSetupDialog) | `hmi/src/auth/UserStore.h/.cpp` + `AdminSetupDialog.*` | — |
| **B2.1** | LoginDialog(失败 3 次锁 5 分钟,倒计时显示) | `hmi/src/auth/LoginDialog.h/.cpp` | — |
| **B2.2** | CurrentUser 单例 + InactivityWatcher(5 分钟无操作自动注销) | `CurrentUser.h/.cpp` + `InactivityWatcher.*` | — |
| **B2.3** | 托盘"切换用户/修改密码" | `hmi/main.cpp:233-257` | — |
| **B2.4** | 托盘"用户管理"(UserManagementDialog,Admin 限定) | `UserManagementDialog.h/.cpp` | — |
| **B3** ✅ | **Permission 枚举(6 个)+ checkPermission 集成 2 个真实调用点** | `Permission.h/.cpp` + `Binding.cpp:148` + `AlarmManager.cpp:35` | `cc4fecb` |
| B3 收尾 | 删 4 条临时诊断日志 + 本文档 | `hmi.cpp:40-57` 旧块 / `Binding.cpp` 旧 3 条 qDebug / 本文件 | (本次) |

---

## 2. B3 集成位置(只这 2 处是真的产品代码,其他都是测试)

### 2.1 写值路径:`Binding::bindEdit` → checkPermission(WriteVariable)

**文件**:`hmi/Binding.cpp`,`bindEdit` lambda 内 `returnPressed` 槽

```cpp
// 阶段 4.2 + ★ B3(2026-09-17):写值前 checkPermission(WriteVariable)
const bool ok = qwhmi::auth::checkPermission(
    qwhmi::auth::CurrentUser::instance().role(),
    qwhmi::auth::Permission::WriteVariable);
if (!ok) {
    applyNowEdit(edit, v);
    setDirtyStyle(edit, true);
    qCWarning(qwhmi::auth::hmiPerm)
        << "本用户" << qwhmi::auth::CurrentUser::instance().name()
        << "无 WriteVariable 权限,写入被拒:" << v->name();
    return;
}
```

**行为**:Viewer 用户回车写 speedWriteEdit → 拒绝 + UI 回滚到旧值 + 边框留橙 + 日志告警。
**测试**:`hmi/tests/tst_Permission.cpp`(Viewer 无 WriteVariable 已覆盖)。

### 2.2 报警确认路径:`AlarmManager::acknowledge` → checkPermission(AcknowledgeAlarm)

**文件**:`hmi/AlarmManager.cpp`,`acknowledge(qint64 id)` 方法入口

```cpp
void AlarmManager::acknowledge(qint64 id) {
    // ★ B3(2026-09-17):确认报警前 checkPermission(AcknowledgeAlarm)
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                              qwhmi::auth::Permission::AcknowledgeAlarm)) {
        qCWarning(qwhmi::auth::hmiPerm)
            << "本用户" << qwhmi::auth::CurrentUser::instance().name()
            << "无 AcknowledgeAlarm 权限,确认被拒 alarmId=" << id;
        return;
    }
    // ... 原逻辑:查 activeAlarms_,改 state,emit alarmAcknowledged
}
```

**行为**:Viewer 用户右键报警 → 点确认 → 拒绝 + 日志告警(无 UI 反馈,因为右键菜单不可见)。

### 2.3 ActionManager 联动 — **不加权限检查(已拍板)**

`hmi/ActionManager.cpp:8` 顶部注释:

> **注:B3 决定联动不加 checkPermission(系统级行为,不是用户操作)**

报警联动(温度高/低 → 速度写 0)是**系统对系统的安全动作**,不应该被 Viewer 权限拦截。
这条决策在 B3 拍板会上定,后续阶段不要回头加。

---

## 3. 关键文件地标(B 阶段找代码用)

### 3.1 启动 + 登录 + 注入 CurrentUser 的入口

**`hmi/main.cpp`**

| 行号 | 内容 | 备注 |
|---|---|---|
| 28-40 | include auth/* 全部头 | LoginDialog / CurrentUser / UserManager / UserStore |
| 159 | `auth::UserManager userMgr;` | **main 局部**,B2.2 提到此处(原 B1.3 嵌套块已挪出) |
| 162-164 | signals → UserStore::save 集中触发 | userAdded / userRemoved / passwordChanged 都自动存盘 |
| 167 | `auth::UserStore::load(userMgr)` | 首次启动空 → 自动弹 AdminSetupDialog |
| 170-175 | 首启引导(必须建 admin) | 用户取消 → return 1 拒绝启动 |
| 178-187 | 已有用户 → LoginDialog | 成功 → `CurrentUser::instance().login(...)` |
| 189-207 | InactivityWatcher 自动注销 | 5 分钟无操作 → 重新弹 LoginDialog |
| 233-249 | 托盘"切换用户" | 同样走 logout + LoginDialog + login |
| 251-257 | 托盘"修改密码" | ChangePasswordDialog(模态) |
| 259-265 | 托盘"用户管理" | UserManagementDialog(exec 内部 Admin 校验) |

### 3.2 权限定义

**`hmi/src/auth/Permission.h/.cpp`**

| 内容 | 行号 |
|---|---|
| 6 个 Permission 枚举 | `.h:29-36` |
| Admin 全 6 个 / Engineer 5 个 / Operator 2 个 / Viewer 0 个 | `.cpp:25-53` |
| `checkPermission(role, perm)` 静态查表 | `.cpp:60-62` |
| `permissionToString` / `permissionFromString` | `.cpp:64-85` |
| Q_LOGGING_CATEGORY(hmiPerm) | `.cpp:19` ← **日志类别,后续 B5 审计会复用** |

### 3.3 当前登录用户查询接口

**`hmi/src/auth/CurrentUser.h/.cpp`**(单例,Q_GLOBAL_STATIC)

```cpp
CurrentUser::instance().isLoggedIn()   // bool
CurrentUser::instance().name()         // QString(空 = 未登录)
CurrentUser::instance().role()         // Role enum(默认 Viewer,安全默认)
// signals: userLoggedIn(QString) / userLoggedOut()
```

---

## 4. 怎么验证 B3 真生效(快速回归测试)

**注**:B3 不会改任何视觉/行为(被拒时静默 + 日志),只能通过日志或自动化测试验证。

### 4.1 看日志(快速,30 秒)

1. 启动程序 → 用 **Viewer** 角色登录
2. 在 speedWriteEdit 输 "1234" + 回车
3. 打开 `%LOCALAPPDATA%/QianWei/HMI/logs/` 看当日 log
4. 应该看到:
   ```
   hmi.perm: 本用户 viewer 无 WriteVariable 权限,写入被拒: Speed
   ```
5. 制造一个温度高报警 → 右键报警 → 点确认
6. 应该看到:
   ```
   hmi.perm: 本用户 viewer 无 AcknowledgeAlarm 权限,确认被拒 alarmId=1234567890
   ```

### 4.2 跑单元测试(权威,5 分钟)

```cmd
cmake -S hmi/tests -B hmi/tests/build && ctest --test-dir hmi/tests/build -C Debug
```

`tst_Permission` 应该全部通过,覆盖矩阵:
- Admin / Engineer / Operator / Viewer × WriteVariable / AcknowledgeAlarm / ManageUsers / ...

### 4.3 反向验证(确认 Operator 还能写)

1. 重启 → 用 **Operator** 登录(不是 Viewer)
2. speedWriteEdit 输 "1234" + 回车 → 应该正常写 + 标签刷新
3. 报警确认 → 应该正常确认 + 状态列改"已确认"

---

## 5. B4 切入点(下一个子阶段)

### 5.1 B4 是什么

**画面级权限** —— 把 checkPermission 反向推到 UI 控件的 `enabled` 属性上,
让"无权限的按钮直接是灰的",而不是点了才被拒。

### 5.2 当前痛点(B3 留下的 UX 缺口)

| 用户 | 体验 |
|---|---|
| Viewer | 看 startButton / stopButton / resetButton 都亮 → 点了 → 弹窗拒绝 → 困惑 |
| Operator | 想改工程 → openButton / saveButton 都亮 → 点了 → 弹窗拒绝 |
| 任何人 | 想进用户管理 → 托盘菜单"用户管理" → 点了 → 内部拒绝 |

按钮一直亮 + 点了才被拒 = 用户体验差 + 增加无效点击 + 日志噪音。

### 5.3 推荐的 B4 最小实现

**新建 `hmi/src/auth/UIPermission.h/.cpp`**,提供 2 个工具函数:

```cpp
namespace qwhmi::auth {

// 把按钮的 enabled 绑到某 Permission
//   - 当前权限不够 → setEnabled(false)
//   - CurrentUser 切换时自动重新计算
void bindButtonEnable(QAbstractButton* btn, Permission p);

// 一次性应用所有已注册的绑定(给 CurrentUser.userLoggedIn/userLoggedOut 用)
void applyAllBindings();

}
```

**接入点**(在 `hmi/main.cpp` `CurrentUser::instance().login(...)` 之后):

```cpp
auth::CurrentUser::instance().login(loginDlg.currentUser());
qwhmi::auth::applyAllBindings();   // ← 新增
```

**绑定示例**(`hmi/hmi.cpp` 构造函数末或新增 `initUiPermissions()`):

```cpp
qwhmi::auth::bindButtonEnable(ui.startButton,       Permission::WriteVariable);
qwhmi::auth::bindButtonEnable(ui.stopButton,        Permission::WriteVariable);
qwhmi::auth::bindButtonEnable(ui.resetButton,       Permission::WriteVariable);
qwhmi::auth::bindButtonEnable(ui.connectBtn,        Permission::EditProject);   // 暂定
qwhmi::auth::bindButtonEnable(ui.saveButton,        Permission::EditProject);
qwhmi::auth::bindButtonEnable(ui.openButton,        Permission::EditProject);
// 托盘"用户管理"菜单项,在 SystemTray.cpp 里 bind(QAction*, Permission::ManageUsers)
```

**验收**:
- 用 Viewer 登录 → startButton/stopButton/resetButton 全灰
- 用 Operator 登录 → 上面 3 个亮,但 saveButton/openButton 灰
- 用 Admin 登录 → 全部亮
- 托盘"切换用户"切到 Viewer → 按钮立即灰(不重启)

### 5.4 B4 估时

参考 v0.2.0 HTML 阶段 B 第 4 小节:**约 1 练(2-3 小时)**。
简单活:工具函数 1 小时 + 接入 6 个按钮 30 分钟 + 验收测试 1 小时。

---

## 6. B5 切入点(阶段 B 最后一站)

### 6.1 B5 是什么

**操作审计** —— 把所有"用户做了什么"持久化到 log,给事故复盘用。

### 6.2 最小事件源清单

B5 要监听的事件(按数据源分):

| 事件源 | 已有的 signal | B5 接什么 |
|---|---|---|
| `UserManager` | `userAdded` / `userRemoved` / `passwordChanged` / `authenticationFailed` | 接,全部留痕 |
| `Binding::bindEdit` 写值成功 | (B3 已加 qCWarning 拒日志,成功路径无) | 加 `valueWrittenSuccessfully` signal |
| `AlarmManager::acknowledge` 成功 | `alarmAcknowledged(id)` | 接,带用户名 |
| `AlarmManager::acknowledgeAll` | (无独立 signal,触发多次 `alarmAcknowledged`) | 走单条路径 |
| `LoginDialog` 成功 / 失败 | (B1.2 `authenticationFailed` 已有) | 成功也补一条 `loginSucceeded` |
| 托盘"切换用户" | (lambda 块,无 signal) | 在 main.cpp 里加一行 `audit->append("logout", ...)` |

### 6.3 最小实现建议

- 新建 `hmi/src/auth/AuditLog.h/.cpp`(`QObject` + `append(action, user, role, target, result)`)
- 落盘到 `%LOCALAPPDATA%/QianWei/HMI/audit.log`(按天滚动,`QFile` + 简单日期切换)
- 用现成的 `Q_LOGGING_CATEGORY(hmiAudit)`(新建)

**估时**:**约 1 练(2-3 小时)**,跟 B4 加起来完成"阶段 B 通过"。

---

## 7. 常见坑(下次回来别踩)

### 7.1 权限被频繁拒绝 + qDebug 噪音

**症状**:qDebug 控制台被 `hmi.perm: 本用户...无...权限,写入被拒` 刷屏。
**原因**:Viewer 登录时,bindEdit 的 dirty style 会让 UI 一直显示橙边框,反复触发不必要的事件。
**正确处理**:B5 审计落地后,这类拒日志会走 audit.log,**主日志要降级为 `qCDebug`(不是 qCWarning)**,避免干扰。

### 7.2 CurrentUser::instance() 在 main 之前调会崩

**症状**:在 `QApplication` 创建之前调 `CurrentUser::instance()` → 程序起不来。
**原因**:Q_GLOBAL_STATIC 依赖 QCoreApplication 已建好。
**正确处理**:B 阶段所有代码都在 main 内部,且都在 `QApplication app` 之后。当前 main.cpp 顺序正确,**不要挪**。

### 7.3 UserStore 默认路径 ≠ working dir

**症状**:找不到 `users.json`。
**原因**:路径是 `QStandardPaths::AppLocalDataLocation + "/users.json"` = `%LOCALAPPDATA%/QianWei/HMI/users.json`,**不是 exe 旁边**。
**正确处理**:调试时去 `%LOCALAPPDATA%\QianWei\HMI\` 看,**别去 `out/build/...` 里找**。

### 7.4 Permission 改了 → 现有 JSON 不需要 migrate

**症状**:加新 Permission 枚举值 → tst_Permission 挂了。
**原因**:Permission 只在 C++ 枚举里,**users.json / project.json 都不存 Permission 字段**,只存 Role。
**正确处理**:放心改枚举,JSON 无需 migrate。

### 7.5 InactivityWatcher 自动注销会跟调试打架

**症状**:开着 qDebug 看 log,5 分钟没动鼠标 → 自动注销 → LoginDialog 弹出来挡住。
**正确处理**:调试时临时把 `inactivityWatcher.installTo(&app)` 注释掉,或调大阈值(看 InactivityWatcher.h 的 `kTimeoutMs`)。

### 7.6 加新 .cpp/.h 漏同步 vcxproj,B4 真踩了

**症状**:VS IDE F5 build 报 LNK2019 — "无法解析的外部符号 qwhmi::auth::bindButtonEnable"。
**原因**:**CMake 用 GLOB 自动捡新文件,但 VS 用的 `hmi.vcxproj` 是显式列表,新文件不会被自动加进去**。
现状.md 第五节约定 #3 早就写过,但 cmake build 通过容易让人忘。

**正确处理**(加新 .cpp/.h 的标准流程):
1. 写源文件 + 头文件
2. **改 `hmi.vcxproj`**:在对应分组加 `<ClInclude Include="src\xxx\Xxx.h" />` + `<ClCompile Include="src\xxx\Xxx.cpp" />`(带 Q_OBJECT 的改成 `<QtMoc>`)
3. **改 `hmi.vcxproj.filters`**:Solution Explorer 分组,加同样的条目 + `<Filter>Header Files</Filter>` 或 `Source Files</Filter>`
4. `cmake build` 验证 + VS F5 验证

**快速检查**:`grep "Xxx\.cpp" hmi/hmi.vcxproj` → 必须有 1 行,否则漏了。
**Q_OBJECT 区分**:
- 类里有 `Q_OBJECT` 宏 → vcxproj 用 `<QtMoc Include="...">`
- 纯 free function / 普通类 → vcxproj 用 `<ClInclude Include="...">`
- 不确定时:`grep "Q_OBJECT" src/xxx/Xxx.h` 看一眼

---

## 8. 一句话回顾

> B3 = "Permission 枚举 + checkPermission 接入 2 个真实调用点",已完成。
> **下一步:B4(画面级权限,1 练 2-3 小时)+ B5(操作审计,1 练 2-3 小时),阶段 B 收官。**

— 完 —
