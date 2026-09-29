# 阶段 A · A5 单元测试
# 用 Qt Test,跟主程序共用 core 源码

cmake_minimum_required(VERSION 3.21)
project(hmi_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network SerialBus Sql PrintSupport)
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
    # 阶段 D · D.1 SQLite 历史库(2026-09-18)
    ${HMI_SRC_DIR}/src/history/HistoryRecorder.cpp
    # ★ D.3(2026-09-20):报警持久化(对称 HistoryRecorder + alarm_history 表)
    ${HMI_SRC_DIR}/src/history/AlarmRecorder.cpp
    # ★ D.4(2026-09-20):CSV 导出
    ${HMI_SRC_DIR}/src/export/CsvExporter.cpp
    # ★ D.6(2026-09-20):派生表达式解析器(Pratt 手写)
    ${HMI_SRC_DIR}/src/tag/ExpressionParser.cpp
    # ★ D.7(2026-09-20):标签导入器(CSV-based)
    ${HMI_SRC_DIR}/src/tag/TagImporter.cpp
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
)

function(add_hmi_test test_name)
    add_executable(${test_name}
        ${test_name}.cpp
        ${HMI_CORE_SOURCES}
    )
    target_link_libraries(${test_name} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets   # ★ B4:UIPermission.cpp 需要 QAbstractButton / QAction
        Qt6::Network   # ★ C.0:Project.cpp 间接 include QTcpSocket(TcpClient.h)
        Qt6::SerialBus  # ★ C.0:PollScheduler.h include QModbusDataUnit(ThresholdAlarmChecker 依赖)
        Qt6::Sql        # ★ D.1(2026-09-18):SQLite 历史库 + HistoryRecorder
        Qt6::PrintSupport  # ★ D.2(2026-09-20):QCustomPlot 需要 QPrinter
    )
    target_compile_options(${test_name} PRIVATE /utf-8)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_hmi_test(tst_Variable)
add_hmi_test(tst_VariableManager)
add_hmi_test(tst_Serializer)
# 阶段 B · B1
add_hmi_test(tst_PasswordHasher)
# 阶段 B · B1.2
add_hmi_test(tst_UserManager)
# 阶段 B · B1.3
add_hmi_test(tst_UserStore)
# 阶段 B · B2.2
add_hmi_test(tst_CurrentUser)
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
# 阶段 C · C.4 协议选择 UX 锁(教训 1.5)
add_hmi_test(tst_ProtocolSwitch)
# 阶段 D · D.1 SQLite 历史库
add_hmi_test(tst_HistoryRecorder)
# 阶段 D · D.3 报警持久化
add_hmi_test(tst_AlarmRecorder)
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
# ★ E.4d-follow-8(2026-09-29):对齐/等尺寸的 Undo(9 个动作,原为零覆盖)
add_hmi_test(tst_GeometryUndo)
# ★ E.4d-follow-9(2026-09-29):选中框【真的画出来了吗】—— 渲染到 QImage 数像素,
#   补上 tst_EditorMainWindow 那 4 个只断言几何、不看画面的用例的盲区
add_hmi_test(tst_SelectionFrameRender)
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

option(HMI_ENABLE_COVERAGE "Enable coverage build" OFF)
