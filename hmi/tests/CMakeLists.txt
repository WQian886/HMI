# 闃舵 A 路 A5 鍗曞厓娴嬭瘯
# 鐢?Qt Test,璺熶富绋嬪簭鍏辩敤 core 婧愮爜

cmake_minimum_required(VERSION 3.21)
project(hmi_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Widgets Network SerialBus Sql PrintSupport)
enable_testing()

# 涓荤▼搴忔簮鐮佺洰褰?鐢ㄦ潵 include 澶存枃浠?
set(HMI_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
include_directories(${HMI_SRC_DIR})
# 闃舵 B 路 B1:src/auth/ 澶存枃浠?#include "auth/Role.h" 椋庢牸)
include_directories(${HMI_SRC_DIR}/src)
# 鈽?D.2(2026-09-20):QCustomPlot 澶存枃浠惰矾寰?TrendWidget.h include "qcustomplot.h")
include_directories(${HMI_SRC_DIR}/3rdparty/qcustomplot)

# 娴嬭瘯瑕侀摼鎺ョ殑鏍稿績婧愭枃浠?
set(HMI_CORE_SOURCES
    ${HMI_SRC_DIR}/Variable.cpp
    ${HMI_SRC_DIR}/VariableManager.cpp
    ${HMI_SRC_DIR}/JsonSerializer.cpp
    # 闃舵 A 路 A3 鏃ュ織(UserManager 鐢?qCInfo/qCWarning)
    ${HMI_SRC_DIR}/src/logging/Logging.cpp
    # 闃舵 B 路 B1 鐢ㄦ埛/瑙掕壊/瀵嗙爜鍝堝笇
    ${HMI_SRC_DIR}/src/auth/Role.cpp
    ${HMI_SRC_DIR}/src/auth/User.cpp
    ${HMI_SRC_DIR}/src/auth/PasswordHasher.cpp
    # 闃舵 B 路 B1.2 UserManager
    ${HMI_SRC_DIR}/src/auth/UserManager.cpp
    # 闃舵 B 路 B1.3 UserStore
    ${HMI_SRC_DIR}/src/auth/UserStore.cpp
    # 闃舵 B 路 B2.2 CurrentUser
    ${HMI_SRC_DIR}/src/auth/CurrentUser.cpp
    # 闃舵 B 路 B3 Permission
    ${HMI_SRC_DIR}/src/auth/Permission.cpp
    # 闃舵 B 路 B4 UI 鏉冮檺缁戝畾
    ${HMI_SRC_DIR}/src/auth/UIPermission.cpp
    # 闃舵 B 路 B5 鎿嶄綔瀹¤
    ${HMI_SRC_DIR}/src/auth/AuditLog.cpp
    # 闃舵 C 路 C.0 schema 婕旇繘
    ${HMI_SRC_DIR}/Project.cpp
    ${HMI_SRC_DIR}/ActionManager.cpp
    ${HMI_SRC_DIR}/ThresholdAlarmChecker.cpp
    ${HMI_SRC_DIR}/TcpClient.cpp
    ${HMI_SRC_DIR}/Alarm.cpp
    ${HMI_SRC_DIR}/AlarmManager.cpp
    ${HMI_SRC_DIR}/PollScheduler.cpp
    ${HMI_SRC_DIR}/ModbusMaster.cpp
    ${HMI_SRC_DIR}/WriteScheduler.cpp
    # 闃舵 C 路 C.1 鍗忚鎶借薄灞?
    ${HMI_SRC_DIR}/src/drivers/IPollDriver.cpp
    ${HMI_SRC_DIR}/src/drivers/ModbusDriver.cpp
    ${HMI_SRC_DIR}/src/drivers/DriverFactory.cpp
    # 闃舵 C 路 C.2 S7 鍗忚閫傞厤鍣?2026-09-18)DriverFactory 鍒涘缓 S7Driver 闇€瑕侀摼鎺?S7Driver.cpp
    ${HMI_SRC_DIR}/src/drivers/S7Driver.cpp
    # 闃舵 C 路 C.3 EIP 鍗忚閫傞厤鍣?2026-09-18)DriverFactory 鍒涘缓 EipDriver 闇€瑕侀摼鎺?EipDriver.cpp
    ${HMI_SRC_DIR}/src/drivers/EipDriver.cpp
    # 闃舵 D 路 D.1 SQLite 鍘嗗彶搴?2026-09-18)
    ${HMI_SRC_DIR}/src/history/HistoryRecorder.cpp
    # 鈽?D.3(2026-09-20):鎶ヨ鎸佷箙鍖?瀵圭О HistoryRecorder + alarm_history 琛?
    ${HMI_SRC_DIR}/src/history/AlarmRecorder.cpp
    # 鈽?D.4(2026-09-20):CSV 瀵煎嚭
    ${HMI_SRC_DIR}/src/export/CsvExporter.cpp
    # 鈽?D.6(2026-09-20):娲剧敓琛ㄨ揪寮忚В鏋愬櫒(Pratt 鎵嬪啓)
    ${HMI_SRC_DIR}/src/tag/ExpressionParser.cpp
    # 鈽?D.7(2026-09-20):鏍囩瀵煎叆鍣?CSV-based)
    ${HMI_SRC_DIR}/src/tag/TagImporter.cpp
    # 鈽?D.2(2026-09-20):鍘嗗彶鏇茬嚎鎺т欢 + QCustomPlot
    ${HMI_SRC_DIR}/src/widgets/TrendWidget.cpp
    ${HMI_SRC_DIR}/3rdparty/qcustomplot/qcustomplot.cpp
    # 鈽?E.1(2026-09-20):鎺т欢鍩虹被 + LabelControl
    ${HMI_SRC_DIR}/src/widgets/base/ControlBase.cpp
    ${HMI_SRC_DIR}/src/widgets/LabelControl.cpp
    # 鈽?E.1a-2(2026-09-20):NumberDisplay + TextDisplay
    ${HMI_SRC_DIR}/src/widgets/NumberDisplay.cpp
    ${HMI_SRC_DIR}/src/widgets/TextDisplay.cpp
    # 鈽?E.1a-3(2026-09-20):Bar + Gauge + LED
    ${HMI_SRC_DIR}/src/widgets/BarControl.cpp
    ${HMI_SRC_DIR}/src/widgets/GaugeControl.cpp
    ${HMI_SRC_DIR}/src/widgets/LedControl.cpp
    # 鈽?E.1a-4(2026-09-20):Image + Rectangle + Line + Group
    ${HMI_SRC_DIR}/src/widgets/ImageControl.cpp
    ${HMI_SRC_DIR}/src/widgets/RectangleControl.cpp
    ${HMI_SRC_DIR}/src/widgets/LineControl.cpp
    ${HMI_SRC_DIR}/src/widgets/GroupControl.cpp
    # 鈽?E.1b(2026-09-20):Button + Toggle + Slider + InputEdit
    ${HMI_SRC_DIR}/src/widgets/ButtonControl.cpp
    ${HMI_SRC_DIR}/src/widgets/ToggleControl.cpp
    ${HMI_SRC_DIR}/src/widgets/SliderControl.cpp
    ${HMI_SRC_DIR}/src/widgets/InputEditControl.cpp
    # 鈽?E.1b-2(2026-09-20):Trend + Table + AlarmList
    ${HMI_SRC_DIR}/src/widgets/TrendControl.cpp
    ${HMI_SRC_DIR}/src/widgets/TableControl.cpp
    ${HMI_SRC_DIR}/src/widgets/AlarmListControl.cpp
    # 鈽?E.1c(2026-09-28):5 涓伐涓氭帶浠?鍌ㄧ綈/绠￠亾/闃€闂?娉?鐢垫満)
    #   閫愪釜鍔?鍐欏畬涓€涓帴绾夸竴涓?閬垮厤鏂囦欢杩樹笉瀛樺湪灏辩紪涓嶈繃
    ${HMI_SRC_DIR}/src/widgets/TankControl.cpp
    # 鈽?E.2(2026-09-21):灞炴€ч潰鏉?PropertySheet + PropertyChangeCommand
    ${HMI_SRC_DIR}/src/editor/PropertySheet.cpp
    ${HMI_SRC_DIR}/src/editor/PropertyChangeCommand.cpp
    # 鈽?E.4c(2026-09-22):QSizeF/QPointF 澶嶅悎缂栬緫鍣?
    ${HMI_SRC_DIR}/src/editor/SizeEditor.cpp
    ${HMI_SRC_DIR}/src/editor/PositionEditor.cpp
    # 鈽?E.2 鍛?11:EditorMainWindow
    ${HMI_SRC_DIR}/src/editor/EditorMainWindow.cpp
    # 鈽?E.4a:PaletteWidget
    ${HMI_SRC_DIR}/src/editor/PaletteWidget.cpp
    # 鈽?E.4b:ResizeHandleItem
    ${HMI_SRC_DIR}/src/editor/ResizeHandleItem.cpp
    # 鈽?E.4d 缁?閫変腑鎬佽瑙夌粺涓€
    ${HMI_SRC_DIR}/src/editor/SelectionFrameItem.cpp
    # 鈽?E.4d 缁?3:澶氶€夌粍 handle 瀹瑰櫒
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
        Qt6::Widgets   # 鈽?B4:UIPermission.cpp 闇€瑕?QAbstractButton / QAction
        Qt6::Network   # 鈽?C.0:Project.cpp 闂存帴 include QTcpSocket(TcpClient.h)
        Qt6::SerialBus  # 鈽?C.0:PollScheduler.h include QModbusDataUnit(ThresholdAlarmChecker 渚濊禆)
        Qt6::Sql        # 鈽?D.1(2026-09-18):SQLite 鍘嗗彶搴?+ HistoryRecorder
        Qt6::PrintSupport  # 鈽?D.2(2026-09-20):QCustomPlot 闇€瑕?QPrinter
    )
    target_compile_options(${test_name} PRIVATE /utf-8)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_hmi_test(tst_Variable)
add_hmi_test(tst_VariableManager)
add_hmi_test(tst_Serializer)
# 闃舵 B 路 B1
add_hmi_test(tst_PasswordHasher)
# 闃舵 B 路 B1.2
add_hmi_test(tst_UserManager)
# 闃舵 B 路 B1.3
add_hmi_test(tst_UserStore)
# 闃舵 B 路 B2.2
add_hmi_test(tst_CurrentUser)
# 闃舵 B 路 B3
add_hmi_test(tst_Permission)
# 闃舵 B 路 B4
add_hmi_test(tst_UIPermission)
# 闃舵 B 路 B5
add_hmi_test(tst_AuditLog)
add_hmi_test(tst_AuditE2E)
# 闃舵 C 路 C.0
add_hmi_test(tst_ProjectSchema)
# 闃舵 C 路 C.1 鍗忚鎶借薄灞?
add_hmi_test(tst_IPollDriver)
# 闃舵 C 路 C.4 鍗忚閫夋嫨 UX 閿?鏁欒 1.5)
add_hmi_test(tst_ProtocolSwitch)
# 闃舵 D 路 D.1 SQLite 鍘嗗彶搴?
add_hmi_test(tst_HistoryRecorder)
# 闃舵 D 路 D.3 鎶ヨ鎸佷箙鍖?
add_hmi_test(tst_AlarmRecorder)
# 闃舵 D 路 D.4 CSV 瀵煎嚭
add_hmi_test(tst_CsvExporter)
# 闃舵 D 路 D.6 娲剧敓琛ㄨ揪寮忚В鏋?
add_hmi_test(tst_ExpressionParser)
# 闃舵 D 路 D.7 鏍囩瀵煎叆(CSV-based,闆朵笁鏂逛緷璧?
add_hmi_test(tst_TagImporter)
# 闃舵 E 路 E.1a 绗?1 涓帶浠?LabelControl(娴嬭瘯闇€瑕?QApplication + offscreen)
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
# 鈽?E.1c(2026-09-28):UI 鈫?PLC 鍙嶅悜鍐欓€氶亾(ControlBase::writeToPlc)
#   淇鍓?ToggleControl 鐐逛竴涓嬪彧缈绘湰鍦板€?鍏ㄤ粨搴撴棤浜烘帴 toggled 淇″彿,
#   17 涓帶浠跺叏鏄彧璇绘樉绀哄櫒銆傝瑙?tst_ControlBaseWrite.cpp 澶存敞閲娿€?
add_hmi_test(tst_ControlBaseWrite)
# 鈽?E.1c(2026-09-28):QColor 灞炴€у瓨璇诲洖褰?QJsonValue::fromVariant 涓嶈 QColor,
#   瀵艰嚧 17 涓帶浠剁殑 8 涓鑹插睘鎬у瓨鐩樺悗鍏ㄤ涪)
add_hmi_test(tst_ControlBaseColorJson)
# 鈽?E.1c(2026-09-28):5 涓伐涓氭帶浠?琛ラ綈 E 闃舵 22 鎺т欢鐨勬渶鍚?5 涓?
add_hmi_test(tst_TankControl)
# 闃舵 E 路 E.2 灞炴€ч潰鏉?PropertySheet
add_hmi_test(tst_PropertySheet)
# 闃舵 E 路 E.2 鍛?11 EditorMainWindow
add_hmi_test(tst_EditorMainWindow)
# 闃舵 E 路 E.4a 鎺т欢璋冭壊鏉?PaletteWidget
add_hmi_test(tst_PaletteWidget)
# 闃舵 E 路 E.4b 8 handle 缂╂斁 ResizeHandleItem
add_hmi_test(tst_ResizeHandle)
# handle 瑙嗚瀵归綈璋冭瘯
add_hmi_test(tst_HandleVisualAlignment)
# 闃舵 D 路 D.2 鍘嗗彶鏇茬嚎鎺т欢(QCustomPlot)
add_hmi_test(tst_Trend)
# 璋冭瘯 QGraphicsObject Q_PROPERTY 琛屼负
add_hmi_test(tst_GraphicsObjectProperty)
# 绔埌绔?AlarmList 缂╁皬澶辨晥璋冭瘯
add_hmi_test(tst_AlarmListShrinkE2E)

option(HMI_ENABLE_COVERAGE "Enable coverage build" OFF)

