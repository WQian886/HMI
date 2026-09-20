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
    # ★ D.2(2026-09-20):历史曲线控件 + QCustomPlot
    ${HMI_SRC_DIR}/src/widgets/TrendWidget.cpp
    ${HMI_SRC_DIR}/3rdparty/qcustomplot/qcustomplot.cpp
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
# 阶段 D · D.2 历史曲线控件(QCustomPlot)
add_hmi_test(tst_Trend)

option(HMI_ENABLE_COVERAGE "Enable coverage build" OFF)