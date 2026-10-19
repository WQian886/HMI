# 阶段 A · A5 单元测试
# 用 Qt Test,跟主程序共用 core 源码

cmake_minimum_required(VERSION 3.21)
project(hmi_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network SerialBus Sql PrintSupport UiTools)
enable_testing()

# 主程序源码目录(用来 include 头文件)
set(HMI_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
include_directories(${HMI_SRC_DIR})
# 阶段 B · B1:src/auth/ 头文件(#include "auth/Role.h" 风格)
include_directories(${HMI_SRC_DIR}/src)
# ★ D.2(2026-09-20):QCustomPlot 头文件路径(TrendWidget.h include "qcustomplot.h")
include_directories(${HMI_SRC_DIR}/3rdparty/qcustomplot)

# 测试要链接的核心源文件
set(HMI_CORE_SOURCES
    ${HMI_SRC_DIR}/Variable.cpp
    ${HMI_SRC_DIR}/VariableManager.cpp
    ${HMI_SRC_DIR}/JsonSerializer.cpp
    # 阶段 A · A3 日志(UserManager 用 qCInfo/qCWarning)
    ${HMI_SRC_DIR}/src/logging/Logging.cpp
    # 阶段 B · B1 用户/角色/密码哈希
    ${HMI_SRC_DIR}/src/auth/Role.cpp
    ${HMI_SRC_DIR}/src/auth/User.cpp
    ${HMI_SRC_DIR}/src/auth/PasswordHasher.cpp
    # 阶段 B · B1.2 UserManager
    ${HMI_SRC_DIR}/src/auth/UserManager.cpp
    # 阶段 B · B1.3 UserStore
    ${HMI_SRC_DIR}/src/auth/UserStore.cpp
    # 阶段 B · B2.2 CurrentUser
    ${HMI_SRC_DIR}/src/auth/CurrentUser.cpp
    # ★ 2026-10-14:B2.2 InactivityWatcher(自动注销)
    #   【为什么现在才加】做 24h 长跑的 --soak 时发现它【零测试覆盖】——
    #   而它是个【安全特性】(5 分钟无操作自动注销)。
    #   --soak 要在长跑期间关掉它,那就等于改了安全特性的生命周期,
    #   却没有一个测试能回答"正常路径有没有被弄坏"。
    #   这跟改善清单 #13 一模一样的结构:【没有测试 ≠ 改动安全】。
    ${HMI_SRC_DIR}/src/auth/InactivityWatcher.cpp
    # 阶段 B · B3 Permission
    ${HMI_SRC_DIR}/src/auth/Permission.cpp
    # 阶段 B · B4 UI 权限绑定
    ${HMI_SRC_DIR}/src/auth/UIPermission.cpp
    # 阶段 B · B5 操作审计
    ${HMI_SRC_DIR}/src/auth/AuditLog.cpp
    # 阶段 C · C.0 schema 演进
    ${HMI_SRC_DIR}/Project.cpp
    ${HMI_SRC_DIR}/ActionManager.cpp
    ${HMI_SRC_DIR}/ThresholdAlarmChecker.cpp
    ${HMI_SRC_DIR}/TcpClient.cpp
    ${HMI_SRC_DIR}/Alarm.cpp
    ${HMI_SRC_DIR}/AlarmManager.cpp
    # ★ F5(2026-10-08):报警声音/蜂鸣
    #   【为什么声音层也要进 HMI_CORE_SOURCES】add_hmi_test 把这整个列表
    #   编进每一个测试。少加一行 = 链接期报一串"无法解析的外部符号",
    #   报错点全在测试文件里,离病根(这一行没加)很远。
    ${HMI_SRC_DIR}/AlarmSoundPlayer.cpp
    ${HMI_SRC_DIR}/src/alarm/AlarmSoundDialog.cpp
    ${HMI_SRC_DIR}/PollScheduler.cpp
    ${HMI_SRC_DIR}/ModbusMaster.cpp
    ${HMI_SRC_DIR}/WriteScheduler.cpp
    # 阶段 C · C.1 协议抽象层
    ${HMI_SRC_DIR}/src/drivers/IPollDriver.cpp
    ${HMI_SRC_DIR}/src/drivers/ModbusDriver.cpp
    ${HMI_SRC_DIR}/src/drivers/DriverFactory.cpp
    # 阶段 C · C.2 S7 协议适配器(2026-09-18)DriverFactory 创建 S7Driver 需要链接 S7Driver.cpp
    ${HMI_SRC_DIR}/src/drivers/S7Driver.cpp
    # 阶段 C · C.3 EIP 协议适配器(2026-09-18)DriverFactory 创建 EipDriver 需要链接 EipDriver.cpp
    ${HMI_SRC_DIR}/src/drivers/EipDriver.cpp
    # ★ 2b(2026-10-19):多 PLC 连接管理(把 plcs[] 变成运行期真的在线的 N 台)
    #   IPlcDriverProvider.h 是纯接口无 Q_OBJECT,不进列表(没有 .cpp)。
    ${HMI_SRC_DIR}/src/plc/PlcConnectionManager.cpp
    # ★ 2b 阶段6(2026-10-19):PLC 配置对话框(形状照 AlarmConfigDialog)
    ${HMI_SRC_DIR}/src/plc/PlcConfigDialog.cpp
    # ★ 2026-10-10:协议下拉框置灰(tst_ProtocolComboGrey 调的是这个生产函数)
    ${HMI_SRC_DIR}/src/drivers/ProtocolAvailabilityUi.cpp
    # ★ 2026-10-16:菜单栏(tst_MenuBar 调的是 hmi.cpp 调的那个同一个 buildApplicationMenu)
    ${HMI_SRC_DIR}/src/ui/MenuBuilder.cpp
    # 阶段 D · D.1 SQLite 历史库(2026-09-18)
    ${HMI_SRC_DIR}/src/history/HistoryRecorder.cpp
    # ★ D.3(2026-09-20):报警持久化(对称 HistoryRecorder + alarm_history 表)
    ${HMI_SRC_DIR}/src/history/AlarmRecorder.cpp
    # ★ F4(2026-10-08):报警历史查询视图
    ${HMI_SRC_DIR}/src/history/AlarmHistoryView.cpp
    # ★ D.4(2026-09-20):CSV 导出
    ${HMI_SRC_DIR}/src/export/CsvExporter.cpp
    # ★ D.6(2026-09-20):派生表达式解析器(Pratt 手写)
    ${HMI_SRC_DIR}/src/tag/ExpressionParser.cpp
    # ★ D.7(2026-09-20):标签导入器(CSV-based)
    ${HMI_SRC_DIR}/src/tag/TagImporter.cpp
    # ★ F4-a(2026-10-08):报警配置对话框(只读 + 草稿导出)
    ${HMI_SRC_DIR}/src/alarm/AlarmConfigDialog.cpp
    # ★ D.2(2026-09-20):历史曲线控件 + QCustomPlot
    ${HMI_SRC_DIR}/src/widgets/TrendWidget.cpp
    ${HMI_SRC_DIR}/3rdparty/qcustomplot/qcustomplot.cpp
    # ★ E.1(2026-09-20):控件基类 + LabelControl
    ${HMI_SRC_DIR}/src/widgets/base/ControlBase.cpp
    ${HMI_SRC_DIR}/src/widgets/LabelControl.cpp
    # ★ E.1a-2(2026-09-20):NumberDisplay + TextDisplay
    ${HMI_SRC_DIR}/src/widgets/NumberDisplay.cpp
    ${HMI_SRC_DIR}/src/widgets/TextDisplay.cpp
    # ★ E.1a-3(2026-09-20):Bar + Gauge + LED
    ${HMI_SRC_DIR}/src/widgets/BarControl.cpp
    ${HMI_SRC_DIR}/src/widgets/GaugeControl.cpp
    ${HMI_SRC_DIR}/src/widgets/LedControl.cpp
    # ★ E.1a-4(2026-09-20):Image + Rectangle + Line + Group
    ${HMI_SRC_DIR}/src/widgets/ImageControl.cpp
    ${HMI_SRC_DIR}/src/widgets/RectangleControl.cpp
    ${HMI_SRC_DIR}/src/widgets/LineControl.cpp
    ${HMI_SRC_DIR}/src/widgets/GroupControl.cpp
    # ★ E.1b(2026-09-20):Button + Toggle + Slider + InputEdit
    ${HMI_SRC_DIR}/src/widgets/ButtonControl.cpp
    ${HMI_SRC_DIR}/src/widgets/ToggleControl.cpp
    ${HMI_SRC_DIR}/src/widgets/SliderControl.cpp
    ${HMI_SRC_DIR}/src/widgets/InputEditControl.cpp
    # ★ E.1b-2(2026-09-20):Trend + Table + AlarmList
    ${HMI_SRC_DIR}/src/widgets/TrendControl.cpp
    ${HMI_SRC_DIR}/src/widgets/TableControl.cpp
    ${HMI_SRC_DIR}/src/widgets/AlarmListControl.cpp
    # ★ 2026-10-14:报警行配色。AlarmListControl 和 hmi.cpp 都调它,
    #   漏了这里会 LNK2019(三个未解析符号)—— 而 hmi/CMakeLists.txt 那份是另一份列表。
    ${HMI_SRC_DIR}/src/alarm/AlarmRowPalette.cpp
    # ★ E.1c(2026-09-28):5 个工业控件(储罐/管道/阀门/泵/电机)
    #   逐个加:写完一个接线一个,避免文件还不存在就编不过
    ${HMI_SRC_DIR}/src/widgets/TankControl.cpp
    ${HMI_SRC_DIR}/src/widgets/ValveControl.cpp
    ${HMI_SRC_DIR}/src/widgets/PipeControl.cpp
    ${HMI_SRC_DIR}/src/widgets/PumpControl.cpp
    ${HMI_SRC_DIR}/src/widgets/MotorControl.cpp
    # ★ E.1c:转子旋转动画(泵/电机共用,见 SpinAnimator.h 头注释)
    ${HMI_SRC_DIR}/src/widgets/base/SpinAnimator.cpp
    # ★ E.2(2026-09-21):属性面板 PropertySheet + PropertyChangeCommand
    ${HMI_SRC_DIR}/src/editor/PropertySheet.cpp
    ${HMI_SRC_DIR}/src/editor/PropertyChangeCommand.cpp
    # ★ E.4d-follow-8(2026-09-29):多控件几何 Undo
    ${HMI_SRC_DIR}/src/editor/GeometryChangeCommand.cpp
    # ★ E.4c(2026-09-22):QSizeF/QPointF 复合编辑器
    ${HMI_SRC_DIR}/src/editor/SizeEditor.cpp
    ${HMI_SRC_DIR}/src/editor/PositionEditor.cpp
    # ★ E.2 周 11:EditorMainWindow
    ${HMI_SRC_DIR}/src/editor/EditorMainWindow.cpp
    # ★ E.4a:PaletteWidget
    ${HMI_SRC_DIR}/src/editor/PaletteWidget.cpp
    # ★ E.4b:ResizeHandleItem
    ${HMI_SRC_DIR}/src/editor/ResizeHandleItem.cpp
    # ★ E.4d 续:选中态视觉统一
    ${HMI_SRC_DIR}/src/editor/SelectionFrameItem.cpp
    # ★ E.4d 续-3:多选组 handle 容器
    ${HMI_SRC_DIR}/src/editor/GroupHandleItem.cpp
    # ★ E.5(2026-09-29):运行时画布(主画面显示编辑器存的画面)
    ${HMI_SRC_DIR}/src/editor/RuntimeCanvas.cpp
    # ★ E.7(2026-09-30):复制/粘贴/删除的 Undo
    ${HMI_SRC_DIR}/src/editor/ControlListCommand.cpp
    # ★ 崩溃 dump(2026-10-14):此前【零测试覆盖】,pending.txt 静默失效 28 天没人发现。
    #   写 dump 和"崩了有人知道"是两回事,后者一直没被测过。详见 tst_CrashHandler.cpp 头注释。
    ${HMI_SRC_DIR}/src/crash/CrashHandler.cpp
)

# ============================================================
# ★★ 2026-10-14:核心源抽成静态库(解决 CI 编不完)
#
# 【问题】上面这 78 个源文件原本被【每一个】测试目标各编一遍。
#   72 个测试 × 78 个源 + AUTOMOC 产物 = 5904 个编译目标。
#   CI(2 核 runner)实测:编到 [4352/5904] = 74% 就撞上 120 分钟超时线,
#   日志末尾 "ninja: build stopped: interrupted by user"。
#   按实测吞吐(0.61 个/秒)推算,编完需 160 分钟 + 开销 ≈ 164 分钟。
#   ★ 注意:【换 Ninja 生成器没能解决这个问题】——
#     换完主程序编译从 4.5 分降到 2.6 分(确实更快),
#     但整体吞吐仍被 2 核 runner 的硬件差距锁死(CI 比本机慢 7 倍)。
#     详见 .github/workflows/build.yml 里那段实测记录。
#
# 【凭什么认为抽库能解决】—— 按【字节】实测(build_ninja,5762 obj / 3834 MB):
#     qcustomplot.cpp        72 份 × 13.5 MB = 973 MB  = 25.2%  ← 3rdparty,最重
#     核心源 moc 产物        72 份 ×  8.4 MB = 606 MB  = 15.6%
#     其余核心源             72 份 ×  ~1 MB = ~1725 MB = 44.3%
#     各测试自己的 tst_*.cpp 各 1 份                  =  1.5%  ← 这不是冗余
#   这三项都是【每份完全一样】的重复编译,静态库化后各只编 1 次。
#
# ★ 这里记一条我今天犯的错(免得下一个人重蹈):
#   我一度用"核心源占编译【次数】的 98.5%"推出"时间能省 98.5%,
#   CI 164 分钟能降到 3 分钟"—— 【那是拿次数当时间】,不成立。
#   次数和耗时权重完全不同(moc 和 qcustomplot 的体积能顶几十个普通源)。
#   要估时间必须按【字节】或【实测时长】,不能按出现次数。
#   上面这张表是按字节量的,可用;按次数算的那句不可用。
# ============================================================
add_library(hmi_core STATIC ${HMI_CORE_SOURCES})
target_compile_options(hmi_core PRIVATE /utf-8)
target_link_libraries(hmi_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets   # ★ B4:UIPermission.cpp 需要 QAbstractButton / QAction
    Qt6::Network   # ★ C.0:Project.cpp 间接 include QTcpSocket(TcpClient.h)
    Qt6::SerialBus  # ★ C.0:PollScheduler.h include QModbusDataUnit(ThresholdAlarmChecker 依赖)
    Qt6::Sql        # ★ D.1(2026-09-18):SQLite 历史库 + HistoryRecorder
    Qt6::PrintSupport  # ★ D.2(2026-09-20):QCustomPlot 需要 QPrinter
)

function(add_hmi_test test_name)
    # ★ 现在只编这一个测试自己的源,核心源由 hmi_core 提供。
    #   Qt6::Test 仍要单独链(核心源不用它,但测试本身要)。
    add_executable(${test_name}
        ${test_name}.cpp
    )
    target_link_libraries(${test_name} PRIVATE
        hmi_core
        Qt6::Test
    )
    target_compile_options(${test_name} PRIVATE /utf-8)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# ★ 2026-10-10:把协议下拉框真的渲染出来存 PNG(见 tst_ProtocolComboShot.cpp 头注)
#   不用 add_hmi_test,因为它要额外的 UiTools 和两个路径宏。
add_executable(tst_ProtocolComboShot tst_ProtocolComboShot.cpp)
target_link_libraries(tst_ProtocolComboShot PRIVATE
    hmi_core
    Qt6::Core Qt6::Gui Qt6::Test Qt6::Widgets Qt6::UiTools
)
target_compile_options(tst_ProtocolComboShot PRIVATE /utf-8)
target_compile_definitions(tst_ProtocolComboShot PRIVATE
    HMI_SRC_DIR_STRING="${HMI_SRC_DIR}"
    HMI_SHOT_DIR="${CMAKE_BINARY_DIR}/shots"
)
# 故意【不进】 add_test / ctest:
#   它里面有 QVERIFY,但那一半是 tooltip 显示与否,依赖 QTipLabel 的显示延迟,
#   轮询 1.5 秒还不行就判红 —— 这正是 tst_Trend 那种偶发红的来源,不想再加一条。
#   字体字形那条断言倒是确定性的,可要拆出来单跑,目前不值得。
#   用法:直接跑 build_ninja\tst_ProtocolComboShot.exe,PNG 落在 shots\。

add_hmi_test(tst_Variable)
add_hmi_test(tst_VariableManager)
# ★ 2026-10-16:多 PLC 第一块砖 —— plc_id 存住/读回/老工程不受影响
#   不并进 tst_Serializer:那个是通用序列化测试,这里钉的是
#   "改这行不会弄坏任何一个已有工程",两者失败含义不同,分开好定位。
add_hmi_test(tst_VariablePlcId)
# ★ 2026-10-16:多 PLC 2a —— plcs[] 数组存得住,一台都不能被静默丢掉
add_hmi_test(tst_ProjectMultiPlc)
# ★ 2b(2026-10-19):plcs[] → 运行期真的在线的 N 台 driver
#   与 tst_ProjectMultiPlc 分开:那边钉"存得住",这边钉"建得对"。
#   两边失败含义完全不同 —— 2a 坏了是文件丢数据,2b 坏了是现场连错设备。
add_hmi_test(tst_PlcConnectionManager)
# ★ 2b(2026-10-19):写侧按 plcId 路由。
#   这是整套 2b 里最严重的一条 —— 改之前给 plc2 的变量写的值
#   从 plc1 的 socket 发出去,而且【零日志】。
add_hmi_test(tst_WriteSchedulerMultiPlc)
# ★ 2b(2026-10-19):3 台真 QTcpServer 的端到端。
#   前三个 2b 测试全都把 driver 换成了 mock —— 它们证明"调度器派得对",
#   证明不了"三个指针背后是三条真的连到三台不同机器的 socket"。
#   而"写错设备"这个 bug 恰恰发生在链路层,只有真 socket 才现形。
#   所以三台服务端各自把寄存器空间填成 0x1111/0x2222/0x3333 —— 值本身编码身份。
add_hmi_test(tst_PlcMultiServerE2E)
# ★ 2b(2026-10-19):PLC 配置对话框 —— 改现场设备的配置不再只能手改 JSON。
#   最要紧的两条在文件头:删被引用的 PLC【不许】自动改变量指向,
#   状态列【不许】把"driver 建出来了"说成"在线"。
add_hmi_test(tst_PlcConfigDialog)
# ★ 2026-10-16:菜单栏 —— 逐个菜单项验证它连到了【正确的那个槽】
#   本来这段 UI 逻辑写在 hmi.cpp 里,测试链不到主程序(hmi_core 不含 hmi.cpp),
#   只能复制一份跑 —— 那样测的是副本。抽成 MenuBuilder 后两边调同一个函数。
add_hmi_test(tst_MenuBar)
add_hmi_test(tst_Serializer)
# 阶段 B · B1
add_hmi_test(tst_PasswordHasher)
# 阶段 B · B1.2
add_hmi_test(tst_UserManager)
# 阶段 B · B1.3
add_hmi_test(tst_UserStore)
# 阶段 B · B2.2
add_hmi_test(tst_CurrentUser)
# ★ 2026-10-14:B2.2 InactivityWatcher 此前零覆盖,补上
#   (它被 --soak 改了生命周期,而它是安全特性)
add_hmi_test(tst_InactivityWatcher)
# 阶段 B · B3
add_hmi_test(tst_Permission)
# 阶段 B · B4
add_hmi_test(tst_UIPermission)
# 阶段 B · B5
add_hmi_test(tst_AuditLog)
add_hmi_test(tst_AuditE2E)
# 阶段 C · C.0
add_hmi_test(tst_ProjectSchema)
# 阶段 C · C.1 协议抽象层
add_hmi_test(tst_IPollDriver)
# ★ 2026-10-10:TcpClient 重连退避状态机(改善清单 #2/#10)
#   TcpClient 从阶段 1 就是核心类,但此前【一个测试都没有】。
add_hmi_test(tst_TcpClient)
# ★ 2026-10-14:ModbusMaster 帧组装基线(改善清单 #13)
#   ModbusMaster 从阶段 2 就是核心类,此前 69 个测试目标里【零个】
#   断言过它组出来的任何一个字节 —— 而清单把 #13 标成"零行为变化"。
#   没有这层钉子,"零行为变化"只是一句没人验证过的话。
add_hmi_test(tst_ModbusFrame)
# ★ 2026-10-16:日志分类的【可关闭性】(改善清单 #23)
#   把 54 处裸 qDebug 归进 8 个 QLoggingCategory 之后,只靠"编译绿+测试绿"
#   证明不了任何事 —— 类别写错名字编译就挂,写对了也可能因为 handler 没认出来
#   而什么也不做,且【没有任何报错】。这个测试钉住"按类别能关掉"这个唯一收益。
add_hmi_test(tst_LoggingCategory)
# 阶段 C · C.4 协议选择 UX 锁(教训 1.5)
add_hmi_test(tst_ProtocolSwitch)
# ★ 2026-10-10:协议可用性(UI 置灰 S7/EIP 的查表)
add_hmi_test(tst_ProtocolAvailability)
# ★ 2026-10-10:协议下拉框置灰的【控件级】验证(逻辑对 ≠ 控件对)
add_hmi_test(tst_ProtocolComboGrey)
# 阶段 D · D.1 SQLite 历史库
add_hmi_test(tst_HistoryRecorder)
# 阶段 D · D.3 报警持久化
add_hmi_test(tst_AlarmRecorder)
# ★ F2(2026-09-30):报警抑制(滞回 + 状态机)
#   【为什么单独建】ThresholdAlarmChecker 是"报警该不该触发"的决策点,
#   全仓没有任何测试碰过它 —— minDurationMs 防抖从来没被一条用例守过。
add_hmi_test(tst_ThresholdAlarmChecker)
# ★ F2-c(2026-09-30):报警屏蔽(shelve)—— 必须可查、可撤销、可见
add_hmi_test(tst_AlarmShelve)
# ★ F5(2026-10-08):报警声音 —— 等级区分 / 确认即静音 / 静默时段
#   【这组测试测的是决策,不是耳朵】"响不响"能断言,声音有没有真的出来测不了。
#   绿灯之后仍然必须人耳验一次 —— 见 tst_AlarmSound.cpp 顶部。
add_hmi_test(tst_AlarmSound)
# ★ F4(2026-10-08):报警历史查询视图 —— 时间/等级过滤
#   【测的是"查得准",不是"画得出来"】界面能显示是一回事,
#   显示的是不是用户要的那几条是另一回事 —— 后者才是"能查事故"和"有个表格"的区别。
add_hmi_test(tst_AlarmHistoryView)
# ★ F4-a(2026-10-08):报警配置对话框
add_hmi_test(tst_AlarmConfigDialog)
# 阶段 D · D.4 CSV 导出
add_hmi_test(tst_CsvExporter)
# 阶段 D · D.6 派生表达式解析
add_hmi_test(tst_ExpressionParser)
# 阶段 D · D.7 标签导入(CSV-based,零三方依赖)
add_hmi_test(tst_TagImporter)
# 阶段 E · E.1a 第 1 个控件 LabelControl(测试需要 QApplication + offscreen)
add_hmi_test(tst_LabelControl)
add_hmi_test(tst_NumberDisplay)
add_hmi_test(tst_TextDisplay)
add_hmi_test(tst_BarControl)
add_hmi_test(tst_GaugeControl)
add_hmi_test(tst_LedControl)
add_hmi_test(tst_ImageControl)
add_hmi_test(tst_RectangleControl)
add_hmi_test(tst_LineControl)
add_hmi_test(tst_GroupControl)
add_hmi_test(tst_ButtonControl)
add_hmi_test(tst_ToggleControl)
add_hmi_test(tst_SliderControl)
add_hmi_test(tst_InputEditControl)
add_hmi_test(tst_TrendControl)
add_hmi_test(tst_TableControl)
add_hmi_test(tst_AlarmListControl)
# ★ 2026-10-14:报警行配色的唯一权威。纯函数测试,跑得极快,可以写密。
add_hmi_test(tst_AlarmRowPalette)
# ★ E.1c(2026-09-28):UI → PLC 反向写通道(ControlBase::writeToPlc)
#   修复前 ToggleControl 点一下只翻本地值,全仓库无人接 toggled 信号,
#   17 个控件全是只读显示器。详见 tst_ControlBaseWrite.cpp 头注释。
add_hmi_test(tst_ControlBaseWrite)
# ★ E.1c(2026-09-28):QColor 属性存读回归(alpha 通道静默丢失 + 非法值画没)
add_hmi_test(tst_ControlBaseColorJson)
# ★ E.1c(2026-09-28):5 个工业控件(补齐 E 阶段 22 控件的最后 5 个)
add_hmi_test(tst_TankControl)
add_hmi_test(tst_ValveControl)
add_hmi_test(tst_PipeControl)
add_hmi_test(tst_PumpControl)
add_hmi_test(tst_MotorControl)
add_hmi_test(tst_SpinAnimator)
# 阶段 E · E.2 属性面板 PropertySheet
add_hmi_test(tst_PropertySheet)
# 阶段 E · E.2 周 11 EditorMainWindow
add_hmi_test(tst_EditorMainWindow)
# ★ E.5(2026-09-29):运行时画布 —— 编辑器存的画面能不能在主画面显示、变量绑不绑得上。
#   重点测两件以前【零覆盖】的事:JSON 字符串 → Variable* 绑定,以及绑不上时能被看见。
add_hmi_test(tst_RuntimeCanvas)
# ★ E.7(2026-09-30):复制 / 粘贴 / 删除 + 它们的 Undo(原为零覆盖)。
#   重点是"删→撤→删→撤"连环 —— 命令存 JSON 快照,撤销造的是新对象,
#   旧指针当场失效,残留一处在第二轮就会出事。
add_hmi_test(tst_ControlClipboard)
# ★ E.4d-follow-8(2026-09-29):对齐/等尺寸的 Undo(9 个动作,原为零覆盖)
add_hmi_test(tst_GeometryUndo)
# ★ E.4d-follow-9(2026-09-29):选中框【真的画出来了吗】—— 渲染到 QImage 数像素,
#   补上 tst_EditorMainWindow 那 4 个只断言几何、不看画面的用例的盲区
add_hmi_test(tst_SelectionFrameRender)
# ★ E.4d-follow-10(2026-09-29):旋转 —— 先量出真正坏的是哪几件事,
#   再决定改什么(QGraphicsItem 本来就有 rotation 属性,见文件头注释)
add_hmi_test(tst_Rotation)
# 阶段 E · E.4a 控件调色板 PaletteWidget
add_hmi_test(tst_PaletteWidget)
# 阶段 E · E.4b 8 handle 缩放 ResizeHandleItem
add_hmi_test(tst_ResizeHandle)
# handle 视觉对齐调试
add_hmi_test(tst_HandleVisualAlignment)
# 阶段 D · D.2 历史曲线控件(QCustomPlot)
add_hmi_test(tst_Trend)
# 调试 QGraphicsObject Q_PROPERTY 行为
add_hmi_test(tst_GraphicsObjectProperty)
# 端到端 AlarmList 缩小失效调试
add_hmi_test(tst_AlarmListShrinkE2E)
# ★ E.8(2026-09-30):控件自定义底图 —— 22 个控件手绘太丑,用户要能换自己的图。
#   底图下沉到 ControlBase,图走下层(矢量层的液位/动画/状态色仍在上层),
#   相对路径按工程目录解析,加载失败回退手绘矢量而不是画灰底。
add_hmi_test(tst_ControlBackgroundImage)
# ★ F3(2026-09-30):手工改 JSON 加变量 —— 加载诊断能力。
#   F3 卖点是"加新变量 = 改 JSON 一行",实测发现报错不含变量名/字段、
#   未知字段静默丢弃、失败留半个工程三件事。见 tst_JsonLoadDiagnostics.cpp 头注释。
add_hmi_test(tst_JsonLoadDiagnostics)

# ★ 崩溃 dump 消费端(2026-10-14)。修的是"pending.txt 写了 28 天没人读"那条:
#   dumpDirectory() 依赖调用方先设好 org/app,而 main() 当时把元数据排在
#   install()/hasPendingDump()【后面】,于是写和读落在两个不同目录。
add_hmi_test(tst_CrashHandler)

option(HMI_ENABLE_COVERAGE "Enable coverage build" OFF)
