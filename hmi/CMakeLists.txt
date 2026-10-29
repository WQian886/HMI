cmake_minimum_required(VERSION 3.21)
project(hmi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ★ Qt 查找路径。
#   【原来是无条件硬编码 "D:/Qt/6.8.3/msvc2022_64"】
#   改动的理由不是"它在 CI 上会坏" —— 实测过,即使硬编码成一个
#   根本不存在的路径,find_package 仍能从 CMAKE_PREFIX_PATH 环境变量
#   回落找到 Qt 6.8.3(CI 上 install-qt-action 设的就是那个环境变量)。
#   理由是这个文件要进 CI,而 CI 机器上根本没有 D 盘 Qt:
#   一句写死的本机路径留在里面,读的人会以为"CI 依赖某人的 D 盘",
#   以后谁把它当真去排查,就是白查一场。
#   【怎么做到既不改行为又不写死】优先用环境变量(本机 build.ps1 正是
#   设了 $env:QTDIR 之后才 --preset Qt-Debug 的,那个 preset 把它
#   映射到 CMAKE_PREFIX_PATH);环境变量没有时才退回本机默认路径。
if(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{CMAKE_PREFIX_PATH})
    set(CMAKE_PREFIX_PATH "$ENV{CMAKE_PREFIX_PATH}")
elseif(NOT CMAKE_PREFIX_PATH AND DEFINED ENV{QTDIR})
    set(CMAKE_PREFIX_PATH "$ENV{QTDIR}")
elseif(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")   # 本机兜底,CI 上不会走到
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Widgets PrintSupport SerialBus Sql)

qt_standard_project_setup()

# 显式列出全部源/头文件 —— 任何新增都必须更新这里
set(SOURCES
    main.cpp
    hmi.cpp
    TcpClient.cpp
    ModbusDemo.cpp
    ModbusMaster.cpp   # 阶段 2 第 4 课补图 —— 不加进 SOURCES 会导致链接错误
    Variable.cpp       # 阶段 3.1:Variable 数据模型
    VariableManager.cpp # 阶段 3.1:变量管理器
    PollScheduler.cpp   # 阶段 3.3:定时轮询调度器
    Binding.cpp         # Binding 控件绑定
    WriteScheduler.cpp  # 写入调度
    ActionManager.cpp   # 动作映射
    Alarm.cpp           # 告警
    AlarmManager.cpp    # 告警管理器
    ThresholdAlarmChecker.cpp # 阈值告警检查
    AlarmSoundPlayer.cpp # ★ F5:报警声音/蜂鸣(等级区分 + 确认即静音 + 静默时段)
    WriteFailChecker.cpp # 写入失败检查
    JsonSerializer.cpp  # JSON 序列化
    Project.cpp         # 项目(保存/加载)
    # ★★ 2026-10-19:变量引用扫描(删变量前查"还有谁在用",纯只读)
    VariableRefScan.cpp
    # ★ D.2(2026-09-20):QCustomPlot 曲线控件(LGPL v2.1,3rdparty/qcustomplot/)
    3rdparty/qcustomplot/qcustomplot.cpp
    # ★ E.1(2026-09-20):控件基类 + 第 1 个显示控件 LabelControl(QGraphicsObject 派生)
    src/widgets/base/ControlBase.cpp
    src/widgets/LabelControl.cpp
    # ★ E.1a-2(2026-09-20):NumberDisplay + TextDisplay(2 个显示控件)
    src/widgets/NumberDisplay.cpp
    src/widgets/TextDisplay.cpp
    # ★ E.1a-3(2026-09-20):Bar + Gauge + LED(3 个显示控件)
    src/widgets/BarControl.cpp
    src/widgets/GaugeControl.cpp
    src/widgets/LedControl.cpp
    # ★ E.1a-4(2026-09-20):Image + Rectangle + Line + Group(4 个元件控件)
    src/widgets/ImageControl.cpp
    src/widgets/RectangleControl.cpp
    src/widgets/LineControl.cpp
    src/widgets/GroupControl.cpp
    # ★ E.1b(2026-09-20):Button + Toggle + Slider + InputEdit(4 个控制控件)
    src/widgets/ButtonControl.cpp
    src/widgets/ToggleControl.cpp
    src/widgets/SliderControl.cpp
    src/widgets/InputEditControl.cpp
    # ★ E.1b-2(2026-09-20):Trend + Table + AlarmList(3 个数据控件)
    src/widgets/TrendControl.cpp
    src/widgets/TableControl.cpp
    src/widgets/AlarmListControl.cpp
    # ★ E.1c(2026-09-28):5 个工业控件(补齐 E 阶段 22 控件的最后 5 个)
    src/widgets/TankControl.cpp
    src/widgets/ValveControl.cpp
    src/widgets/PipeControl.cpp
    src/widgets/PumpControl.cpp
    src/widgets/MotorControl.cpp
    # ★ E.1c:转子旋转动画(泵/电机共用)
    src/widgets/base/SpinAnimator.cpp
    # ★ E.2(2026-09-21):属性面板 PropertySheet + PropertyChangeCommand(QUndoCommand)
    src/editor/PropertySheet.cpp
    src/editor/PropertyChangeCommand.cpp
    # ★ E.4d-follow-8(2026-09-29):多控件几何 Undo(9 个对齐/等尺寸动作)
    src/editor/GeometryChangeCommand.cpp
    # ★ E.7(2026-09-30):复制/粘贴/删除的 Undo(增删合一,存 JSON 快照)
    src/editor/ControlListCommand.cpp
    # ★ E.2 周 11:EditorMainWindow 骨架
    src/editor/EditorMainWindow.cpp
    # ★ E.4a:控件调色板 PaletteWidget
    src/editor/PaletteWidget.cpp
    # ★ E.4b:8 handle 缩放 ResizeHandleItem
    src/editor/ResizeHandleItem.cpp
    # ★ E.4d 续-3:多选组 handle 容器 GroupHandleItem
    src/editor/GroupHandleItem.cpp
    # ★ E.4d 续:选中态视觉统一 SelectionFrameItem(蓝色虚线边框,paint() 视觉延后)
    src/editor/SelectionFrameItem.cpp
    # ★ E.5(2026-09-29):运行时画布 RuntimeCanvas —— 主窗口显示编辑器存的画面
    src/editor/RuntimeCanvas.cpp
    # ★ F4-a(2026-10-08):报警配置对话框(只读 + 草稿导出)
    src/alarm/AlarmConfigDialog.cpp
    src/alarm/AlarmSoundDialog.cpp     # ★ F5:报警声音设置对话框
    src/alarm/AlarmRowPalette.cpp       # ★ 2026-10-14:报警行配色唯一权威(两套报警 UI 共用)
    src/alarm/AlarmTablePresenter.cpp   # ★ 2026-10-24:报警【行】的呈现(行怎么造/怎么改/留多少行)
    # ★ 2026-10-25:"报警触发了要执行联动"这根接线的唯一出处。
    #   抽出来是因为写在 hmi.cpp 里的接线只能被测试【复制一份】跑 ——
    #   测的是副本。而"接线断了"在这个模块上最严重:
    #   界面正常、报警正常响、审计记"已执行",而 PLC 一个字节都没收到。
    src/alarm/ActionWiring.cpp
    # ★ 2026-10-25:值类型适配(把"来路不明的 QVariant"转成变量声明的精确类型)
    #   控件写回和报警联动共用这一份 —— 见 src/util/ValueCoercion.h 的文件头。
    src/util/ValueCoercion.cpp
    # ★ 2026-10-19:变量配置对话框 —— 让变量 plcId 有 UI 入口(多 PLC 2b 的收尾缺口)
    src/plc/VariableConfigDialog.cpp
    # ★ 2026-10-10:协议下拉框"未实现"标注(置灰 + tooltip)
    #   注:src/drivers/ 下的文件目前靠下面的 GLOB 兜底捡进来,
    #   显式写出来是为了守住"新文件必须登记"的约定。
    src/drivers/ProtocolAvailabilityUi.cpp
    # ★ F4(2026-10-08):报警历史查询视图 —— 填掉 D.3 留的占位 tab。
    #   注:src/history/ 下的文件目前靠下面的 GLOB 兜底捡进来(AlarmRecorder 等
    #   都没写进显式列表)。这一条显式写出来是为了守住"新文件必须登记"的约定。
    src/history/AlarmHistoryView.cpp
    # ★ 2026-10-16:菜单栏搭建。单独一个文件的原因和 ProtocolAvailabilityUi 同款 ——
    #   写在 hmi.cpp 里的 UI 逻辑测试只能复制一份跑,测的是副本;
    #   而"点'打开工程'弹出'保存工程'框"这种错连,副本测试【抓不到】。
    src/ui/MenuBuilder.cpp
    # ★ 2b(2026-10-19):多 PLC 连接管理。src/plc/ 目前靠 GLOB 兜底也能捡到,
    #   显式写出来是为了守住"新文件必须登记"的约定(见上面 MenuBuilder 那段)。
    src/plc/PlcConnectionManager.cpp
    # ★ 2026-10-20:通讯帧监视器(调试区第三页"通讯监视")。
    #   数据层,纯 QtCore,一行界面代码都没有 —— 所以能在无界面测试里跑。
    src/comm/CommTraceModel.cpp
)

set(HEADERS
    hmi.h
    TcpClient.h
    ModbusDemo.h
    ModbusMaster.h
    Variable.h
    VariableManager.h
    PollScheduler.h
    Binding.h
    WriteScheduler.h
    ActionManager.h
    Alarm.h
    AlarmManager.h
    ThresholdAlarmChecker.h
    AlarmSoundPlayer.h    # ★ F5:报警声音/蜂鸣
    WriteFailChecker.h
    JsonSerializer.h
    Project.h
    # ★ D.2:QCustomPlot 头文件
    3rdparty/qcustomplot/qcustomplot.h
    # ★ E.1:控件基类 + 第 1 个显示控件 LabelControl
    src/widgets/base/ControlBase.h
    src/widgets/LabelControl.h
    # ★ E.1a-2:NumberDisplay + TextDisplay
    src/widgets/NumberDisplay.h
    src/widgets/TextDisplay.h
    # ★ E.1a-3:Bar + Gauge + LED
    src/widgets/BarControl.h
    src/widgets/GaugeControl.h
    src/widgets/LedControl.h
    # ★ E.1a-4:Image + Rectangle + Line + Group
    src/widgets/ImageControl.h
    src/widgets/RectangleControl.h
    src/widgets/LineControl.h
    src/widgets/GroupControl.h
    # ★ E.1b:Button + Toggle + Slider + InputEdit
    src/widgets/ButtonControl.h
    src/widgets/ToggleControl.h
    src/widgets/SliderControl.h
    src/widgets/InputEditControl.h
    # ★ E.1b-2:Trend + Table + AlarmList
    src/widgets/TrendControl.h
    src/widgets/TableControl.h
    src/widgets/AlarmListControl.h
    # ★ E.1c(2026-09-28):工业控件
    src/widgets/TankControl.h
    src/widgets/ValveControl.h
    src/widgets/PipeControl.h
    src/widgets/PumpControl.h
    src/widgets/MotorControl.h
    # ★ E.1c:转子旋转动画
    src/widgets/base/SpinAnimator.h
    # ★ E.2:PropertySheet + PropertyChangeCommand
    src/editor/PropertySheet.h
    src/editor/PropertyChangeCommand.h
    # ★ E.4d-follow-8:多控件几何 Undo
    src/editor/GeometryChangeCommand.h
    # ★ E.7:复制/粘贴/删除的 Undo
    src/editor/ControlListCommand.h
    # ★ E.2 周 11:EditorMainWindow
    src/editor/EditorMainWindow.h
    # ★ E.4a:PaletteWidget
    src/editor/PaletteWidget.h
    # ★ E.4b:ResizeHandleItem
    src/editor/ResizeHandleItem.h
    # ★ E.4d 续-3:多选组 handle 容器 GroupHandleItem
    src/editor/GroupHandleItem.h
    # ★ E.4d 续:选中态视觉统一 SelectionFrameItem
    src/editor/SelectionFrameItem.h
    # ★ F4-a(2026-10-08):报警配置对话框
    src/alarm/AlarmConfigDialog.h
    src/alarm/AlarmSoundDialog.h      # ★ F5:报警声音设置对话框
    src/alarm/AlarmRowPalette.h        # ★ 2026-10-14:报警行配色唯一权威(纯函数,无 Q_OBJECT)
    src/alarm/AlarmTablePresenter.h   # ★ 2026-10-24:报警【行】的呈现(两套报警 UI 共用)
    src/alarm/ActionWiring.h          # ★ 2026-10-25:"报警→联动"接线的唯一出处(两边共用)
    src/util/ValueCoercion.h         # ★ 2026-10-25:值类型适配(控件写回 + 报警联动共用一份)
    # ★ 2026-10-10:协议下拉框"未实现"标注
    src/drivers/ProtocolAvailabilityUi.h
    src/history/AlarmHistoryView.h  # ★ F4:报警历史查询视图
    src/ui/MenuBuilder.h            # ★ 2026-10-16:菜单栏(抽出来才能被 tst_MenuBar 测到)
    src/plc/PlcConnectionManager.h  # ★ 2b:多 PLC 连接管理
    src/plc/VariableConfigDialog.h  # ★ 2026-10-19:变量配置对话框
    # ★ 2026-10-20:通讯帧监视器。CommFrame.h 是给 ModbusMaster 和
    #   CommTraceModel 共用的方向枚举(两边互相 include 会成环)。
    src/comm/CommTraceModel.h
    src/comm/CommFrame.h
    src/drivers/IPlcDriverProvider.h  # ★ 2b:纯接口无 Q_OBJECT(写进 HEADERS 便于 VS 里查得到)
)

set(FORMS
    hmi.ui
)

# ★ E.1b-2 修复 AUTOUIC 冲突:
#   即使从 GLOB 排除了 qcustomplot/examples/,AUTOUIC 默认还会扫描 .cpp/.h
#   找 ui_*.h include — 这步是为了双保险,把 qcustomplot/examples 排除在 AUTOUIC 搜索之外
set(CMAKE_AUTOUIC_SEARCH_PATHS "")

set(RESOURCES
    hmi.qrc
)

# ★ 显式列表 + GLOB 兜底(防"加了 .cpp 忘改 CMakeLists.txt"再次踩坑)
#   - 显式列表里的文件永远编(GLOB 不会重复加)
#   - 新增的 .cpp / .h 自动捡进 SOURCES / HEADERS
#   - CONFIGURE_DEPENDS 让 CMake 在 build 时检测文件变化,自动重 configure
#
# ★ F4(2026-10-08)修正:原来用 file(GLOB_RECURSE "*.cpp") 从 hmi/ 根开始递归。
#   它会一路钻进 tests/build_ninja/ 的 moc 中间文件目录,于是
#   【每加一个测试,build 都喷一百多行 "GLOB mismatch"】,
#   列出来的还是 moc_AlarmHistoryView.cpp 这种明显不该进 hmi.exe 的东西。
#   —— 不影响编译结果,但看着像"测试文件被编进主程序了",
#   下一个人得来花半小时确认有没有出事。
#   改成【显式列出要递归的目录】:根目录(非递归)+ src/ + src 的子目录。
#   tests/、out/、x64/ 从一开始就不在搜索路径里,不是"捡进来再过滤掉"。
file(GLOB EXTRA_CPP CONFIGURE_DEPENDS
    "*.cpp"                       # hmi/ 根下的 hmi.cpp / Alarm.cpp 等
)
file(GLOB_RECURSE EXTRA_CPP_SRC CONFIGURE_DEPENDS
    "src/*.cpp"
)
file(GLOB EXTRA_H CONFIGURE_DEPENDS
    "*.h"
)
file(GLOB_RECURSE EXTRA_H_SRC CONFIGURE_DEPENDS
    "src/*.h"
)
list(APPEND EXTRA_CPP ${EXTRA_CPP_SRC})
list(APPEND EXTRA_H   ${EXTRA_H_SRC})
list(REMOVE_ITEM EXTRA_CPP ${SOURCES})
list(REMOVE_ITEM EXTRA_H   ${HEADERS})
list(APPEND SOURCES ${EXTRA_CPP})
list(APPEND HEADERS ${EXTRA_H})
# ★ 给 VS 解决方案资源管理器分组(只在 VS 里生效,Xcode/CLion/Ninja 不受影响)
#   CMake 用 source_group() 声明逻辑组,VS 会按这些组在"CMake 目标视图"里建子文件夹
#   匹配规则:对每个文件按顺序找第一个匹配的组,所以 Sources 必须在 Headers 之前
source_group("Sources"     REGULAR_EXPRESSION ".*\.cpp$")
source_group("Headers"     REGULAR_EXPRESSION ".*\.h$")
source_group("Forms"       REGULAR_EXPRESSION ".*\.ui$")
source_group("Resources"   REGULAR_EXPRESSION ".*\.qrc$")
source_group("Build Files" REGULAR_EXPRESSION "(CMakeLists\.txt|.*Presets\.json|.*\.slnx|.*\.vcxproj.*|default\.json|LICENSE)$")

# 阶段 A:Debug + Release 都用 Windows 子系统
# 启动没黑框,用户没法通过关黑框误杀程序;qDebug 输出到 hmi.log(看 %APPDATA%\QianWei\HMI\logs)
# vcxproj 也对齐改了(两个配置都 SubSystem=Windows)

qt_add_executable(hmi ${SOURCES} ${HEADERS} ${FORMS} ${RESOURCES})
# 阶段 A:Debug + Release 都用 Windows 子系统
# 启动没黑框,用户没法通过关黑框误杀程序;qDebug 输出到 hmi.log(看 %APPDATA%\QianWei\HMI\logs)
# vcxproj 也对齐改了(两个配置都 SubSystem=Windows)
set_target_properties(hmi PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(hmi PRIVATE src)
# ★ E.1b-2 修复 CMake 构建历史遗留:
#   src/{history,auth,tag,export,logging}/ 下的 .cpp 用 `#include "Variable.h"`
#   /`#include "auth/Permission.h"`/`#include "tag/ExpressionParser.h"` 这种
#   相对 hmi 根目录的路径。target_include_directories(hmi PRIVATE src) 只把
#   src/ 加进路径,这些文件会找不到头。vcxproj 是把 hmi/ 根当 include 根的,
#   CMake 这边一直没补,这次首次用 build.ps1 编 hmi.exe 才暴露。
target_include_directories(hmi PRIVATE .)
# ★ D.2(2026-09-20):QCustomPlot 头文件路径(LGPL v2.1)
target_include_directories(hmi PRIVATE 3rdparty/qcustomplot)
# ★ 阶段 C · C.2 Snap7 集成占位
#   - DLL 到位后启用 3rdparty/snap7/include/ 头文件搜索
#   - 当前骨架阶段用 QLibrary 动态加载,不静态链 snap7.lib(LGPL 合规)
#   - include 路径暂不添加,等 C.2.2 真加载 Snap7 函数指针时再开
# target_include_directories(hmi PRIVATE 3rdparty/snap7/include)  # C.2.2 启用

target_link_libraries(hmi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::PrintSupport  # ★ D.2:QCustomPlot 需要 QPrinter(虽然我们不打印,但 QCP 头文件 include)
    Qt6::SerialBus
    Qt6::Sql        # ★ D.1(2026-09-18):SQLite 历史库
)

# 中文字符串源码用 UTF-8 编码,需告诉 MSVC
target_compile_options(hmi PRIVATE /utf-8)