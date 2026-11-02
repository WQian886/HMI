# 阶段 A · A5 单元测试
# 用 Qt Test,跟主程序共用 core 源码

cmake_minimum_required(VERSION 3.21)
project(hmi_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
# ★ 2026-10-20:主工程 CMakeLists.txt 第 8 行有这条,测试工程【漏了】。
#   漏了它的表现是 qrc 源文件【被静默忽略】—— 不报错、不警告、不生成任何东西,
#   只有等运行时报"读不到 :/..." 才看得出少了点什么。
#   tst_Theme 正是靠它才能读到 hmi.qrc 里的样式表(与主程序同一份登记)。
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network SerialBus Sql PrintSupport UiTools)
# ★ 2026-10-24:找 Python 解释器(给 Q_OBJECT 判据检查用)。
#   硬编码 "python" 会在 CI 上炸:windows-2022 runner 上命令名可能是
#   python / py / python3,不一定叫 python。让 CMake 自己找。
#   找不到也不致命 —— 那两条测试标成 DISABLED,而不是让整个 ctest 失败。
find_package(Python3 COMPONENTS Interpreter QUIET)
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
    # ★ 2026-10-21:util/Paths —— 应用数据目录的唯一出处。
    #   Logging / AuditLog / UserStore / HistoryRecorder / CrashHandler
    #   现在都从它那儿拿目录(以前各算各的),所以它必须进 hmi_core。
    ${HMI_SRC_DIR}/src/util/Paths.cpp
    # ★ 2026-10-25:util/ValueCoercion —— 值类型适配。
    #   ControlBase(控件写回)和 ActionManager(报警联动)都要调它,
    #   所以它必须进 hmi_core,否则链接不过。
    ${HMI_SRC_DIR}/src/util/ValueCoercion.cpp
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
    # ★ 2026-10-19:WriteFailChecker 此前【一直】没进这个列表。
    #   变量引用扫描要证明"非阈值报警会被列成查不到的",就得有一个真的
    #   非阈值报警实例 —— 而唯一那种就是它。它是生产代码,不是测试替身,
    #   用真类才能证明"生产里那类报警确实落进查不到那个桶"。
    #   这也正是上面 AlarmSoundPlayer 那段注释说的那个坑:
    #   漏一行 = 链接期报一串"无法解析的外部符号",报错点在测试文件里,离病根很远。
    ${HMI_SRC_DIR}/WriteFailChecker.cpp
    # ★ 2026-10-19:变量引用扫描(阶段 1)
    ${HMI_SRC_DIR}/VariableRefScan.cpp
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
    # ★ 2026-10-19:变量配置对话框 —— 【两份 CMakeLists 都要登记】
    #   漏了这份的症状是链接期 LNK2019 undefined symbol,
    #   而 hmi/CMakeLists.txt 里明明已经加了 —— 两个独立的源文件列表,
    #   只加一个不会报任何配置错(这个坑在 WriteFailChecker 上踩过一次)。
    ${HMI_SRC_DIR}/src/plc/VariableConfigDialog.cpp
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
    # ★ 2026-10-24:报警【行】的呈现(行怎么造/怎么改状态/怎么上色/留多少行)。
    #   主画面 hmi.cpp 与运行时画布 AlarmListControl 现在都调它 ——
    #   原来两边各写一份,主画面那份【少写了状态列】,于是主画面报警表整张废了
    #   (状态列空白/确认无效/无配色/不闪烁/"确认"菜单恒灰)。
    ${HMI_SRC_DIR}/src/alarm/AlarmTablePresenter.cpp
    # ★ 2026-10-25:"报警→联动"这根接线的唯一出处。
    #   抽到共享模块是因为写在 hmi.cpp 里的接线只能被测试【复制一份】跑 ——
    #   测的是副本(hmi.cpp 不在 hmi_core 里)。两边调同一个函数,
    #   测试绿了才说明生产那根线也是通的。
    ${HMI_SRC_DIR}/src/alarm/ActionWiring.cpp
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
    # ★ 2026-10-20:通讯帧监视的数据层(纯 QtCore,一行界面代码都没有)
    #   【为什么必须进这个列表】tst_CommTraceModel 调的是它的真实实现,
    #   不是副本。漏了它 = 链接期 LNK2019 undefined symbol,
    #   而报错点全在测试文件里,离病根(这一行没加)很远。
    #   同 WriteFailChecker / AlarmSoundPlayer 那两段注释说的同一个坑。
    ${HMI_SRC_DIR}/src/comm/CommTraceModel.cpp
    # ★ G-20(2026-10-30):调试面板请求解析。
    #   【必须进 hmi_core】hmi.cpp 不在里面,所以调试面板那一行的解析
    #   以前只能"抄一份跑"—— 而这份逻辑抄错了就是【广播写】。
    #   现在测试调的是生产用的同一个 parseRequestSpec。
    ${HMI_SRC_DIR}/src/plc/ModbusRequestParser.cpp
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
# ★ 2026-10-19:变量配置对话框 —— 让变量 plcId 有 UI 入口。
#   最要紧的两条:删被引用的变量【拒删】并逐条列出引用者;
#   以及【画面绑定查不到】这句话必须一直在界面上。
add_hmi_test(tst_VariableConfigDialog)
# ★ 2026-10-16:菜单栏 —— 逐个菜单项验证它连到了【正确的那个槽】
#   本来这段 UI 逻辑写在 hmi.cpp 里,测试链不到主程序(hmi_core 不含 hmi.cpp),
#   只能复制一份跑 —— 那样测的是副本。抽成 MenuBuilder 后两边调同一个函数。
add_hmi_test(tst_MenuBar)
# ★ 2026-10-20:深色主题样式表。
#   【为什么它不能直接用 add_hmi_test】那函数只编 tst_Xxx.cpp 一个源文件,
#   而本测试要验证"qss 真的编进二进制了"—— 资源不进这个目标,
#   :/hmi/theme/... 在测试里就【根本不存在】,于是测试恒红。
#
#   ★★ 这里踩过两次,都记下来:
#     ① 把 hmi.qrc 直接列进 add_executable 的源文件列表 → CMake 【静默忽略】
#        (构建日志一行警告都没有,测试只是"读不到资源"这一条红,
#         看不出是构建系统压根没理它)。qrc 光列出来不会自动编,
#         必须显式 AUTORCC ON。
#     ② 改用 qt_add_resources(... FILES <绝对路径>) → CMake 直接 configure 报错:
#        "was specified with an absolute path and is used in a Qt resource",
#        要么补 QT_RESOURCE_ALIAS,要么继续用 qrc。
#     结论:继续用 qrc。它同时是主程序和本测试的资源清单 ——
#     【只有一份登记】,两处不可能读到不同的东西。
add_executable(tst_Theme tst_Theme.cpp)
set_source_files_properties(${HMI_SRC_DIR}/hmi.qrc PROPERTIES AUTORCC ON)
target_sources(tst_Theme PRIVATE ${HMI_SRC_DIR}/hmi.qrc)
target_link_libraries(tst_Theme PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_Theme PRIVATE /utf-8)
# 徽章字号的真实所在是 hmi.cpp(不是 qss —— inline 样式表会让 qss 失效),
# 要钉住它只能读源码文本,所以要 HMI_SRC_DIR。同 tst_MenuBar 的做法。
target_compile_definitions(tst_Theme PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
add_test(NAME tst_Theme COMMAND tst_Theme)
# ★ 2026-10-20:两条路径宏,给 tst_MenuBar 里那条"菜单槽必须在 slots: 段"的用例。
#   它现读 MenuBuilder.cpp(取槽名)和 hmi.h(查访问说明符段)——
#   不抄一份槽名表是关键:抄了就又变成"测副本",生产代码改了测试照样绿。
#   加宏而不是写死相对路径:cwd 由 ctest / 手跑 / CI 三处决定,写死必有一处错。
target_compile_definitions(tst_MenuBar PRIVATE
    HMI_SRC_DIR_STRING="${HMI_SRC_DIR}"
    MENU_SRC_DIR_STRING="${HMI_SRC_DIR}/src"
)
# ★ P1-d(2026-10-20):Modbus 调试行的【形状】。
#   不用 add_hmi_test —— 它要 HMI_SRC_DIR_STRING 读 hmi.ui 和 qss 源码文本。
#   (同 tst_Theme 的理由:hmi_core 不编 hmi.cpp,建不出主窗口那一排控件。)
#   这里【不要】把 hmi.qrc 加进来:本测试读的是磁盘上的文件,
#   不是 :/ 资源。加了反而让人以为测的是二进制里那份。
add_executable(tst_ModbusPanel tst_ModbusPanel.cpp)
target_link_libraries(tst_ModbusPanel PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_ModbusPanel PRIVATE /utf-8)
target_compile_definitions(tst_ModbusPanel PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
add_test(NAME tst_ModbusPanel COMMAND tst_ModbusPanel)

# ★ 2026-10-19:派生变量不许进轮询。
#   它 address=0(没有寄存器),而 onTick 原来没有 isDerived 过滤 ——
#   于是去读 plc1 的 0 号寄存器,回包把算出来的值覆盖掉。
add_hmi_test(tst_PollSchedulerDerived)
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
# ★ P0(2026-10-20):tst_UIPermission 要读 hmi.cpp / EditorMainWindow.cpp 的源码文本,
#   验"主画面那几个按钮到底绑没绑权限"(hmi_core 不编 hmi.cpp,链不到真实控件,
#   读源码是唯一能钉住"接线"的形状,同 tst_MenuBar / tst_Theme)。
target_compile_definitions(tst_UIPermission PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
# 阶段 B · B5
add_hmi_test(tst_AuditLog)
add_hmi_test(tst_AuditE2E)
# 阶段 C · C.0
add_hmi_test(tst_ProjectSchema)
# 阶段 C · C.1 协议抽象层
add_hmi_test(tst_IPollDriver)
# ★ 2026-10-21:BUGFIX-C1 回归 —— "从站失败之后应用必须还能继续轮询"
#   2026-10-21 查"16.5 小时零轮询流量"查到的根因:协议级失败不发 readFinished,
#   而 PollScheduler 清 inFlight_ 的唯一入口就是 readFinished
#   ⇒ 一次失败,那台 PLC 就再也收不到任何读请求,而且日志里一声不吭。
#   改之前这条路径【一次都没被测过】(它压根不产生可观测信号),
#   现在用自己写的最小从站把它钉成断言。
add_hmi_test(tst_ModbusFailureRecovery)
# ★ 2026-10-21:写响应的 echo 地址必须真的跟请求比对。
#   改之前 ModbusMaster::handleWriteResponse 里那行是
#   `echoAddr != quint16(req->reply ? /*addr 通过 reply 拿*/0 : 0)` ——
#   三元两分支【都是 0】,占位从没填上,于是它判的其实是 "echoAddr != 0"。
#   后果:每一次正常的写都被报成"不一致"(内容是假的,qCDebug 默认还不落盘),
#   而"回包地址恰是 0"这种真的不一致反而一声不吭。
#   ★ 这个测试自己开一个按 Modbus 协议现写的从站(不跟被测代码共享逻辑),
#     回什么地址由用例显式指定 —— 免得变成"自己拿自己当参照"。
add_hmi_test(tst_ModbusEchoAddress)
# ★ 2026-10-26:写【排队】,不再跟读抢那个在途槽。
#   2026-10-26 短跑实测(soaks/sweep_m85)挖出来的:
#     604 次写尝试全部被拒、0 帧到达从站,而 547 次读全部成功;
#     写被拒到读回包到达平均只隔 2.18 ms(94.2% 在 5 ms 内)。
#     而同样配置的另外六轮写一次都没被拒 —— 决定成败的是两个 100ms
#     节拍的【启动相位差】,它一辈子不变 ⇒ 开机掷一次硬币,
#     掷到坏的那一面 24 小时一个字节都写不出去。
#   ★ 危险的地方在于:读照常成功 ⇒ 长跑的活体判据照样绿
#     ⇒ 只有【从站那侧收到的字节】才能抓住它,应用日志和读计数都抓不住。
add_hmi_test(tst_ModbusWriteQueue)
# ★ 2026-10-23:单 in-flight 限流【不得】被当成错误。
#   这条钉的是一个已经造成过 P0 的契约(2026-10-23 修的 G-11):
#   Modbus 协议串行,onTick 一个 tick 连发多帧时第 2 帧起必然撞上限流。
#   限流是【内部调度细节】不是错误 —— 它一旦 emit errorOccurred,
#   在 hmi.cpp 那边就变成「改状态 Failed + 弹模态 QMessageBox」,
#   于是任何【地址不连续】的工程第一次轮询之后永久停摆,而全程零告警。
#   ★ 为什么以前 92 条用例全绿也没抓到:调度器层的回滚是用【假 driver】测的,
#     假 driver 上方没有 hmi.cpp —— 没有任何一条用例穿过 errorOccurred 这根线。
#   不用 add_hmi_test:它要 HMI_SRC_DIR_STRING 读 hmi.cpp 源码文本
#   (UI 层的弹窗守卫没法在进程内断言 —— QMessageBox 是模态阻塞的,
#    弹出来测试自己就卡住了,跟现场同一个坑)。
add_executable(tst_ThrottleNotAnError tst_ThrottleNotAnError.cpp)
target_link_libraries(tst_ThrottleNotAnError PRIVATE
    hmi_core
    Qt6::Test
)
target_compile_options(tst_ThrottleNotAnError PRIVATE /utf-8)
target_compile_definitions(tst_ThrottleNotAnError PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
add_test(NAME tst_ThrottleNotAnError COMMAND tst_ThrottleNotAnError)
# ★ 2026-10-21:单实例键可指定 —— "不动上一轮长跑"方案的地基。
#   不用 add_hmi_test:SingleInstance.cpp 不在 hmi_core 里
#   (主工程那边是靠 GLOB 兜底捡进去的,测试工程是显式列表),
#   所以这个目标得自己带一份源文件 —— 跟 tst_Theme 一个路子。
add_executable(tst_SingleInstanceKey
    tst_SingleInstanceKey.cpp
    ${HMI_SRC_DIR}/src/instance/SingleInstance.cpp
)
target_link_libraries(tst_SingleInstanceKey PRIVATE
    hmi_core
    Qt6::Test
)
target_compile_options(tst_SingleInstanceKey PRIVATE /utf-8)
add_test(NAME tst_SingleInstanceKey COMMAND tst_SingleInstanceKey)
# ★ G-25(2026-10-30):单实例消息【只处理一次】。
#   共享内存里的消息以前【读完不记已读】⇒ 主实例每 500ms 把同一条消息
#   重放一遍 ⇒ 窗口每 0.5 秒被抢一次焦点 + 一天 60 万行日志。
#   同样不用 add_hmi_test:SingleInstance.cpp 不在 hmi_core 里。
add_executable(tst_SingleInstanceMessage
    tst_SingleInstanceMessage.cpp
    ${HMI_SRC_DIR}/src/instance/SingleInstance.cpp
)
target_link_libraries(tst_SingleInstanceMessage PRIVATE
    hmi_core
    Qt6::Test
)
target_compile_options(tst_SingleInstanceMessage PRIVATE /utf-8)
add_test(NAME tst_SingleInstanceMessage COMMAND tst_SingleInstanceMessage)
# ★ 2026-10-21:应用数据目录(HMI_DATA_DIR 覆盖)
#   长跑实例要与"已经在跑的实例"各写各的 —— 日志 / history.db / 审计日志 /
#   崩溃标记,任意一个共用都会互相写坏。
#   只测 util::appDataDir() 是弱测试:真正的风险是"某个调用方忘了跟着走",
#   那种漏网的调用方在【没设覆盖】时完全看不出来。
#   所以这条测试逐个点名 users.json / 审计日志 / 崩溃 dump / history.db / 日志目录。
add_hmi_test(tst_Paths)
# ★ 2026-10-21:轮询统计行的【两方契约】。
#   2026-10-21 实测:长跑的启动自检等满 120 秒一条统计行都没匹配上,
#   判成"没资格跑" —— 而那 120 秒里程序一直在正常轮询(成功 271 次、失败 0 次)。
#   根因:程序打的 "发出= 271"(QDebug 在相邻两项之间插了空格)与判据要的
#   "发出=271"对不上,而失配的表现是【判不出】,不是【判错】,最难发现。
#   ★ 这个测试把 scripts/long_run.ps1 里的 $rePollStats 【原样读出来】,
#     再去匹配程序【真的打出来的那一行】—— 一边改了另一边没改,这里立刻红。
#   单独钉程序那一侧没用:两边照样能各改各的、一起漂走。
#   (同 tst_MenuBar / tst_Theme 的理由:要路径宏读磁盘上的文件。)
add_executable(tst_PollStatsFormat tst_PollStatsFormat.cpp)
target_link_libraries(tst_PollStatsFormat PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_PollStatsFormat PRIVATE /utf-8)
target_compile_definitions(tst_PollStatsFormat PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
add_test(NAME tst_PollStatsFormat COMMAND tst_PollStatsFormat)
# ★ 2026-10-27:写侧统计行的【两方契约】。
#   SOAK-W(写侧闸)量的是程序自己报出来的写侧统计行 ——
#   读侧活着不代表写能落地:BUGFIX-E1 修之前是"读 547 次全成功、
#   写 604 次全被拒、0 帧到达从站",而当时的活体判据(SOAK-6)【只看读】。
#
#   ★ 这条比轮询统计那条【更要紧】,因为写侧统计有 17 个字段:
#     判据会把它们【分开算】(恒等式①用已发出/成功/失败/在途写,
#     恒等式②用排队/排队发出/排队作废/在队)——
#     取错一格 ⇒ 恒等式必然残差 ⇒ 判据【看着绿、其实在量错东西】。
#     而"看着绿"比"判红"危险得多。
#   ★ 所以这个测试除了"能不能匹配上",还要钉"取出来的数【逐个】对不对得上"。
#   ★ 方法同 tst_PollStatsFormat:把 scripts/long_run.ps1 里的 $reWriteStats
#     【原样读出来】,两边【物理上】就是同一份,漂不了。
add_executable(tst_WriteStatsFormat tst_WriteStatsFormat.cpp)
target_link_libraries(tst_WriteStatsFormat PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_WriteStatsFormat PRIVATE /utf-8)
target_compile_definitions(tst_WriteStatsFormat PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
add_test(NAME tst_WriteStatsFormat COMMAND tst_WriteStatsFormat)
# ★ 2026-10-25:报警联动的【写通路】端到端。
#   24 小时长跑实测(soaks/20261021_164804)挖出来的:
#     工程里配的"温度高 → 速度写 0"(也就是"停机")【24 小时一次都没执行过】。
#     根因:write_value 的类型跟变量声明类型对不上 → setValue 返 Rejected
#     → 不 markDirty → WriteScheduler 看不到它 → 从站 FC6/FC16 计数 = 0。
#   而"界面正常、报警正常响、审计正常记'已执行'"—— 从站那边一个字节没收到。
#
#   ★ 不用 add_hmi_test:它有一条用例要读 hmi.cpp 源码文本
#     (确认 hmi.cpp 真的调了共享接线函数),所以要 HMI_SRC_DIR_STRING。
#     同 tst_Theme / tst_ModbusPanel 的理由。
#   ★ 行为部分调的是【共享函数】qwhmi::alarm::bindActionToAlarms,
#     不是自己 connect 一份 —— 抄一份就是"测副本"(BUGFIX-C3 的形状)。
add_executable(tst_ActionWrite tst_ActionWrite.cpp)
target_link_libraries(tst_ActionWrite PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_ActionWrite PRIVATE /utf-8)
target_compile_definitions(tst_ActionWrite PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")
add_test(NAME tst_ActionWrite COMMAND tst_ActionWrite)
# ★ 2026-10-21:PowerShell 脚本里的【格式化占位符漏替换】静态检查。
#   这不是 C++ 测试,是一条跑脚本的 ctest 目标 —— 因为它要检的东西
#   本来就不在 C++ 里(在 .ps1 里)。
#   2026-10-21 实测踩到三处,全在 scripts/long_run.ps1 的 Get-LivenessVerdict:
#     · "★ 成功读 +{0} 次"          压根没写 -f
#     · "★ 发出 +{0} 次" + "在途变量 = {1}" -f …   ← -f 的优先级比 + 高,
#       于是只有最后一段被格式化,前面两段的 {0}/{1} 原样留着。
#   后果【不报错】:判据状态判对了,但给人看的那句话里的数字全缺。
#   而且后两处走的正是"真出问题时才走的那一支" —— 平时跑得再顺也发现不了。
#   ⇒ 静态扫一遍(几秒),比在报告里读到一个空数字便宜得多。
add_test(NAME tst_PsFormatLeak
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${HMI_SRC_DIR}/../scripts/check_ps_format_leak.ps1)
# ★ 2026-10-21:长跑看门狗(self_test)。
#   上一轮 24 小时验收批在 00:40 死了,58 个采样点之后没人知道,
#   README 还"跑批中"挂了两天 —— 那条记录自己写下的结论是
#   "跑批是 session-only,崩了无人发现"。
#   这一轮把 harness 改成了 detached 进程,还剩最后一层:
#   harness 中途死了要等到第二天才有人知道。
#   看门狗补的就是这一层,而且它盯的不只是"进程还在不在":
#   上一轮真正的死法是【进程活着但一次都没跟 PLC 说过话】,
#   光看进程存活抓不到它 —— 所以看门狗盯的是"成功读次数还在不在涨"。
#   它的自测造假现场,验的是"该报的报、不该报的不报":
#   健康/harness 死/verdict 停更/被测退/从站死/业务不涨/没统计行/
#   跑完了不报/状态异常/刚启动不误报/旧格式/连日志都没有。
add_test(NAME tst_SoakWatchdog
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${HMI_SRC_DIR}/../scripts/watch_soak.ps1 -SelfTest)
# ★ 2026-10-24:sync_vcxproj.py 的 Q_OBJECT 自动判据 + 负向对照
#   这不是 C++ 测试,是两条跑脚本的 ctest 目标 —— 因为它要检的东西
#   本来就不在 C++ 里(在 scripts/sync_vcxproj.py 里)。
#
#   【为什么必须有】
#     G-12 查出:Paths.cpp / Paths.h / IPlcDriverProvider.h 三个新文件
#     三个星期没进 hmi.vcxproj,MSBuild 链接期 LNK2001 × 5。
#     之所以没人发现,是因为 CMake 有 GLOB 兜底(CI 只跑 CMake),
#     而 sync_vcxproj.py 以前【判断不了 Q_OBJECT】,每次 dry-run 都刷
#     "可能缺 QtMoc,请 Wqian 手动确认" + 21 条噪声。
#     噪声的害处不是烦,是让人习惯性跳过这一段 —— 真缺的时候也跟着被跳过。
#     现在判据自动了(2026-10-24),那就得【被守】。
#
#   【这两条各守什么】
#     tst_QObjectDetection — 正向:判据在真仓库上是否正确
#       (探针覆盖假阳性/假阴性两个方向 + 全仓 vcxproj↔filters 对齐)
#     tst_QObjectNegCtl   — 反向:把守卫拆掉,断言测试【变红】
#       ★ 绿的测试不一定是有效的测试 ★
#         判据和仓库"同时错"的时候,正向测试也是绿的。
#         必须让它咬自己一次:拆守卫 → 期望红 → 装回去 → 期望绿。
if(Python3_Interpreter_FOUND)
    add_test(NAME tst_QObjectDetection
        COMMAND ${Python3_EXECUTABLE} ${HMI_SRC_DIR}/../scripts/check_qobject_detection.py)
    add_test(NAME tst_QObjectNegCtl
        COMMAND ${Python3_EXECUTABLE} ${HMI_SRC_DIR}/../scripts/negctl_qobject.py)
else()
    # 找不到 Python 就把这两条【标掉】,而不是让 ctest 报失败 ——
    # 判据检查是守门,不是被测对象;守门缺席不该让整条流水线红。
    message(WARNING
        "没找到 Python3 解释器 —— tst_QObjectDetection / tst_QObjectNegCtl "
        "被禁用(它们守的是 sync_vcxproj.py 的 Q_OBJECT 判据)")
    add_test(NAME tst_QObjectDetection
        COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: 没有 Python3 解释器")
    set_tests_properties(tst_QObjectDetection PROPERTIES DISABLED TRUE)
    add_test(NAME tst_QObjectNegCtl
        COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: 没有 Python3 解释器")
    set_tests_properties(tst_QObjectNegCtl PROPERTIES DISABLED TRUE)
endif()
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
# ★ 2026-10-30:G-19 —— 日志目录打不开 ⇒ 每条日志都重试一次打开
#   (stderr 刷爆 + CPU 白烧 + 日志静默全丢)。
#   不用 add_hmi_test:它要自己带 TIMEOUT ——
#   万一哪天 handler 又被改成会卡住的形态,没有超时上限的话
#   ctest 会一直挂着,CI 上要等全局超时(120 分钟)才知道出了事。
#   60 秒足够区分"卡住"和"正常跑完"(本文件正常跑完 < 1 秒)。
add_executable(tst_LoggingFileUnavailable tst_LoggingFileUnavailable.cpp)
target_link_libraries(tst_LoggingFileUnavailable PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_LoggingFileUnavailable PRIVATE /utf-8)
add_test(NAME tst_LoggingFileUnavailable COMMAND tst_LoggingFileUnavailable)
set_tests_properties(tst_LoggingFileUnavailable PROPERTIES TIMEOUT 60)
# ★ 2026-10-30:Qt 消息处理器的【重入语义】基线。
#   G-19 那条"死锁"结论就是被这个探针推翻的 ——
#   Qt 6.8.3 不会重入自定义 handler(handler 内部再打一条日志,
#   Qt 会把它交给【默认 handler】)。
#   ★ 所以它必须留成【断言】,而不是一次性探针:
#     哪天 Qt 升级改了这条语义,这里会先红,提醒我们重新审视
#     Logging.cpp 里那把非递归 QMutex 和"重入保护"的定位,
#     而不是等到现场发现程序不动了。
add_executable(tst_QtReentryProbe tst_QtReentryProbe.cpp)
target_link_libraries(tst_QtReentryProbe PRIVATE hmi_core Qt6::Test)
target_compile_options(tst_QtReentryProbe PRIVATE /utf-8)
add_test(NAME tst_QtReentryProbe COMMAND tst_QtReentryProbe)
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
# ★ G-24(2026-10-30):表达式解析安全闸 —— 深度/长度/节点数 + 非有限值
#   【为什么单独一个文件】体检 P1-6 和 P2-1 讲的是同一件事:
#   "一行人给的文本能把进程打爆 / 能算出没人拦的数"。
#   钉住它的用例必须和"正常表达式怎么算"分开写 ——
#   混在一起,后加的用例会稀释前一条的失败信号。
add_hmi_test(tst_ExpressionParserLimits)
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
# ★ 2026-10-24:报警【行】的呈现(行怎么造/怎么改状态/怎么上色/留多少行)。
#   【为什么必须有】主画面 hmi.cpp 的报警表 2026-10-14 之后【整张是废的】:
#   状态列从来没被创建过 → 状态列空白 / 确认无效 / 无配色 / 不闪烁 /
#   "确认"菜单项恒灰。而当时 68 个测试全绿 —— 因为主画面那套【零测试覆盖】。
#   现在"行怎么造出来"本身成了可测的纯逻辑,两套 UI 共用。
add_hmi_test(tst_AlarmTablePresenter)
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
# ★ G-20(2026-10-30):调试面板那一行的解析 —— 从站号 0 = 广播写。
#   纯逻辑,无界面,QTEST_GUILESS_MAIN 就够。
add_hmi_test(tst_ModbusRequestParser)
# ★ G-21 / G-22(2026-10-30):"写被拒"必须被看见 ——
#   ① 运行时画面上没接线的控件,点一下绝不能本地翻(画面直接骗人);
#   ② 编辑器里的本地预览必须保留(否则把一个静默失败换成另一个);
#   ③ writeRejected 信号在基类上,四个控件都发得出;
#   ④ RuntimeCanvas 是生产环境里唯一接这个信号的地方 —— 它必须让操作员看得见。
add_hmi_test(tst_ControlWriteRejection)
# ★ G-23(2026-10-30):派生变量 ——
#   ① 存得住:expression 必须进 JSON(不存的话,存盘再打开它就变成
#      一个 address=0 的普通变量,去读 plc1 的 0 号寄存器);
#   ② 算得出:源变量一变,派生量必须自己跟着变(以前一行 connect 都没有);
#   ③ 派生量不许冒充寄存器:外部写一律拒收(否则 WriteScheduler 会
#      把它发到 plc1 的 0 号寄存器 —— 那是真的现场寄存器)。
add_hmi_test(tst_DerivedVariable)
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

# ★ 2026-10-20:通讯帧监视器(调试区第三页「通讯监视」)
#   两个测试分家:
#     tst_CommTraceModel —— 数据层(行淘汰/暂停/清空/统计算术)。
#       纯 QtCore,所以用 QTEST_GUILESS_MAIN,没有显示设备也能跑。
#     tst_CommTracePanel —— 页面形状(表格钉死 160/底下一行 40/
#       高度不写死在 .ui/信号接上了没)。
#       读 .ui 和 hmi.cpp 的【文本】,同 tst_ModbusPanel 的老问题:
#       hmi_core 不编 hmi.cpp,建不出主窗口那一排控件;
#       而 .ui 写错属性是静默的,读文本能把"写了但写歪了"变成"查得到"。
add_hmi_test(tst_CommTraceModel)
add_hmi_test(tst_CommTracePanel)
target_compile_definitions(tst_CommTracePanel PRIVATE HMI_SRC_DIR_STRING="${HMI_SRC_DIR}")

option(HMI_ENABLE_COVERAGE "Enable coverage build" OFF)
