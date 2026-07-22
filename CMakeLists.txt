cmake_minimum_required(VERSION 3.21)
project(hmi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Widgets SerialBus)

qt_standard_project_setup()

set(SOURCES
    main.cpp
    hmi.cpp
    TcpClient.cpp
    ModbusDemo.cpp
    ModbusMaster.cpp  # 2026-07-08:阶段 2 第 4 练补回 —— 没加进 SOURCES 导致链接缺符号
    Variable.cpp          # 阶段 3.1:Variable 数据模型
    VariableManager.cpp   # 阶段 3.1:变量管理器
    PollScheduler.cpp      # 阶段 3.3:定时轮询调度器
)

set(HEADERS
    hmi.h
    TcpClient.h
    ModbusDemo.h
    ModbusMaster.h  # 2026-07-08:同上
    Variable.h            # 阶段 3.1
    VariableManager.h     # 阶段 3.1
    PollScheduler.h       # 阶段 3.3
)

set(FORMS
    hmi.ui
)

set(RESOURCES
    hmi.qrc
)

qt_add_executable(hmi WIN32 ${SOURCES} ${HEADERS} ${FORMS} ${RESOURCES})

target_link_libraries(hmi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::SerialBus
)

# 中文字符串源码用 UTF-8 编码,需告诉 MSVC
target_compile_options(hmi PRIVATE /utf-8)
